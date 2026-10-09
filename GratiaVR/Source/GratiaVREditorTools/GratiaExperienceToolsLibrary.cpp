#include "GratiaExperienceToolsLibrary.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "AssetCompilingManager.h"
#include "ContentStreaming.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Math/RandomStream.h"
#include "Engine/SkeletalMesh.h"
#include "SkeletalMeshAttributes.h"
#include "SkinnedAssetCompiler.h"
#include "ReferenceSkeleton.h"

bool UGratiaExperienceToolsLibrary::BuildCompositeFont(UFont* Font, const TArray<FName>& Names, const TArray<UFontFace*>& Faces)
{
    if (!Font || Names.IsEmpty() || Names.Num() != Faces.Num()) return false;
    Font->Modify();
    Font->FontCacheType = EFontCacheType::Runtime;
    FTypeface& Typeface = Font->GetMutableInternalCompositeFont().DefaultTypeface;
    Typeface.Fonts.Reset();
    for (int32 Index = 0; Index < Names.Num(); ++Index)
    {
        if (!Faces[Index]) return false;
        FTypefaceEntry& Entry = Typeface.Fonts.Emplace_GetRef(Names[Index]);
        Entry.Font = FFontData(Faces[Index]);
    }
    Font->LegacyFontName = Names[0];
    Font->MarkPackageDirty();
    return true;
}

void UGratiaExperienceToolsLibrary::FinishCompilationAndStreaming()
{
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    FAssetCompilingManager::Get().FinishAllCompilation();
    IStreamingManager::Get().StreamAllResources(10.0f);
    FlushRenderingCommands();
}

int32 UGratiaExperienceToolsLibrary::ReduceStaticMeshLOD0(UStaticMesh* Mesh, float PercentTriangles)
{
    if (!Mesh || Mesh->GetNumSourceModels() < 1 || !FMath::IsFinite(PercentTriangles)) return -1;
    FMeshReductionSettings& Settings = Mesh->GetSourceModel(0).ReductionSettings;
    Settings.PercentTriangles = FMath::Clamp(PercentTriangles, 0.01f, 1.0f);
    Settings.PercentVertices = 1.0f;
    Settings.TerminationCriterion = EStaticMeshReductionTerimationCriterion::Triangles;
    Mesh->Modify();
    Mesh->Build(true);
    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();
    return Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0 ? Mesh->GetRenderData()->LODResources[0].GetNumTriangles() : -1;
}

