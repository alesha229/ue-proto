#include "GratiaClothPortLibrary.h"

#include "AssetCompilingManager.h"
#include "GratiaSourceClothingAsset.h"

#include "ChaosCloth/ChaosClothConfig.h"
#include "ClothingAsset.h"
#include "ClothingAssetFactory.h"
#include "ClothLODData.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "Utils/ClothingMeshUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaClothPort, Log, All);

namespace GratiaClothPort
{
constexpr TCHAR OwnedPrefix[] = TEXT("GratiaSourceCloth_");
constexpr float SampleToleranceCm = 0.2f;
// SurfaceDeform consumers lie on or just above their cages. Far bindings (for
// example a coincident head/neck seam sample) would swing with a rotating cage
// triangle, so their cloth weight fades out smoothly instead of tearing.
constexpr float BindFullDistanceCm = 1.0f;
constexpr float BindZeroDistanceCm = 3.0f;

bool ReadVector(const TSharedPtr<FJsonValue>& Value, FVector& Out)
{
    if (!Value.IsValid() || Value->Type != EJson::Array) return false;
    const auto& Values = Value->AsArray();
    if (Values.Num() != 3) return false;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        double Component;
        if (!Values[Axis]->TryGetNumber(Component) || !FMath::IsFinite(Component)) return false;
        Out[Axis] = Component;
    }
    return true;
}

double Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Default)
{
    double Value;
    return Object.IsValid() && Object->TryGetNumberField(Key, Value) && FMath::IsFinite(Value) ? Value : Default;
}

bool Flag(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, bool Default)
{
    bool Value;
    return Object.IsValid() && Object->TryGetBoolField(Key, Value) ? Value : Default;
}

struct FSourceTransform
{
    int32 Axes[3] = {0, 1, 2};
    double Signs[3] = {1, 1, 1};
    double Scale = 100.0;
    FVector Offset = FVector::ZeroVector;
    double MaximumError = TNumericLimits<double>::Max();
    FVector Orient(const FVector& P) const { return FVector(P[Axes[0]] * Signs[0], P[Axes[1]] * Signs[1], P[Axes[2]] * Signs[2]); }
    FVector Position(const FVector& P) const { return Orient(P) * Scale + Offset; }
    bool FlipsWinding() const
    {
        const int32 Inversions = (Axes[0] > Axes[1]) + (Axes[0] > Axes[2]) + (Axes[1] > Axes[2]);
        return (Inversions % 2 ? -1 : 1) * Signs[0] * Signs[1] * Signs[2] < 0;
    }
};

bool Calibrate(const USkeletalMesh* Mesh, const TSharedPtr<FJsonObject>& Json, FSourceTransform& Out)
{
    const TSharedPtr<FJsonObject>* Heads;
    if (!Json->TryGetObjectField(TEXT("bone_heads"), Heads)) return false;
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    TArray<FTransform> Rest = Skeleton.GetRefBonePose();
    for (int32 I = 0; I < Rest.Num(); ++I)
    {
        const int32 Parent = Skeleton.GetParentIndex(I);
        if (Parent != INDEX_NONE) Rest[I] *= Rest[Parent];
    }
    TArray<FVector> Source, Target;
    for (const auto& Pair : (*Heads)->Values)
    {
        const int32 Bone = Skeleton.FindBoneIndex(FName(*Pair.Key));
        FVector Position;
        if (Bone != INDEX_NONE && ReadVector(Pair.Value, Position))
        {
            Source.Add(Position);
            Target.Add(Rest[Bone].GetLocation());
        }
    }
    if (Source.Num() < 8) return false;
    FVector SourceMean = FVector::ZeroVector, TargetMean = FVector::ZeroVector;
    for (int32 I = 0; I < Source.Num(); ++I) { SourceMean += Source[I]; TargetMean += Target[I]; }
    SourceMean /= Source.Num(); TargetMean /= Target.Num();
    const int32 Permutations[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    double BestError = TNumericLimits<double>::Max();
    for (const auto& Permutation : Permutations)
    {
        for (int32 Bits = 0; Bits < 8; ++Bits)
        {
            FSourceTransform Candidate;
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                Candidate.Axes[Axis] = Permutation[Axis];
                Candidate.Signs[Axis] = Bits & (1 << Axis) ? -1 : 1;
            }
            double Numerator = 0, Denominator = 0;
            for (int32 I = 0; I < Source.Num(); ++I)
            {
                const FVector Centred = Candidate.Orient(Source[I] - SourceMean);
                Numerator += FVector::DotProduct(Centred, Target[I] - TargetMean);
                Denominator += Centred.SizeSquared();
            }
            if (Denominator <= UE_SMALL_NUMBER) continue;
            Candidate.Scale = Numerator / Denominator;
            if (Candidate.Scale < 90 || Candidate.Scale > 110) continue;
            Candidate.Offset = TargetMean - Candidate.Orient(SourceMean) * Candidate.Scale;
            double Error = 0;
            Candidate.MaximumError = 0;
            for (int32 I = 0; I < Source.Num(); ++I)
            {
                const double Distance = FVector::Distance(Candidate.Position(Source[I]), Target[I]);
                Error += Distance * Distance;
                Candidate.MaximumError = FMath::Max(Candidate.MaximumError, Distance);
            }
            if (Error < BestError) { Out = Candidate; BestError = Error; }
        }
    }
    UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_FRAME bones=%d scale=%.8f max_error_cm=%.8f axes=%d%d%d signs=%.0f,%.0f,%.0f"),
        Source.Num(), Out.Scale, Out.MaximumError, Out.Axes[0], Out.Axes[1], Out.Axes[2], Out.Signs[0], Out.Signs[1], Out.Signs[2]);
    return Out.MaximumError <= 0.05;
}

FString MaterialKey(FString Name)
{
    Name.ToLowerInline();
    Name.ReplaceInline(TEXT(" "), TEXT("_"));
    Name.ReplaceInline(TEXT("."), TEXT("_"));
    return Name;
}

struct FInfluenceSample
{
    FVector3f Position;
    float Weight = 0;
    int32 Cage = 0;
    TSet<FString> Materials;
};

