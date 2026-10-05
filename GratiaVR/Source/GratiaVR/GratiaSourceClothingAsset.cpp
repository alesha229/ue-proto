#include "GratiaSourceClothingAsset.h"

#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "SkeletalMeshTypes.h"
#if WITH_EDITOR
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogGratiaSourceClothBinding, Log, All);

UGratiaSourceClothingAsset::UGratiaSourceClothingAsset(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void UGratiaSourceClothingAsset::PostLoad()
{
    Super::PostLoad();
    if (!PhysicsAsset) return;
    PhysicsAsset->ConditionalPostLoad();
    for (USkeletalBodySetup* Body : PhysicsAsset->SkeletalBodySetups)
    {
        if (!Body) continue;
        Body->ConditionalPostLoad();
        if (Body->AggGeom.ConvexElems.IsEmpty()) continue;
        Body->CreatePhysicsMeshes();
        const int32 Removed = Body->AggGeom.ConvexElems.RemoveAll([](const FKConvexElem& Convex) { return !Convex.GetChaosConvexMesh(); });
        if (Removed)
            UE_LOG(LogGratiaSourceClothBinding, Error, TEXT("SOURCE_CLOTH_COLLIDER_UNAVAILABLE asset=%s bone=%s removed_convexes=%d"),
                *GetName(), *Body->BoneName.ToString(), Removed);
    }
}

#if WITH_EDITOR
uint32 UGratiaSourceClothingAsset::HashSectionPositions(const FSkelMeshSection& Section)
{
    uint32 Hash = GetTypeHash(Section.SoftVertices.Num());
    for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
        for (int32 Axis = 0; Axis < 3; ++Axis)
            Hash = HashCombineFast(Hash, GetTypeHash(FMath::RoundToInt(Vertex.Position[Axis] * 1000.f)));
    return Hash;
}

const FGratiaStoredClothSectionMapping* UGratiaSourceClothingAsset::FindStoredMapping(int32 SectionIndex) const
{
    return StoredMappings.FindByPredicate([SectionIndex](const auto& Stored) { return Stored.SectionIndex == SectionIndex; });
}

namespace
{
bool GratiaReadStoredMapping(const FGratiaStoredClothSectionMapping& Stored, const FSkelMeshSection& Section, TArray<FMeshToMeshVertData>& Out)
{
    if (Stored.NumVertices != Section.SoftVertices.Num()
        || Stored.MappingBytes.Num() != Stored.NumVertices * int32(sizeof(FMeshToMeshVertData))
        || Stored.PositionHash != UGratiaSourceClothingAsset::HashSectionPositions(Section)) return false;
    Out.SetNumUninitialized(Stored.NumVertices);
    FMemory::Memcpy(Out.GetData(), Stored.MappingBytes.GetData(), Stored.MappingBytes.Num());
    return true;
}
}

bool UGratiaSourceClothingAsset::IsSectionUsingStoredMapping(const USkeletalMesh* Mesh, int32 MeshLodIndex, int32 SectionIndex) const
{
    const auto* Model = Mesh ? const_cast<USkeletalMesh*>(Mesh)->GetImportedModel() : nullptr;
    if (!Model || !Model->LODModels.IsValidIndex(MeshLodIndex) || !Model->LODModels[MeshLodIndex].Sections.IsValidIndex(SectionIndex)) return false;
    const FSkelMeshSection& Section = Model->LODModels[MeshLodIndex].Sections[SectionIndex];
    const auto* Stored = FindStoredMapping(SectionIndex);
    TArray<FMeshToMeshVertData> Expected;
    return Stored && Section.ClothingData.AssetGuid == GetAssetGuid() && Section.ClothMappingDataLODs.Num() >= 1
        && GratiaReadStoredMapping(*Stored, Section, Expected) && Section.ClothMappingDataLODs[0] == Expected;
}

bool UGratiaSourceClothingAsset::BindToSkeletalMesh(USkeletalMesh* InSkelMesh, const int32 InMeshLodIndex, const int32 InSectionIndex, const int32 InAssetLodIndex)
{
    // Without stored data this is an ordinary engine-bound clothing asset.
    if (StoredMappings.IsEmpty()) return Super::BindToSkeletalMesh(InSkelMesh, InMeshLodIndex, InSectionIndex, InAssetLodIndex);
    if (!InSkelMesh || InSkelMesh != GetOuter() || !InSkelMesh->GetImportedModel()
        || !InSkelMesh->GetImportedModel()->LODModels.IsValidIndex(InMeshLodIndex) || !LodData.IsValidIndex(InAssetLodIndex)) return false;
    FSkeletalMeshLODModel& Lod = InSkelMesh->GetImportedModel()->LODModels[InMeshLodIndex];
    if (!Lod.Sections.IsValidIndex(InSectionIndex)) return false;
    const auto* Stored = FindStoredMapping(InSectionIndex);
    TArray<FMeshToMeshVertData> Mapping;
    if (!Stored || !GratiaReadStoredMapping(*Stored, Lod.Sections[InSectionIndex], Mapping))
    {
        // A generic nearest-triangle binding would move unrelated vertices (for
        // example hair over the chest). Leave the section skinned and report it.
        UE_LOG(LogGratiaSourceClothBinding, Error, TEXT("SOURCE_CLOTH_BINDING_REJECTED asset=%s section=%d reason=%s; section stays skinned, rerun the cloth port"),
            *GetName(), InSectionIndex, Stored ? TEXT("render section changed") : TEXT("no stored mapping"));
        return false;
    }
    const bool bAlreadyMapped = LodMap.IsValidIndex(InMeshLodIndex) && LodMap[InMeshLodIndex] == InAssetLodIndex;
    if (!bAlreadyMapped)
    {
        // First section: the engine binds asset/LOD bookkeeping, bones and bias data.
        if (!Super::BindToSkeletalMesh(InSkelMesh, InMeshLodIndex, InSectionIndex, InAssetLodIndex)) return false;
    }
    else
    {
        // Further sections share the asset LOD, which the engine path refuses.
        FSkelMeshSection& Section = Lod.Sections[InSectionIndex];
        int32 AssetIndex = INDEX_NONE;
        if (!InSkelMesh->GetMeshClothingAssets().Find(this, AssetIndex)) return false;
        for (const FName BoneName : UsedBoneNames)
        {
            const int32 BoneIndex = InSkelMesh->GetRefSkeleton().FindBoneIndex(BoneName);
            if (BoneIndex == INDEX_NONE) continue;
            Section.BoneMap.AddUnique(BoneIndex);
            if (!Lod.RequiredBones.Contains(BoneIndex)) { Lod.RequiredBones.Add(BoneIndex); Lod.ActiveBoneIndices.AddUnique(BoneIndex); }
        }
        Lod.RequiredBones.Sort();
        InSkelMesh->GetRefSkeleton().EnsureParentsExistAndSort(Lod.ActiveBoneIndices);
        Section.CorrespondClothAssetIndex = AssetIndex;
        Section.ClothingData.AssetGuid = GetAssetGuid();
        Section.ClothingData.AssetLodIndex = InAssetLodIndex;
    }
    FSkelMeshSection& Section = Lod.Sections[InSectionIndex];
    Section.ClothMappingDataLODs.SetNum(1);
    Section.ClothMappingDataLODs[0] = MoveTemp(Mapping);
    UE_LOG(LogGratiaSourceClothBinding, Display, TEXT("SOURCE_CLOTH_BINDING_RESTORED asset=%s section=%d vertices=%d"),
        *GetName(), InSectionIndex, Section.SoftVertices.Num());
    return true;
}

TArray<TPair<FText, FText>> UGratiaSourceClothingAsset::GetStats() const
{
    // The base implementation is not exported from its module.
    TArray<TPair<FText, FText>> Stats;
    for (int32 Index = 0; Index < LodData.Num(); ++Index)
        Stats.Emplace(FText::FromString(FString::Printf(TEXT("LOD%d simulation vertices"), Index)),
            FText::AsNumber(LodData[Index].PhysicalMeshData.Vertices.Num()));
    Stats.Emplace(FText::FromString(TEXT("Stored source-mask sections")), FText::AsNumber(StoredMappings.Num()));
    return Stats;
}
#endif