UStaticMesh* UGratiaExperienceToolsLibrary::CreateRainStreakMesh(const FString& PackageName, int32 Streaks, float Radius, float Length,
    float Width, float FallHeight, int32 Seed, const TArray<FVector>& Clearings)
{
    if (!FPackageName::IsValidLongPackageName(PackageName) || Streaks < 1 || Streaks > 100000 || !(Radius > 0.0f) || !(Length > 0.0f)
        || !(Width > 0.0f) || !(FallHeight >= 0.0f) || !FMath::IsFinite(Radius + Length + Width + FallHeight)) return nullptr;
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!Mesh)
    {
        UPackage* Package = CreatePackage(*PackageName);
        Mesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Mesh);
    }
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description);
    Attributes.Register();
    TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
    UVs.SetNumChannels(2);
    TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
    TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
    TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
    const FPolygonGroupID Group = Description.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Rain");
    Description.ReserveNewVertices(Streaks * 8);
    Description.ReserveNewVertexInstances(Streaks * 8);
    Description.ReserveNewTriangles(Streaks * 4);
    FRandomStream Random(Seed);
    for (int32 Streak = 0, Tries = 0; Streak < Streaks && Tries < Streaks * 20; ++Tries)
    {
        // Uniform over the disc outside the clearings; phase and brightness are shared by both quads of the streak.
        const float Distance = Radius * FMath::Sqrt(Random.FRand());
        const float Angle = Random.FRand() * UE_TWO_PI;
        const FVector3f Base(Distance * FMath::Cos(Angle), Distance * FMath::Sin(Angle), 0.0f);
        if (Clearings.ContainsByPredicate([&Base](const FVector& Clear) { return FVector2f::Distance(FVector2f(Base.X, Base.Y), FVector2f(Clear.X, Clear.Y)) < Clear.Z; }))
            continue;
        ++Streak;
        const FVector2f Seeds(Random.FRand(), 0.5f + 0.5f * Random.FRand());
        for (int32 Plane = 0; Plane < 2; ++Plane)
        {
            const FVector3f Across = Plane == 0 ? FVector3f(1.0f, 0.0f, 0.0f) : FVector3f(0.0f, 1.0f, 0.0f);
            const FVector3f Facing = Plane == 0 ? FVector3f(0.0f, 1.0f, 0.0f) : FVector3f(1.0f, 0.0f, 0.0f);
            FVertexInstanceID Corners[4];
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                const float U = Corner == 1 || Corner == 2 ? 1.0f : 0.0f;
                const float V = Corner >= 2 ? 1.0f : 0.0f;
                const FVertexID Vertex = Description.CreateVertex();
                Positions[Vertex] = Base + Across * ((U - 0.5f) * Width) + FVector3f(0.0f, 0.0f, V * Length);
                Corners[Corner] = Description.CreateVertexInstance(Vertex);
                UVs.Set(Corners[Corner], 0, FVector2f(U, V));
                UVs.Set(Corners[Corner], 1, Seeds);
                Normals[Corners[Corner]] = Facing;
                Tangents[Corners[Corner]] = Across;
                Signs[Corners[Corner]] = 1.0f;
                Colors[Corners[Corner]] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
            }
            Description.CreateTriangle(Group, {Corners[0], Corners[1], Corners[2]});
            Description.CreateTriangle(Group, {Corners[0], Corners[2], Corners[3]});
        }
    }
    Mesh->Modify();
    Mesh->SetNumSourceModels(1);
    FMeshBuildSettings& Settings = Mesh->GetSourceModel(0).BuildSettings;
    Settings.bRecomputeNormals = false;
    Settings.bRecomputeTangents = false;
    Settings.bRemoveDegenerates = false;
    Settings.bUseFullPrecisionUVs = true;
    Settings.bGenerateLightmapUVs = false;
    Mesh->CreateMeshDescription(0, MoveTemp(Description));
    Mesh->CommitMeshDescription(0);
    Mesh->GetStaticMaterials().Reset();
    Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Rain")));
    Mesh->SetPositiveBoundsExtension(FVector(0.0, 0.0, FallHeight));
    Mesh->Build(true);
    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();
    return Mesh;
}

UFontFace* UGratiaExperienceToolsLibrary::ImportFontFace(const FString& Filename, const FString& PackageName)
{
    TArray<uint8> Bytes;
    if (!FPackageName::IsValidLongPackageName(PackageName) || !FFileHelper::LoadFileToArray(Bytes, *Filename) || Bytes.IsEmpty()) return nullptr;
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    UFontFace* Face = LoadObject<UFontFace>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!Face)
    {
        UPackage* Package = CreatePackage(*PackageName);
        Face = NewObject<UFontFace>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Face);
    }
    Face->Modify();
    Face->SourceFilename = Filename;
    Face->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(Bytes));
    Face->CacheSubFaces();
    Face->MarkPackageDirty();
    return Face;
}