FIntVector Cell(const FVector3f& P)
{
    return FIntVector(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y), FMath::FloorToInt(P.Z));
}

struct FCage
{
    FString Name;
    int32 FirstTriangle = 0;
    int32 NumTriangles = 0;
    float Pressure = 0;
};

struct FPortData
{
    FClothPhysicalMeshData Physical;
    TArray<FName> Bones;
    TArray<FCage> Cages;
    TArray<FInfluenceSample> Samples;
    TMap<FIntVector, TArray<int32>> SampleCells;
    TArray<float> PressureMask;
    int32 FixedCount = 0;
};

bool ReadCages(const TSharedPtr<FJsonObject>& Json, const USkeletalMesh* Mesh, const FSourceTransform& Transform, FPortData& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values;
    if (!Json->TryGetArrayField(TEXT("cages"), Values)) return false;
    TArray<float> MaxDistance, AnimDrive;
    TArray<FVector3f> ExpectedNormals;
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    for (const auto& CageValue : *Values)
    {
        const auto CageJson = CageValue->AsObject();
        if (!CageJson.IsValid() || !Flag(CageJson, TEXT("enabled_in_source_render"), true)) continue;
        FCage Cage;
        if (!CageJson->TryGetStringField(TEXT("name"), Cage.Name)) return false;
        const TArray<TSharedPtr<FJsonValue>> *Vertices, *Normals, *Triangles, *Pin, *Weights, *Consumers;
        if (!CageJson->TryGetArrayField(TEXT("vertices"), Vertices) || !CageJson->TryGetArrayField(TEXT("triangles"), Triangles) ||
            !CageJson->TryGetArrayField(TEXT("normals"), Normals) || Normals->Num() != Vertices->Num() ||
            !CageJson->TryGetArrayField(TEXT("pin"), Pin) || !CageJson->TryGetArrayField(TEXT("weights"), Weights) ||
            !CageJson->TryGetArrayField(TEXT("consumers"), Consumers) || Vertices->Num() != Pin->Num() || Vertices->Num() != Weights->Num()) return false;
        const TSharedPtr<FJsonObject>* SettingsPtr;
        TSharedPtr<FJsonObject> Settings;
        if (CageJson->TryGetObjectField(TEXT("settings"), SettingsPtr)) Settings = *SettingsPtr;
        Cage.Pressure = Flag(Settings, TEXT("use_pressure"), false) ? FMath::Clamp((float)Number(Settings, TEXT("uniform_pressure_force"), 0), 0.f, 1.f) : 0.f;
        const int32 CageIndex = Out.Cages.Num();
        const int32 Base = Out.Physical.Vertices.Num();
        Cage.FirstTriangle = Out.Physical.Indices.Num() / 3;
        for (int32 I = 0; I < Vertices->Num(); ++I)
        {
            FVector Position;
            if (!ReadVector((*Vertices)[I], Position)) return false;
            Out.Physical.Vertices.Add(FVector3f(Transform.Position(Position)));
            FVector Normal;
            if (!ReadVector((*Normals)[I], Normal)) return false;
            ExpectedNormals.Add(FVector3f(Transform.Orient(Normal).GetSafeNormal()));
            const float P = FMath::Clamp((float)(*Pin)[I]->AsNumber(), 0.f, 1.f);
            const bool Fixed = P >= 0.99f;
            // Source pin weights remain continuous. Only full pins become kinematic.
            // Native Chaos treats distances below 0.1cm as kinematic. Keep a
            // 1.01mm floor for partial source pins so they stay dynamic.
            MaxDistance.Add(Fixed ? 0.f : FMath::Max(0.101f, 3.f * FMath::Square(1.f - P)));
            AnimDrive.Add(FMath::Max(P, 0.05f));
            Out.PressureMask.Add(Cage.Pressure);
            Out.FixedCount += Fixed;
            FClothVertBoneData BoneData;
            float Sum = 0;
            const auto& VertexWeights = (*Weights)[I]->AsArray();
            for (const auto& WeightValue : VertexWeights)
            {
                const auto Weight = WeightValue->AsObject();
                FString Bone;
                if (!Weight.IsValid() || !Weight->TryGetStringField(TEXT("bone"), Bone)) return false;
                const float Amount = (float)Number(Weight, TEXT("weight"), 0);
                if (Amount <= 0) continue;
                if (Skeleton.FindBoneIndex(FName(*Bone)) == INDEX_NONE || BoneData.NumInfluences >= FClothVertBoneData::MaxTotalInfluences) return false;
                BoneData.BoneIndices[BoneData.NumInfluences] = Out.Bones.AddUnique(FName(*Bone));
                BoneData.BoneWeights[BoneData.NumInfluences++] = Amount;
                Sum += Amount;
            }
            if (Sum <= UE_SMALL_NUMBER) return false;
            for (int32 W = 0; W < BoneData.NumInfluences; ++W) BoneData.BoneWeights[W] /= Sum;
            Out.Physical.BoneData.Add(BoneData);
        }
        for (const auto& TriangleValue : *Triangles)
        {
            const auto& Triangle = TriangleValue->AsArray();
            if (Triangle.Num() != 3) return false;
            uint32 Indices[3];
            for (int32 I = 0; I < 3; ++I)
            {
                const int32 Index = (int32)Triangle[I]->AsNumber();
                if (Index < 0 || Index >= Vertices->Num()) return false;
                Indices[I] = Base + Index;
            }
            if (Transform.FlipsWinding()) Swap(Indices[1], Indices[2]);
            if (FVector3f::CrossProduct(Out.Physical.Vertices[Indices[1]] - Out.Physical.Vertices[Indices[0]],
                Out.Physical.Vertices[Indices[2]] - Out.Physical.Vertices[Indices[0]]).SizeSquared() <= UE_SMALL_NUMBER) return false;
            Out.Physical.Indices.Append(Indices, 3);
        }
        Cage.NumTriangles = Out.Physical.Indices.Num() / 3 - Cage.FirstTriangle;
        if (!Cage.NumTriangles) return false;
        Out.Cages.Add(Cage);
        for (const auto& ConsumerValue : *Consumers)
        {
            const auto Consumer = ConsumerValue->AsObject();
            const TArray<TSharedPtr<FJsonValue>> *Materials, *Samples;
            if (!Consumer.IsValid() || !Consumer->TryGetArrayField(TEXT("materials"), Materials) ||
                !Consumer->TryGetArrayField(TEXT("influenced_vertices"), Samples)) return false;
            TSet<FString> MaterialNames;
            for (const auto& Material : *Materials) MaterialNames.Add(MaterialKey(Material->AsString()));
            for (const auto& SampleValue : *Samples)
            {
                const auto SampleJson = SampleValue->AsObject();
                FVector Position;
                const TSharedPtr<FJsonValue>* PositionValue = SampleJson.IsValid() ? SampleJson->Values.Find(TEXT("position")) : nullptr;
                if (!PositionValue || !ReadVector(*PositionValue, Position)) return false;
                FInfluenceSample Sample;
                Sample.Position = FVector3f(Transform.Position(Position));
                Sample.Weight = FMath::Clamp((float)Number(SampleJson, TEXT("weight"), 0), 0.f, 1.f);
                Sample.Cage = CageIndex;
                Sample.Materials = MaterialNames;
                Out.SampleCells.FindOrAdd(Cell(Sample.Position)).Add(Out.Samples.Num());
                Out.Samples.Add(MoveTemp(Sample));
            }
        }
        UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_CAGE name=%s vertices=%d triangles=%d pressure_mask=%.3f"), *Cage.Name, Vertices->Num(), Cage.NumTriangles, Cage.Pressure);
    }
    if (Out.Physical.Vertices.Num() < 3 || Out.Physical.Vertices.Num() >= 65535 || Out.FixedCount == 0) return false;
    Out.Physical.GetWeightMap(EWeightMapTargetCommon::MaxDistance).Values = MoveTemp(MaxDistance);
    Out.Physical.GetWeightMap(EWeightMapTargetCommon::AnimDriveStiffness).Values = MoveTemp(AnimDrive);
    // Chaos's cloth descriptor uses clockwise normals; calibrate winding against
    // the exported source normals instead of assuming FBX handedness.
    const ClothingMeshUtils::ClothMeshDesc InitialDesc(Out.Physical.Vertices, Out.Physical.Indices);
    double Agreement = 0;
    for (int32 I = 0; I < ExpectedNormals.Num(); ++I) Agreement += FVector3f::DotProduct(InitialDesc.GetNormals()[I], ExpectedNormals[I]);
    if (Agreement < 0)
        for (int32 I = 0; I < Out.Physical.Indices.Num(); I += 3) Swap(Out.Physical.Indices[I + 1], Out.Physical.Indices[I + 2]);
    const ClothingMeshUtils::ClothMeshDesc SourceDesc(Out.Physical.Vertices, Out.Physical.Indices);
    Out.Physical.Normals.Append(SourceDesc.GetNormals().GetData(), SourceDesc.GetNormals().Num());
    return true;
}

