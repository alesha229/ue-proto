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