int32 UGratiaExperienceToolsLibrary::CreateChannelOpeningMorph(USkeletalMesh* Mesh, FName MorphName, const TArray<FName>& EntranceBones, FName InwardBone,
    const TArray<FName>& LeftBones, const TArray<FName>& RightBones, float OpeningCm, float CoreRadiusCm, float FalloffCm,
    float OutsideCm, float InsideCm, float AlongSlit)
{
    if (!Mesh || MorphName.IsNone() || EntranceBones.IsEmpty() || OpeningCm <= 0.0f || !Mesh->HasMeshDescription(0)) return -1;
    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    auto RefLocation = [&Ref](FName Bone, FVector& Out)
    {
        const int32 Index = Ref.FindBoneIndex(Bone);
        if (Index == INDEX_NONE) return false;
        FTransform Pose = Ref.GetRefBonePose()[Index];
        for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent)) Pose *= Ref.GetRefBonePose()[Parent];
        Out = Pose.GetLocation();
        return true;
    };
    auto Centre = [&RefLocation](const TArray<FName>& Bones, FVector& Out)
    {
        Out = FVector::ZeroVector;
        int32 Found = 0;
        for (const FName Bone : Bones) { FVector Point; if (RefLocation(Bone, Point)) { Out += Point; ++Found; } }
        if (Found) Out /= Found;
        return Found == Bones.Num() && Found > 0;
    };
    FVector Entrance, Target;
    if (!Centre(EntranceBones, Entrance) || !RefLocation(InwardBone, Target)) return -1;
    const FVector Inward = (Target - Entrance).GetSafeNormal();
    if (Inward.IsNearlyZero()) return -1;
    // Across a slit: from its right lip to its left one, square to the axis.
    FVector Lateral = FVector::ZeroVector, Left, Right;
    if (!LeftBones.IsEmpty() && !RightBones.IsEmpty() && Centre(LeftBones, Left) && Centre(RightBones, Right))
    {
        Lateral = Left - Right;
        Lateral = (Lateral - Inward * FVector::DotProduct(Lateral, Inward)).GetSafeNormal();
    }
    // A falloff of at least 1.875x the opening keeps the radial mapping monotonic (smootherstep's steepest slope).
    const double Opening = OpeningCm, Core = FMath::Max(0.0f, CoreRadiusCm), Falloff = FMath::Max(double(FalloffCm), 2.0 * Opening);
    const double Outside = FMath::Max(0.0f, OutsideCm), Inside = FMath::Max(0.0f, InsideCm), Edge = 0.5 * Falloff;
    auto Smoother = [](double T) { T = FMath::Clamp(T, 0.0, 1.0); return T * T * T * (T * (6.0 * T - 15.0) + 10.0); };
    FMeshDescription* Description = Mesh->GetMeshDescription(0);
    if (!Description) return -1;
    FSkeletalMeshAttributes Attributes(*Description);
    if (!Attributes.GetMorphTargetNames().Contains(MorphName) && !Attributes.RegisterMorphTargetAttribute(MorphName, false)) return -1;
    TVertexAttributesRef<FVector3f> Deltas = Attributes.GetVertexMorphPositionDelta(MorphName);
    const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
    int32 Moved = 0;
    for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
    {
        const FVector Relative = FVector(Positions[Vertex]) - Entrance;
        const double Along = FVector::DotProduct(Relative, Inward);
        FVector Radial = Relative - Inward * Along;
        const double Distance = Radial.Size();
        FVector Delta = FVector::ZeroVector;
        if (Distance > 0.02 && Distance < Core + Falloff && Along > -(Outside + Edge) && Along < Inside + Edge)
        {
            Radial /= Distance;
            const double Across = 1.0 - Smoother((Distance - Core) / Falloff);
            const double Depth = (1.0 - Smoother((-Along - Outside) / Edge)) * (1.0 - Smoother((Along - Inside) / Edge));
            const double Shape = Lateral.IsZero() ? 1.0 : FMath::Lerp(double(AlongSlit), 1.0, FMath::Abs(FVector::DotProduct(Radial, Lateral)));
            Delta = Radial * (Opening * Across * Depth * Shape);
        }
        Deltas[Vertex] = FVector3f(Delta);
        if (Delta.SizeSquared() > 1.0e-6) ++Moved;
    }
    USkeletalMesh::FCommitMeshDescriptionParams Params;
    Params.bMarkPackageDirty = true;
    if (!Mesh->CommitMeshDescription(0, Params)) return -1;
    Mesh->Build();
    // Skeletal meshes build asynchronously; the morph exists for FindMorphTarget only once the build finished.
    USkinnedAsset* const Built[] = {Mesh};
    FSkinnedAssetCompilingManager::Get().FinishCompilation(Built);
    Mesh->MarkPackageDirty();
    UE_LOG(LogTemp, Display, TEXT("GRATIA_CHANNEL_MORPH %s moved=%d opening=%.1fcm falloff=%.1fcm slit=%s"), *MorphName.ToString(), Moved, Opening, Falloff,
        Lateral.IsZero() ? TEXT("no") : TEXT("yes"));
    return Moved;
}

namespace
{
/** Reference-pose (component space) centre of Bones; false when one is missing. */
bool GratiaRefCentre(const FReferenceSkeleton& Ref, const TArray<FName>& Bones, FVector& Out)
{
    Out = FVector::ZeroVector;
    for (const FName Bone : Bones)
    {
        const int32 Index = Ref.FindBoneIndex(Bone);
        if (Index == INDEX_NONE) return false;
        FTransform Pose = Ref.GetRefBonePose()[Index];
        for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent)) Pose *= Ref.GetRefBonePose()[Parent];
        Out += Pose.GetLocation();
    }
    if (Bones.IsEmpty()) return false;
    Out /= Bones.Num();
    return true;
}
}