void FindInfluence(const FVector3f& Position, const TSet<FString>& Materials, const FPortData& Data, TMap<int32, float>& Out)
{
    const FIntVector Centre = Cell(Position);
    TMap<int32, float> ClosestDistance;
    for (int32 X = -1; X <= 1; ++X) for (int32 Y = -1; Y <= 1; ++Y) for (int32 Z = -1; Z <= 1; ++Z)
    {
        const auto* Bucket = Data.SampleCells.Find(Centre + FIntVector(X,Y,Z));
        if (!Bucket) continue;
        for (const int32 SampleIndex : *Bucket)
        {
            const auto& Sample = Data.Samples[SampleIndex];
            bool MaterialMatch = false;
            for (const auto& Material : Materials) if (Sample.Materials.Contains(Material)) { MaterialMatch = true; break; }
            if (!MaterialMatch) continue;
            const float Distance = FVector3f::DistSquared(Position, Sample.Position);
            if (Distance > FMath::Square(SampleToleranceCm)) continue;
            const float* Previous = ClosestDistance.Find(Sample.Cage);
            if (!Previous || Distance < *Previous - UE_SMALL_NUMBER)
            {
                ClosestDistance.Add(Sample.Cage, Distance);
                Out.Add(Sample.Cage, Sample.Weight);
            }
            else if (FMath::IsNearlyEqual(Distance, *Previous, UE_SMALL_NUMBER)) Out[Sample.Cage] = FMath::Max(Out[Sample.Cage], Sample.Weight);
        }
    }
}

struct FSectionMapping
{
    int32 Section = INDEX_NONE;
    TArray<FMeshToMeshVertData> Mapping;
    int32 DynamicVertices = 0;
    int32 DistanceFaded = 0;
    float MaxRestError = 0;
};

struct FShaderTriangle
{
    FVector P[3];
    FVector N[3];

    FVector Evaluate(const FVector& Parameters) const
    {
        return (P[0] + N[0] * Parameters.Z) * Parameters.X
            + (P[1] + N[1] * Parameters.Z) * Parameters.Y
            + (P[2] + N[2] * Parameters.Z) * (1.0 - Parameters.X - Parameters.Y);
    }
};