int32 UGratiaExperienceToolsLibrary::CreateChannelBulgeMorph(USkeletalMesh* Mesh, FName MorphName, const TArray<FName>& EntranceBones, FName InwardBone,
    float DepthCm, const TArray<FName>& FrontBones, const TArray<FName>& BackBones, float RadiusCm, float AmountCm, float FloorCm)
{
    if (!Mesh || MorphName.IsNone() || AmountCm <= 0.0f || !Mesh->HasMeshDescription(0)) return -1;
    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    FVector Entrance, Target, Front, Back;
    if (!GratiaRefCentre(Ref, EntranceBones, Entrance) || !GratiaRefCentre(Ref, {InwardBone}, Target)
        || !GratiaRefCentre(Ref, FrontBones, Front) || !GratiaRefCentre(Ref, BackBones, Back)) return -1;
    const FVector Inward = (Target - Entrance).GetSafeNormal();
    FVector Forward = Front - Back;
    Forward.Z = 0.0;
    if (Inward.IsNearlyZero() || !Forward.Normalize()) return -1;
    const FVector Centre = Entrance + Inward * DepthCm;
    const double Amount = AmountCm, Radius = FMath::Max(double(RadiusCm), 2.0 * Amount);
    auto Smoother = [](double T) { T = FMath::Clamp(T, 0.0, 1.0); return T * T * T * (T * (6.0 * T - 15.0) + 10.0); };
    FMeshDescription* Description = Mesh->GetMeshDescription(0);
    if (!Description) return -1;
    FSkeletalMeshAttributes Attributes(*Description);
    if (!Attributes.GetMorphTargetNames().Contains(MorphName) && !Attributes.RegisterMorphTargetAttribute(MorphName, false)) return -1;
    TVertexAttributesRef<FVector3f> Deltas = Attributes.GetVertexMorphPositionDelta(MorphName);
    const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
    int32 Moved = 0;
    for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
    {
        // The skin in front of the path moves forward by the full amount however deep the path lies under it, fading
        // out across the belly (and a little outward, so the swelling is round); nothing behind the path moves, nor
        // the entrance, the pubic area and the thighs below the floor (fading in over 5 cm above it).
        const FVector Point(Positions[Vertex]);
        const double Above = Smoother((Point.Z - Front.Z - FloorCm) / 5.0);
        const FVector Away = Point - Centre;
        const double Ahead = FVector::DotProduct(Away, Forward);
        const FVector Across = Away - Forward * Ahead;
        const double Spread = Across.Size();
        FVector Delta = FVector::ZeroVector;
        if (Ahead > -2.0 && Ahead < 25.0 && Spread < Radius && Above > 0.0)
        {
            const double InFront = Smoother((Ahead + 2.0) / 4.0);
            const double Fade = 1.0 - Smoother(Spread / Radius);
            const FVector Direction = (Forward + Across.GetSafeNormal() * (0.35 * Spread / Radius)).GetSafeNormal();
            Delta = Direction * (Amount * Fade * InFront * Above);
        }
        Deltas[Vertex] = FVector3f(Delta);
        if (Delta.SizeSquared() > 1.0e-6) ++Moved;
    }
    USkeletalMesh::FCommitMeshDescriptionParams Params;
    Params.bMarkPackageDirty = true;
    if (!Mesh->CommitMeshDescription(0, Params)) return -1;
    Mesh->Build();
    USkinnedAsset* const Built[] = {Mesh};
    FSkinnedAssetCompilingManager::Get().FinishCompilation(Built);
    Mesh->MarkPackageDirty();
    UE_LOG(LogTemp, Display, TEXT("GRATIA_BULGE_MORPH %s depth=%.1fcm moved=%d amount=%.1fcm radius=%.1fcm floor=%.1fcm"), *MorphName.ToString(), DepthCm,
        Moved, Amount, Radius, FloorCm);
    return Moved;
}