bool SolveShaderCoordinates(const FShaderTriangle& Triangle, const FVector& Point, const FVector4f& Initial, FVector4f& Result)
{
    // Stock binding falls back to planar signed distance if its cubic solve
    // fails. The render shader still interpolates the three vertex normals,
    // so that fallback need not preserve a point on a curved source cage.
    const FVector EdgeA = Triangle.P[0] - Triangle.P[2];
    const FVector EdgeB = Triangle.P[1] - Triangle.P[2];
    const FVector FaceNormal = FVector::CrossProduct(EdgeA, EdgeB).GetSafeNormal();
    const FVector Projected = Point - FaceNormal * FVector::DotProduct(Point - Triangle.P[2], FaceNormal);
    const FVector PlanarBary = FMath::ComputeBaryCentric2D(Projected, Triangle.P[0], Triangle.P[1], Triangle.P[2]);
    const FVector InterpolatedNormal = Triangle.N[0] * PlanarBary.X + Triangle.N[1] * PlanarBary.Y + Triangle.N[2] * PlanarBary.Z;
    const double NormalProjection = FVector::DotProduct(InterpolatedNormal, FaceNormal);
    const double PlanarDistance = FVector::DotProduct(Point - Triangle.P[2], FaceNormal);
    const double Distance = FMath::Abs(NormalProjection) > 1.e-8 ? PlanarDistance / NormalProjection : PlanarDistance;
    const FVector Seeds[] = {
        FVector(Initial.X, Initial.Y, Initial.W),
        FVector(PlanarBary.X, PlanarBary.Y, Distance),
        FVector(PlanarBary.X, PlanarBary.Y, 0),
        FVector(PlanarBary.X, PlanarBary.Y, -Distance),
        FVector(1.0 / 3.0, 1.0 / 3.0, Distance)
    };
    for (FVector Parameters : Seeds)
    {
        for (int32 Iteration = 0; Iteration < 32; ++Iteration)
        {
            const FVector Residual = Triangle.Evaluate(Parameters) - Point;
            if (Residual.SizeSquared() < 1.e-10) break;
            const FVector JU = EdgeA + (Triangle.N[0] - Triangle.N[2]) * Parameters.Z;
            const FVector JV = EdgeB + (Triangle.N[1] - Triangle.N[2]) * Parameters.Z;
            const FVector JW = Triangle.N[0] * Parameters.X + Triangle.N[1] * Parameters.Y
                + Triangle.N[2] * (1.0 - Parameters.X - Parameters.Y);
            const double Determinant = FVector::DotProduct(JU, FVector::CrossProduct(JV, JW));
            if (!FMath::IsFinite(Determinant) || FMath::Abs(Determinant) < 1.e-12) break;
            const FVector Step(
                FVector::DotProduct(Residual, FVector::CrossProduct(JV, JW)) / Determinant,
                FVector::DotProduct(JU, FVector::CrossProduct(Residual, JW)) / Determinant,
                FVector::DotProduct(JU, FVector::CrossProduct(JV, Residual)) / Determinant);
            if (Step.ContainsNaN()) break;
            bool Improved = false;
            double Fraction = 1;
            for (int32 Search = 0; Search < 12; ++Search, Fraction *= 0.5)
            {
                const FVector Candidate = Parameters - Step * Fraction;
                if (!Candidate.ContainsNaN() && (Triangle.Evaluate(Candidate) - Point).SizeSquared() < Residual.SizeSquared())
                {
                    Parameters = Candidate;
                    Improved = true;
                    break;
                }
            }
            if (!Improved) break;
        }
        if (Parameters.ContainsNaN() || FMath::Abs(Parameters.Z) > 50 ||
            FMath::Abs(Parameters.X) > 16 || FMath::Abs(Parameters.Y) > 16 || FMath::Abs(1.0 - Parameters.X - Parameters.Y) > 16) continue;
        const FVector4f Candidate((float)Parameters.X, (float)Parameters.Y,
            1.f - (float)Parameters.X - (float)Parameters.Y, (float)Parameters.Z);
        // Validate stored float coefficients, not only the double Newton state.
        if (FVector::Distance(Triangle.Evaluate(FVector(Candidate.X, Candidate.Y, Candidate.W)), Point) <= 0.0001)
        {
            Result = Candidate;
            return true;
        }
    }
    return false;
}

bool RepairMappingCoordinates(FMeshToMeshVertData& Mapping, const FVector3f& Position, const FVector3f& Normal, const FVector3f& Tangent,
    const ClothingMeshUtils::ClothMeshDesc& Source, const TArray<ClothingMeshUtils::FMeshToMeshFilterSet>& Filters, const int32 TargetIndex)
{
    auto TryTriangle = [&](FMeshToMeshVertData& Candidate)
    {
        FShaderTriangle Triangle;
        for (int32 I = 0; I < 3; ++I)
        {
            const uint16 Vertex = Candidate.SourceMeshVertIndices[I];
            if (!Source.GetPositions().IsValidIndex(Vertex)) return false;
            Triangle.P[I] = FVector(Source.GetPositions()[Vertex]);
            Triangle.N[I] = -FVector(Source.GetNormals()[Vertex]);
        }
        FVector4f PositionCoordinates, NormalCoordinates, TangentCoordinates;
        if (!SolveShaderCoordinates(Triangle, FVector(Position), Candidate.PositionBaryCoordsAndDist, PositionCoordinates) ||
            !SolveShaderCoordinates(Triangle, FVector(Position + Normal), Candidate.NormalBaryCoordsAndDist, NormalCoordinates) ||
            !SolveShaderCoordinates(Triangle, FVector(Position + Tangent), Candidate.TangentBaryCoordsAndDist, TangentCoordinates)) return false;
        Candidate.PositionBaryCoordsAndDist = PositionCoordinates;
        Candidate.NormalBaryCoordsAndDist = NormalCoordinates;
        Candidate.TangentBaryCoordsAndDist = TangentCoordinates;
        return true;
    };
    auto Candidate = Mapping;
    if (TryTriangle(Candidate)) { Mapping = Candidate; return true; }
    // An averaged-normal prism can fold or fail to contain the render point.
    // Try the closest other triangles from the same source SurfaceDeform masks.
    TSet<int32> AllowedTriangles;
    for (const auto& Filter : Filters)
        if (Filter.TargetVertices.Contains(TargetIndex)) AllowedTriangles.Append(Filter.SourceTriangles);
    TArray<TPair<float, int32>> Nearby;
    for (const int32 Triangle : AllowedTriangles)
        Nearby.Emplace(Source.DistanceToTriangle(FVector(Position), Triangle * 3), Triangle);
    Nearby.Sort([](const auto& A, const auto& B) { return A.Key < B.Key; });
    for (int32 I = 0; I < FMath::Min(32, Nearby.Num()); ++I)
    {
        Candidate = Mapping;
        const int32 Base = Nearby[I].Value * 3;
        for (int32 V = 0; V < 3; ++V) Candidate.SourceMeshVertIndices[V] = Source.GetIndices()[Base + V];
        if (TryTriangle(Candidate)) { Mapping = Candidate; return true; }
    }
    return false;
}

bool BuildMappings(USkeletalMesh* Mesh, const FPortData& Data, const TArray<FName>& ExcludedBoneRoots, TArray<FSectionMapping>& Out)
{
    const auto& Lod = Mesh->GetImportedModel()->LODModels[0];
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    TArray<int32> ExcludedRoots;
    for (const FName Bone : ExcludedBoneRoots)
    {
        const int32 Index = Skeleton.FindBoneIndex(Bone);
        if (Index == INDEX_NONE) { UE_LOG(LogGratiaClothPort, Error, TEXT("SOURCE_CLOTH_EXCLUDED_ROOT_MISSING bone=%s"), *Bone.ToString()); return false; }
        ExcludedRoots.Add(Index);
    }
    const ClothingMeshUtils::ClothMeshDesc Source(Data.Physical.Vertices, Data.Physical.Indices);
    const auto* MaxDistances = Data.Physical.FindWeightMap(EWeightMapTargetCommon::MaxDistance);
    for (int32 SectionIndex = 0; SectionIndex < Lod.Sections.Num(); ++SectionIndex)
    {
        const auto& Section = Lod.Sections[SectionIndex];
        if (!Mesh->GetMaterials().IsValidIndex(Section.MaterialIndex)) return false;
        const auto& Material = Mesh->GetMaterials()[Section.MaterialIndex];
        TSet<FString> MaterialNames = {MaterialKey(Material.ImportedMaterialSlotName.ToString()), MaterialKey(Material.MaterialSlotName.ToString())};
        TArray<FVector3f> Positions, Normals, Tangents;
        TArray<int32> RenderIndices;
        TArray<float> BlendWeights;
        TArray<ClothingMeshUtils::FMeshToMeshFilterSet> Filters;
        Filters.SetNum(Data.Cages.Num());
        for (int32 Cage = 0; Cage < Data.Cages.Num(); ++Cage)
            for (int32 T = 0; T < Data.Cages[Cage].NumTriangles; ++T) Filters[Cage].SourceTriangles.Add(Data.Cages[Cage].FirstTriangle + T);
        int32 ExcludedVertices = 0;
        for (int32 I = 0; I < Section.SoftVertices.Num(); ++I)
        {
            const auto& Vertex = Section.SoftVertices[I];
            int32 Dominant = INDEX_NONE;
            uint16 BestWeight = 0;
            for (int32 Influence = 0; Influence < MAX_TOTAL_INFLUENCES; ++Influence)
                if (Vertex.InfluenceWeights[Influence] > BestWeight && Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[Influence]))
                { BestWeight = Vertex.InfluenceWeights[Influence]; Dominant = Section.BoneMap[Vertex.InfluenceBones[Influence]]; }
            bool bExcluded = false;
            for (int32 Bone = Dominant; Bone != INDEX_NONE && !bExcluded; Bone = Skeleton.GetParentIndex(Bone)) bExcluded = ExcludedRoots.Contains(Bone);
            TMap<int32, float> Influence;
            FindInfluence(Vertex.Position, MaterialNames, Data, Influence);
            float Blend = 0;
            for (const auto& Pair : Influence) if (Pair.Value > 0.001f)
            {
                Filters[Pair.Key].TargetVertices.Add(Positions.Num());
                Blend = FMath::Max(Blend, Pair.Value);
            }
            if (Blend <= 0.001f) continue;
            if (bExcluded)
            {
                for (auto& Filter : Filters) Filter.TargetVertices.Remove(Positions.Num());
                ++ExcludedVertices; continue;
            }
            Positions.Add(Vertex.Position); Normals.Add(Vertex.TangentZ); Tangents.Add(Vertex.TangentX);
            RenderIndices.Add(I); BlendWeights.Add(Blend);
        }
        if (ExcludedVertices) UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_EXCLUDED section=%d vertices=%d reason=dominant bone under excluded root"), SectionIndex, ExcludedVertices);
        if (Positions.IsEmpty()) continue;
        if (Section.HasClothingData() && !Section.ClothingData.AssetGuid.IsValid()) return false;
        TArray<uint32> EmptyTriangles;
        const ClothingMeshUtils::ClothMeshDesc Target(Positions, Normals, Tangents, EmptyTriangles);
        TArray<FMeshToMeshVertData> ActiveMapping;
        ClothingMeshUtils::GenerateMeshToMeshVertData(ActiveMapping, Target, Source, MaxDistances, true, false, 20.f, Filters);
        if (ActiveMapping.Num() != Positions.Num()) return false;
        for (int32 I = 0; I < ActiveMapping.Num(); ++I)
        {
            if (ActiveMapping[I].SourceMeshVertIndices[3] == 0xffff) continue;
            if (!RepairMappingCoordinates(ActiveMapping[I], Positions[I], Normals[I], Tangents[I], Source, Filters, I))
            {
                UE_LOG(LogGratiaClothPort, Error, TEXT("SOURCE_CLOTH_COORDINATES_REJECTED section=%d vertex=%d"), SectionIndex, RenderIndices[I]);
                return false;
            }
        }
        // A repaired mapping can select a neighboring source triangle.
        ClothingMeshUtils::ComputeVertexContributions(ActiveMapping, MaxDistances, true, false);
        FSectionMapping Result;
        Result.Section = SectionIndex;
        Result.Mapping.SetNum(Section.SoftVertices.Num());
        for (auto& Mapping : Result.Mapping)
        {
            FMemory::Memzero(Mapping);
            Mapping.SourceMeshVertIndices[3] = 0xffff;
        }
        for (int32 I = 0; I < ActiveMapping.Num(); ++I)
        {
            auto Mapping = ActiveMapping[I];
            const float BindDistance = FMath::Abs(Mapping.PositionBaryCoordsAndDist.W);
            const float DistanceWeight = 1.f - FMath::SmoothStep(BindFullDistanceCm, BindZeroDistanceCm, BindDistance);
            Result.DistanceFaded += DistanceWeight < 0.999f;
            const float SimWeight = (1.f - Mapping.SourceMeshVertIndices[3] / 65535.f) * BlendWeights[I] * DistanceWeight;
            Mapping.SourceMeshVertIndices[3] = (uint16)FMath::RoundToInt((1.f - SimWeight) * 65535.f);
            Result.DynamicVertices += SimWeight > 0.001f;
            if (SimWeight > 0.001f)
            {
                const auto& B = Mapping.PositionBaryCoordsAndDist;
                const float Barycentric[3] = {B.X, B.Y, 1.f - B.X - B.Y};
                FVector3f Reconstructed = FVector3f::ZeroVector;
                for (int32 V = 0; V < 3; ++V)
                {
                    const uint16 Index = Mapping.SourceMeshVertIndices[V];
                    if (!Data.Physical.Vertices.IsValidIndex(Index)) return false;
                    // AppendSimulationData flips cloth normals for the render
                    // shader; reproduce that convention in the rest-pose check.
                    Reconstructed += (Data.Physical.Vertices[Index] - Source.GetNormals()[Index] * B.W) * Barycentric[V];
                }
                Result.MaxRestError = FMath::Max(Result.MaxRestError, FVector3f::Distance(Reconstructed, Positions[I]));
            }
            Result.Mapping[RenderIndices[I]] = Mapping;
        }
        if (Result.MaxRestError > 0.05f)
        {
            UE_LOG(LogGratiaClothPort, Error, TEXT("SOURCE_CLOTH_MAPPING_REJECTED section=%d rest_error_cm=%.6f"), SectionIndex, Result.MaxRestError);
            return false;
        }
        if (Result.DynamicVertices) Out.Add(MoveTemp(Result));
    }
    return !Out.IsEmpty();
}