int32 UGratiaExperienceToolsLibrary::SubdivideMeshAroundBones(USkeletalMesh* Mesh, const TArray<FName>& Bones, float RadiusCm, FName Marker)
{
    if (!Mesh || Bones.IsEmpty() || RadiusCm <= 0.0f || Marker.IsNone() || !Mesh->HasMeshDescription(0)) return -1;
    FMeshDescription* Description = Mesh->GetMeshDescription(0);
    if (!Description) return -1;
    if (Description->VertexAttributes().HasAttribute(Marker)) return 0;
    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    TArray<FVector> Centres;
    for (const FName Bone : Bones)
    {
        FVector Point;
        if (!GratiaRefCentre(Ref, {Bone}, Point)) return -1;
        Centres.Add(Point);
    }
    FSkeletalMeshAttributes Attributes(*Description);
    TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
    auto Near = [&Centres, RadiusCm](const FVector& Point)
    {
        for (const FVector& Centre : Centres) if (FVector::DistSquared(Point, Centre) < FMath::Square(double(RadiusCm))) return true;
        return false;
    };
    // Every edge of a triangle near a bone is split.
    TSet<FEdgeID> Split;
    for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
    {
        FVector Centre = FVector::ZeroVector;
        for (const FVertexInstanceID Instance : Description->GetTriangleVertexInstances(Triangle))
            Centre += FVector(Positions[Description->GetVertexInstanceVertex(Instance)]) / 3.0;
        if (Near(Centre)) for (const FEdgeID Edge : Description->GetTriangleEdges(Triangle)) Split.Add(Edge);
    }
    if (Split.IsEmpty()) { Description->VertexAttributes().RegisterAttribute<int32>(Marker, 1, 0); return 0; }
    // The triangles to replace are recorded and removed first; new elements are created afterwards (the mesh
    // description's indexers do not take removals mixed with fresh elements).
    struct FOld { FVertexInstanceID I[3]; FVertexID V[3]; FEdgeID E[3]; bool S[3]; FPolygonGroupID Group; };
    TArray<FOld> Replaced;
    for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
    {
        const TArrayView<const FVertexInstanceID> View = Description->GetTriangleVertexInstances(Triangle);
        FOld Old;
        bool bAny = false;
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            Old.I[Corner] = View[Corner];
            Old.V[Corner] = Description->GetVertexInstanceVertex(View[Corner]);
        }
        for (int32 Side = 0; Side < 3; ++Side)
        {
            Old.E[Side] = Description->GetVertexPairEdge(Old.V[Side], Old.V[(Side + 1) % 3]);
            Old.S[Side] = Split.Contains(Old.E[Side]);
            bAny |= Old.S[Side];
        }
        if (!bAny) continue;
        Old.Group = Description->GetTrianglePolygonGroup(Triangle);
        Replaced.Add(Old);
    }
    TArray<FTriangleID> Remove;
    for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
        for (const FEdgeID Edge : Description->GetTriangleEdges(Triangle))
            if (Split.Contains(Edge)) { Remove.Add(Triangle); break; }
    TArray<FEdgeID> Orphans;
    // Imported skeletal meshes carry no UV elements on their triangles (UVs live on the vertex instances); the UV
    // indexer would trip over the empty references, so it stays suspended while triangles are replaced.
    Description->SuspendUVIndexing();
    for (const FTriangleID Triangle : Remove) Description->DeleteTriangle(Triangle, &Orphans);
    // Midpoint vertices: average position and morph deltas, blended skin weights in every profile.
    TArray<FName> Profiles = Attributes.GetSkinWeightProfileNames();
    if (Profiles.IsEmpty()) Profiles.Add(NAME_None);
    const TArray<FName> Morphs = Attributes.GetMorphTargetNames();
    TMap<FEdgeID, FVertexID> Middle;
    for (const FEdgeID Edge : Split)
    {
        const FVertexID A = Description->GetEdgeVertex(Edge, 0), B = Description->GetEdgeVertex(Edge, 1);
        const FVertexID Mid = Description->CreateVertex();
        Positions = Attributes.GetVertexPositions();
        Positions[Mid] = (Positions[A] + Positions[B]) * 0.5f;
        for (const FName Profile : Profiles)
        {
            FSkinWeightsVertexAttributesRef Weights = Attributes.GetVertexSkinWeights(Profile);
            const UE::AnimationCore::FBoneWeights WeightsA = UE::AnimationCore::FBoneWeights::Create(Weights.Get(A));
            const UE::AnimationCore::FBoneWeights WeightsB = UE::AnimationCore::FBoneWeights::Create(Weights.Get(B));
            Weights.Set(Mid, UE::AnimationCore::FBoneWeights::Blend(WeightsA, WeightsB, 0.5f));
        }
        for (const FName Morph : Morphs)
        {
            TVertexAttributesRef<FVector3f> Deltas = Attributes.GetVertexMorphPositionDelta(Morph);
            Deltas[Mid] = (Deltas[A] + Deltas[B]) * 0.5f;
        }
        Middle.Add(Edge, Mid);
    }
    // Midpoint vertex instances, shared by both triangles of an edge unless a seam separates their instances.
    TMap<TPair<FVertexInstanceID, FVertexInstanceID>, FVertexInstanceID> MiddleInstance;
    auto MidInstance = [&](FVertexInstanceID A, FVertexInstanceID B, FVertexID Vertex)
    {
        const TPair<FVertexInstanceID, FVertexInstanceID> Key = A.GetValue() < B.GetValue() ? MakeTuple(A, B) : MakeTuple(B, A);
        if (const FVertexInstanceID* Found = MiddleInstance.Find(Key)) return *Found;
        const FVertexInstanceID Instance = Description->CreateVertexInstance(Vertex);
        TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
        TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
        TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
        TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
        TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
        for (int32 Channel = 0; Channel < UVs.GetNumChannels(); ++Channel) UVs.Set(Instance, Channel, (UVs.Get(A, Channel) + UVs.Get(B, Channel)) * 0.5f);
        if (Normals.IsValid()) Normals[Instance] = (Normals[A] + Normals[B]).GetSafeNormal();
        if (Tangents.IsValid()) Tangents[Instance] = (Tangents[A] + Tangents[B]).GetSafeNormal();
        if (Signs.IsValid()) Signs[Instance] = Signs[A];
        if (Colors.IsValid()) Colors[Instance] = (Colors[A] + Colors[B]) * 0.5f;
        MiddleInstance.Add(Key, Instance);
        return Instance;
    };
    // Retriangulate: three split edges make four triangles, two make three, one makes two (no T-junctions).
    for (const FOld& Old : Replaced)
    {
        const FPolygonGroupID Group = Old.Group;
        auto Make = [Description, Group](FVertexInstanceID A, FVertexInstanceID B, FVertexInstanceID C)
        {
            const FVertexInstanceID Corners[3] = {A, B, C};
            Description->CreateTriangle(Group, Corners);
        };
        FVertexInstanceID M[3];
        for (int32 Side = 0; Side < 3; ++Side)
            M[Side] = Old.S[Side] ? MidInstance(Old.I[Side], Old.I[(Side + 1) % 3], Middle[Old.E[Side]]) : FVertexInstanceID();
        const FVertexInstanceID* I = Old.I;
        const bool* S = Old.S;
        const int32 Count = int32(S[0]) + int32(S[1]) + int32(S[2]);
        if (Count == 3)
        {
            Make(I[0], M[0], M[2]); Make(M[0], I[1], M[1]); Make(M[2], M[1], I[2]); Make(M[0], M[1], M[2]);
        }
        else
        {
            // Rotate so the first split side starts the triangle (winding kept).
            int32 R = 0;
            while (!S[R]) ++R;
            const FVertexInstanceID A = I[R], B = I[(R + 1) % 3], C = I[(R + 2) % 3];
            const FVertexInstanceID AB = M[R], BC = M[(R + 1) % 3], CA = M[(R + 2) % 3];
            if (Count == 1) { Make(A, AB, C); Make(AB, B, C); }
            else if (S[(R + 1) % 3]) { Make(AB, B, BC); Make(A, AB, BC); Make(A, BC, C); }
            else { Make(A, AB, CA); Make(AB, B, C); Make(AB, C, CA); }
        }
    }
    // Split edges left without triangles go (edges reused by the new triangles stay).
    for (const FEdgeID Edge : Orphans)
        if (Description->IsEdgeValid(Edge) && Description->GetNumEdgeConnectedTriangles(Edge) == 0) Description->DeleteEdge(Edge);
    Description->ResumeUVIndexing();
    Description->VertexAttributes().RegisterAttribute<int32>(Marker, 1, 0);
    USkeletalMesh::FCommitMeshDescriptionParams Params;
    Params.bMarkPackageDirty = true;
    if (!Mesh->CommitMeshDescription(0, Params)) return -1;
    Mesh->Build();
    USkinnedAsset* const Built[] = {Mesh};
    FSkinnedAssetCompilingManager::Get().FinishCompilation(Built);
    Mesh->MarkPackageDirty();
    UE_LOG(LogTemp, Display, TEXT("GRATIA_SUBDIVIDE %s radius=%.1fcm split_edges=%d triangles=%d vertices=%d"), *Marker.ToString(), RadiusCm,
        Split.Num(), Description->Triangles().Num(), Description->Vertices().Num());
    return Split.Num();
}