void AddMask(FClothLODDataCommon& Lod, const uint32 Target, const TCHAR* Name, const TArray<float>& Values)
{
    auto& Map = Lod.PhysicalMeshData.AddWeightMap(Target);
    Map.Values = Values;
    auto& EditorMask = Lod.PointWeightMaps.AddDefaulted_GetRef();
    EditorMask.Initialize(Map, Target);
    EditorMask.Name = FName(Name);
}

bool CoreColliderTouchesFreeCage(const USkeletalBodySetup* Body, const USkeletalMesh* Mesh, const FClothPhysicalMeshData& Physical)
{
    const auto& Skeleton = Mesh->GetRefSkeleton();
    const int32 BoneIndex = Skeleton.FindBoneIndex(Body->BoneName);
    if (BoneIndex == INDEX_NONE) return true;
    FTransform BoneTransform = Skeleton.GetRefBonePose()[BoneIndex];
    for (int32 Parent = Skeleton.GetParentIndex(BoneIndex); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
        BoneTransform *= Skeleton.GetRefBonePose()[Parent];
    const double Scale = BoneTransform.GetScale3D().GetAbsMax();
    const auto& MaxDistance = Physical.GetWeightMap(EWeightMapTargetCommon::MaxDistance).Values;
    for (int32 Vertex = 0; Vertex < Physical.Vertices.Num(); ++Vertex)
    {
        if (MaxDistance[Vertex] <= 0.001f) continue;
        const FVector Point(Physical.Vertices[Vertex]);
        for (const auto& Sphere : Body->AggGeom.SphereElems)
            if (FVector::Distance(Point, BoneTransform.TransformPosition(Sphere.Center)) < Sphere.Radius * Scale + 0.15) return true;
        for (const auto& Capsule : Body->AggGeom.SphylElems)
        {
            const FVector Axis = Capsule.Rotation.RotateVector(FVector(0, 0, Capsule.Length * 0.5));
            const FVector A = BoneTransform.TransformPosition(Capsule.Center - Axis);
            const FVector B = BoneTransform.TransformPosition(Capsule.Center + Axis);
            if (FMath::PointDistToSegment(Point, A, B) < Capsule.Radius * Scale + 0.15) return true;
        }
        // Convexes and boxes are not included by the legacy skeletal-mesh cloth collision extraction.
    }
    return false;
}
}

bool UGratiaClothPortLibrary::BuildSourceClothCages(USkeletalMesh* SkeletalMesh, const FString& JSONPath, const TArray<FName>& ExcludedBoneRoots)
{
    using namespace GratiaClothPort;
    if (!SkeletalMesh || !SkeletalMesh->GetImportedModel() || SkeletalMesh->GetImportedModel()->LODModels.IsEmpty()) return false;
    FString Text;
    TSharedPtr<FJsonObject> Json;
    if (!FFileHelper::LoadFileToString(Text, *JSONPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json)) return false;
    FSourceTransform Transform;
    FPortData Data;
    TArray<FSectionMapping> Mappings;
    if (!Calibrate(SkeletalMesh, Json, Transform) || !ReadCages(Json, SkeletalMesh, Transform, Data) || !BuildMappings(SkeletalMesh, Data, ExcludedBoneRoots, Mappings))
    {
        UE_LOG(LogGratiaClothPort, Error, TEXT("SOURCE_CLOTH_BUILD_REJECTED validation failed; mesh not modified"));
        return false;
    }
    const UEnum* Targets = FindObject<UEnum>(nullptr, TEXT("/Script/ChaosCloth.EChaosWeightMapTarget"));
    const int64 PressureTarget = Targets ? Targets->GetValueByNameString(TEXT("Pressure")) : INDEX_NONE;
    if (PressureTarget == INDEX_NONE) return false;
    // Never replace clothing authored by another tool or artist.
    for (const auto& Mapping : Mappings)
    {
        const auto& Section = SkeletalMesh->GetImportedModel()->LODModels[0].Sections[Mapping.Section];
        if (Section.HasClothingData())
        {
            const auto& Assets = SkeletalMesh->GetMeshClothingAssets();
            if (!Assets.IsValidIndex(Section.CorrespondClothAssetIndex) || !Assets[Section.CorrespondClothAssetIndex]->GetName().StartsWith(OwnedPrefix)) return false;
        }
    }
    UGratiaSourceClothingAsset* Asset = nullptr;
    int32 CoreColliderCount = 0, SimulationVertexCount = 0;
    {
    // The scope's end rebuilds the mesh; the engine then unbinds and rebinds cloth
    // through UGratiaSourceClothingAsset, which restores the stored mappings.
    FScopedSkeletalMeshPostEditChange ScopedChange(SkeletalMesh);
    SkeletalMesh->Modify();
    auto& Assets = SkeletalMesh->GetMeshClothingAssets();
    for (int32 I = Assets.Num() - 1; I >= 0; --I)
    {
        if (Assets[I] && Assets[I]->GetName().StartsWith(OwnedPrefix))
        {
            const FGuid OldGuid = Assets[I]->GetAssetGuid();
            Assets[I]->UnbindFromSkeletalMesh(SkeletalMesh, INDEX_NONE, INDEX_NONE);
            for (auto& MeshLod : SkeletalMesh->GetImportedModel()->LODModels)
                for (auto& Pair : MeshLod.UserSectionsData)
                    if (Pair.Value.ClothingData.AssetGuid == OldGuid)
                    {
                        Pair.Value.CorrespondClothAssetIndex = INDEX_NONE;
                        Pair.Value.ClothingData.AssetGuid.Invalidate();
                        Pair.Value.ClothingData.AssetLodIndex = INDEX_NONE;
                    }
            Assets.RemoveAt(I);
        }
    }
    const FName ClothName(TEXT("GratiaSourceCloth_BodyCages"));
    if (UObject* Previous = StaticFindObject(UClothingAssetBase::StaticClass(), SkeletalMesh, *ClothName.ToString()))
    {
        Asset = Cast<UGratiaSourceClothingAsset>(Previous);
        // An earlier port used the plain engine class; retire it so the name is free.
        if (!Asset) Previous->Rename(*MakeUniqueObjectName(GetTransientPackage(), Previous->GetClass(), TEXT("RetiredGratiaSourceCloth")).ToString(),
            GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
    }
    if (!Asset)
    {
        // The engine factory owns assigning the protected identity GUID. Do not
        // extract a render section: its welded triangles can be degenerate and
        // its geometry is replaced by the source cages anyway. Duplicate an empty
        // transient template instead; the factory assigns the GUID.
        auto* Factory = NewObject<UClothingAssetFactory>();
        // The duplicate takes the template's name, so give the template a private outer.
        UPackage* TemplateOuter = CreatePackage(*FString::Printf(TEXT("/Temp/GratiaClothTemplate_%s"), *FGuid::NewGuid().ToString()));
        // No RF_Transient: DuplicateObject copies flags, and a transient asset would
        // be dropped on save, leaving a null clothing entry in the mesh.
        auto* Template = NewObject<UGratiaSourceClothingAsset>(TemplateOuter, ClothName);
        Asset = Cast<UGratiaSourceClothingAsset>(Factory->CreateFromExistingCloth(SkeletalMesh, SkeletalMesh, Template));
        if (Asset) { Asset->ClearFlags(RF_Transient); Asset->SetFlags(RF_Transactional); }
    }
    if (!Asset || !Asset->GetAssetGuid().IsValid() || Asset->GetFName() != ClothName || Asset->HasAnyFlags(RF_Transient)
        || Asset->GetOutermost() != SkeletalMesh->GetOutermost()) return false;
    Asset->Modify();
    TArray<FGratiaStoredClothSectionMapping> Stored;
    for (const auto& Mapping : Mappings)
    {
        const auto& Section = SkeletalMesh->GetImportedModel()->LODModels[0].Sections[Mapping.Section];
        auto& Entry = Stored.AddDefaulted_GetRef();
        Entry.SectionIndex = Mapping.Section;
        Entry.NumVertices = Section.SoftVertices.Num();
        Entry.PositionHash = UGratiaSourceClothingAsset::HashSectionPositions(Section);
        Entry.MappingBytes.SetNumUninitialized(Mapping.Mapping.Num() * sizeof(FMeshToMeshVertData));
        FMemory::Memcpy(Entry.MappingBytes.GetData(), Mapping.Mapping.GetData(), Entry.MappingBytes.Num());
    }
    Asset->SetStoredMappings(MoveTemp(Stored));
    Asset->LodData.Empty();
    Asset->LodMap.Empty();
    Asset->UsedBoneNames = Data.Bones;
    Asset->LodData.AddDefaulted();
    auto& Lod = Asset->LodData[0];
    Lod.PhysicalMeshData = MoveTemp(Data.Physical);
    Lod.bSmoothTransition = true;
    Lod.bUseMultipleInfluences = false;
    const auto MaxDistances = Lod.PhysicalMeshData.GetWeightMap(EWeightMapTargetCommon::MaxDistance).Values;
    const auto Drive = Lod.PhysicalMeshData.GetWeightMap(EWeightMapTargetCommon::AnimDriveStiffness).Values;
    AddMask(Lod, (uint32)EWeightMapTargetCommon::MaxDistance, TEXT("SourcePinMaxDistance"), MaxDistances);
    AddMask(Lod, (uint32)EWeightMapTargetCommon::AnimDriveStiffness, TEXT("SourcePinAnimDrive"), Drive);
    AddMask(Lod, (uint32)PressureTarget, TEXT("SourcePressureRegions"), Data.PressureMask);
    auto* Config = NewObject<UChaosClothConfig>(Asset);
    Config->MassMode = EClothMassMode::TotalMass;
    Config->TotalMass = 1.f;
    Config->EdgeStiffnessWeighted = {0.65f, 0.65f};
    Config->BendingStiffnessWeighted = {0.1f, 0.1f};
    Config->AreaStiffnessWeighted = {0.85f, 0.85f};
    Config->TetherStiffness = {0.8f, 0.8f};
    Config->TetherScale = {1.05f, 1.05f};
    // Blender keeps cage volume with pressure and pin springs. Chaos pressure on
    // three open cages inflated free particles to MaxDistance at rest, so rest
    // shape comes from the anim drive instead; the pressure mask stays editable.
    Config->AnimDriveStiffness = {0.35f, 1.0f};
    Config->AnimDriveDamping = {0.2f, 0.4f};
    Config->DampingCoefficient = 0.1f;
    Config->LocalDampingCoefficient = 0.05f;
    Config->GravityScale = 0.2f;
    Config->Pressure = {0.f, 0.f};
    Config->CollisionThickness = 0.15f;
    Config->FrictionCoefficient = 0.5f;
    Config->bUseCCD = true;
    Config->bUseSelfCollisions = false;
    auto* Shared = NewObject<UChaosClothSharedSimConfig>(Asset);
    Shared->IterationCount = 5;
    Shared->MaxIterationCount = 8;
    Shared->SubdivisionCount = 2;
    Asset->ClothConfigs.Empty();
    Asset->ClothConfigs.Add(Config->GetClass()->GetFName(), Config);
    Asset->ClothConfigs.Add(Shared->GetClass()->GetFName(), Shared);
    // Only kinematic core collisions: simulated body capsules must not push their own cage away.
    UPhysicsAsset* CollisionAsset = nullptr;
    if (const UPhysicsAsset* ExistingPhysics = SkeletalMesh->GetPhysicsAsset())
    {
        const FName CollisionName = MakeUniqueObjectName(Asset, UPhysicsAsset::StaticClass(), TEXT("CoreCollision"));
        CollisionAsset = DuplicateObject<UPhysicsAsset>(ExistingPhysics, Asset, CollisionName);
        CollisionAsset->ConstraintSetup.Empty();
        CollisionAsset->SkeletalBodySetups.RemoveAll([&](const USkeletalBodySetup* Body)
        {
            return !Body || Body->PhysicsType != PhysType_Kinematic || CoreColliderTouchesFreeCage(Body, SkeletalMesh, Lod.PhysicalMeshData);
        });
        CollisionAsset->CollisionDisableTable.Empty();
        CollisionAsset->UpdateBodySetupIndexMap();
        CollisionAsset->UpdateBoundsBodiesArray();
    }
    Asset->PhysicsAsset = CollisionAsset;
    Asset->RefreshBoneMapping(SkeletalMesh);
    Asset->CalculateReferenceBoneIndex();
    Asset->ApplyParameterMasks(false);
    Asset->InvalidateAllCachedData();
    SkeletalMesh->AddClothingAsset(Asset);
    const int32 AssetIndex = Assets.IndexOfByKey(Asset);
    auto& MeshLod = SkeletalMesh->GetImportedModel()->LODModels[0];
    for (const auto& Mapping : Mappings)
    {
        auto& Section = MeshLod.Sections[Mapping.Section];
        Section.CorrespondClothAssetIndex = AssetIndex;
        Section.ClothingData.AssetGuid = Asset->GetAssetGuid();
        Section.ClothingData.AssetLodIndex = 0;
        auto& UserData = MeshLod.UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);
        UserData.CorrespondClothAssetIndex = AssetIndex;
        UserData.ClothingData = Section.ClothingData;
        Section.ClothMappingDataLODs.SetNum(1);
        Section.ClothMappingDataLODs[0] = Mapping.Mapping;
        for (const FName Bone : Asset->UsedBoneNames)
        {
            const int32 Index = SkeletalMesh->GetRefSkeleton().FindBoneIndex(Bone);
            Section.BoneMap.AddUnique(Index);
            MeshLod.RequiredBones.AddUnique(Index);
            MeshLod.ActiveBoneIndices.AddUnique(Index);
        }
        UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_SECTION section=%d material=%d render_vertices=%d dynamic_vertices=%d distance_faded=%d rest_error_cm=%.8f"),
            Mapping.Section, Section.MaterialIndex, Section.SoftVertices.Num(), Mapping.DynamicVertices, Mapping.DistanceFaded, Mapping.MaxRestError);
    }
    MeshLod.RequiredBones.Sort();
    SkeletalMesh->GetRefSkeleton().EnsureParentsExistAndSort(MeshLod.ActiveBoneIndices);
    Asset->LodMap.Init(INDEX_NONE, SkeletalMesh->GetImportedModel()->LODModels.Num());
    Asset->LodMap[0] = 0;
    // Removing our previous assets can change indices of unrelated cloth entries.
    for (auto& OtherLod : SkeletalMesh->GetImportedModel()->LODModels)
        for (auto& Section : OtherLod.Sections)
            if (Section.HasClothingData())
                for (int32 Index = 0; Index < Assets.Num(); ++Index)
                    if (Assets[Index] && Assets[Index]->GetAssetGuid() == Section.ClothingData.AssetGuid)
                    {
                        Section.CorrespondClothAssetIndex = Index;
                        auto& UserData = OtherLod.UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);
                        UserData.CorrespondClothAssetIndex = Index;
                        UserData.ClothingData = Section.ClothingData;
                    }
    SkeletalMesh->MarkPackageDirty();
    CoreColliderCount = CollisionAsset ? CollisionAsset->SkeletalBodySetups.Num() : 0;
    SimulationVertexCount = Lod.PhysicalMeshData.Vertices.Num();
    }
    FAssetCompilingManager::Get().FinishCompilationForObjects({SkeletalMesh});
    // Verify the binding that survived the engine rebuild, not only what was written.
    for (const auto& Mapping : Mappings)
        if (!Asset->IsSectionUsingStoredMapping(SkeletalMesh, 0, Mapping.Section))
        {
            UE_LOG(LogGratiaClothPort, Error, TEXT("SOURCE_CLOTH_REBUILD_LOST_MAPPING section=%d"), Mapping.Section);
            return false;
        }
    UE_LOG(LogGratiaClothPort, Display, TEXT("GRATIA_SOURCE_CLOTH_CAGES_BUILT cages=%d simulation_vertices=%d fixed_vertices=%d sections=%d core_colliders=%d morphs_preserved=%d rebuild_verified=1"),
        Data.Cages.Num(), SimulationVertexCount, Data.FixedCount, Mappings.Num(), CoreColliderCount, SkeletalMesh->GetMorphTargets().Num());
    return true;
}
