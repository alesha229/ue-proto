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
#include "Chaos/Convex.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaClothPort, Log, All);

namespace GratiaClothPort
{
constexpr TCHAR OwnedPrefix[] = TEXT("GratiaSourceCloth_");
constexpr float SampleToleranceCm = 0.2f;
// Blender springs are force-based (stiffness / vertex mass); Chaos uses unitless
// PBD stiffness in [0,1]. s(k) = k / (k + HalfPoint) keeps the source ordering
// per cage; HalfPoint and PressureScale are calibrated against the measured
// source offsets in evidence/04/blender_cloth_offset_reference.json.
float SourceStiffnessHalfPoint = 0.12f;
float SourcePressureScale = 0.01f;
// Chaos has one gravity per asset. A cage whose source gravity is lower than the
// asset gravity gets this anim-drive floor so it stays near its pose as in Blender.
constexpr float GravityCompensationDrive = 0.9f;
constexpr float FreeVertexDriveFloor = 0.05f;

float SourceStiffness(double Stiffness, double Mass)
{
    const double K = Mass > UE_SMALL_NUMBER ? FMath::Max(Stiffness, 0.0) / Mass : 0.0;
    return float(K / (K + SourceStiffnessHalfPoint));
}

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
    float Gravity = 1;
    float Edge = 1, Area = 1, Bending = 1, PinDrive = 1, InternalDrive = 0;
    bool bCollision = true, bSelfCollision = false;
};

struct FPortData
{
    FClothPhysicalMeshData Physical;
    TArray<FName> Bones;
    TArray<FCage> Cages;
    TArray<FInfluenceSample> Samples;
    TMap<FIntVector, TArray<int32>> SampleCells;
    TArray<float> PressureMask, EdgeMask, AreaMask, BendingMask;
    float Gravity = 0;
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
        const double Mass = Number(Settings, TEXT("mass"), 1.0);
        Cage.Edge = SourceStiffness(Number(Settings, TEXT("tension_stiffness"), 15), Mass);
        Cage.Area = SourceStiffness(Number(Settings, TEXT("shear_stiffness"), 5), Mass);
        Cage.Bending = SourceStiffness(Number(Settings, TEXT("bending_stiffness"), 0.5), Mass);
        Cage.PinDrive = SourceStiffness(Number(Settings, TEXT("pin_stiffness"), 1), Mass);
        // Blender internal springs hold free vertices to the cage's own rest shape.
        // Chaos legacy cloth has no internal springs; anim drive toward the
        // animated (rest-shaped) cage is its closest shape-preserving term.
        if (Flag(Settings, TEXT("use_internal_springs"), false))
            Cage.InternalDrive = SourceStiffness(Number(Settings, TEXT("internal_tension_stiffness"), 0), Mass);
        Cage.Gravity = FMath::Clamp((float)Number(CageJson, TEXT("gravity"), 1.0), 0.f, 1.f);
        const TSharedPtr<FJsonObject>* CollisionPtr;
        if (CageJson->TryGetObjectField(TEXT("collision"), CollisionPtr))
        {
            Cage.bCollision = Flag(*CollisionPtr, TEXT("use_collision"), true);
            Cage.bSelfCollision = Flag(*CollisionPtr, TEXT("use_self_collision"), false);
        }
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
            AnimDrive.Add(FMath::Max3(P * Cage.PinDrive, Cage.InternalDrive, FreeVertexDriveFloor));
            Out.PressureMask.Add(Cage.Pressure);
            Out.EdgeMask.Add(Cage.Edge);
            Out.AreaMask.Add(Cage.Area);
            Out.BendingMask.Add(Cage.Bending);
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
        Out.Gravity = FMath::Max(Out.Gravity, Cage.Gravity);
        UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_CAGE name=%s vertices=%d triangles=%d pressure=%.3f edge=%.3f area=%.3f bending=%.3f pin_drive=%.3f internal_drive=%.3f gravity=%.2f collision=%d self_collision=%d"),
            *Cage.Name, Vertices->Num(), Cage.NumTriangles, Cage.Pressure, Cage.Edge, Cage.Area, Cage.Bending, Cage.PinDrive, Cage.InternalDrive, Cage.Gravity, Cage.bCollision, Cage.bSelfCollision);
    }
    if (Out.Physical.Vertices.Num() < 3 || Out.Physical.Vertices.Num() >= 65535 || Out.FixedCount == 0) return false;
    for (const FCage& Cage : Out.Cages)
    {
        if (Cage.Gravity >= Out.Gravity - 0.01f) continue;
        const auto& Indices = Out.Physical.Indices;
        TSet<uint32> CageVertices;
        for (int32 T = Cage.FirstTriangle; T < Cage.FirstTriangle + Cage.NumTriangles; ++T)
            for (int32 C = 0; C < 3; ++C) CageVertices.Add(Indices[T * 3 + C]);
        for (const uint32 Vertex : CageVertices) AnimDrive[Vertex] = FMath::Max(AnimDrive[Vertex], GravityCompensationDrive);
        UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_GRAVITY_COMPENSATION cage=%s source_gravity=%.2f asset_gravity=%.2f drive_floor=%.2f"),
            *Cage.Name, Cage.Gravity, Out.Gravity, GravityCompensationDrive);
    }
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
            // Source SurfaceDeform weights are used as authored, also for clothing.
            const float SimWeight = (1.f - Mapping.SourceMeshVertIndices[3] / 65535.f) * BlendWeights[I];
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

FTransform RefComponentTransform(const FReferenceSkeleton& Skeleton, int32 BoneIndex)
{
    FTransform Result = Skeleton.GetRefBonePose()[BoneIndex];
    for (int32 Parent = Skeleton.GetParentIndex(BoneIndex); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
        Result *= Skeleton.GetRefBonePose()[Parent];
    return Result;
}

/**
 * Source "Collision" objects (Body collision, Head collision) become convex hulls,
 * one per dominant skinning bone, so they follow the skeleton as the armature-deformed
 * source meshes do. Only these source colliders are used, as in Blender.
 */
UPhysicsAsset* BuildSourceColliders(const TSharedPtr<FJsonObject>& Json, USkeletalMesh* Mesh, const FSourceTransform& Transform,
    UObject* Outer, const FPortData& Data, const FClothPhysicalMeshData& Physical, float& OutFriction, float& OutThickness)
{
    const TArray<TSharedPtr<FJsonValue>>* Colliders;
    if (!Json->TryGetArrayField(TEXT("colliders"), Colliders) || Colliders->IsEmpty())
    {
        UE_LOG(LogGratiaClothPort, Error, TEXT("SOURCE_CLOTH_COLLIDERS_MISSING export schema 2 with source collision objects is required"));
        return nullptr;
    }
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    auto* Asset = NewObject<UPhysicsAsset>(Outer, MakeUniqueObjectName(Outer, UPhysicsAsset::StaticClass(), TEXT("SourceCollision")));
    TMap<FName, USkeletalBodySetup*> Bodies;
    double FrictionSum = 0, ThicknessMax = 0;
    for (const auto& ColliderValue : *Colliders)
    {
        const auto Collider = ColliderValue->AsObject();
        FString Name;
        const TArray<TSharedPtr<FJsonValue>> *Vertices, *Weights;
        if (!Collider.IsValid() || !Collider->TryGetStringField(TEXT("name"), Name) || !Collider->TryGetArrayField(TEXT("vertices"), Vertices)
            || !Collider->TryGetArrayField(TEXT("weights"), Weights) || Vertices->Num() != Weights->Num()) return nullptr;
        FrictionSum += Number(Collider, TEXT("friction"), 0);
        ThicknessMax = FMath::Max(ThicknessMax, Number(Collider, TEXT("thickness_outer"), 0.001) * 100.0);
        TMap<FName, TArray<FVector>> Groups;
        for (int32 I = 0; I < Vertices->Num(); ++I)
        {
            FVector Position;
            if (!ReadVector((*Vertices)[I], Position)) return nullptr;
            FName Dominant;
            double Best = 0;
            for (const auto& WeightValue : (*Weights)[I]->AsArray())
            {
                const auto Weight = WeightValue->AsObject();
                FString Bone;
                if (Weight.IsValid() && Weight->TryGetStringField(TEXT("bone"), Bone) && Number(Weight, TEXT("weight"), 0) > Best)
                { Best = Number(Weight, TEXT("weight"), 0); Dominant = FName(*Bone); }
            }
            if (Dominant.IsNone() || Skeleton.FindBoneIndex(Dominant) == INDEX_NONE) return nullptr;
            Groups.FindOrAdd(Dominant).Add(Transform.Position(Position));
        }
        int32 Hulls = 0, Skipped = 0;
        for (auto& Pair : Groups)
        {
            if (Pair.Value.Num() < 8) { ++Skipped; continue; }
            const FTransform BoneTransform = RefComponentTransform(Skeleton, Skeleton.FindBoneIndex(Pair.Key));
            FKConvexElem Elem;
            for (const FVector& Point : Pair.Value) Elem.VertexData.Add(BoneTransform.InverseTransformPosition(Point));
            Elem.UpdateElemBox();
            USkeletalBodySetup*& Body = Bodies.FindOrAdd(Pair.Key);
            if (!Body)
            {
                Body = NewObject<USkeletalBodySetup>(Asset, NAME_None, RF_Transactional);
                Body->BoneName = Pair.Key;
                Body->PhysicsType = PhysType_Kinematic;
                Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
                Asset->SkeletalBodySetups.Add(Body);
            }
            Body->AggGeom.ConvexElems.Add(MoveTemp(Elem));
            ++Hulls;
        }
        UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_COLLIDER name=%s vertices=%d convex_hulls=%d skipped_small_groups=%d friction=%.2f"),
            *Name, Vertices->Num(), Hulls, Skipped, Number(Collider, TEXT("friction"), 0));
    }
    for (auto& Pair : Bodies)
    {
        Pair.Value->InvalidatePhysicsData();
        Pair.Value->CreatePhysicsMeshes();
        for (const auto& Convex : Pair.Value->AggGeom.ConvexElems)
            if (!Convex.GetChaosConvexMesh())
            {
                UE_LOG(LogGratiaClothPort, Error, TEXT("SOURCE_CLOTH_COLLIDER_COOK_FAILED bone=%s"), *Pair.Key.ToString());
                return nullptr;
            }
    }
    // A dynamic particle never leaves its MaxDistance sphere around the animated
    // position, so a hull farther than that (plus a margin for animation stretch)
    // from every dynamic particle can never collide. Dropping such hulls keeps the
    // collision result and removes most per-particle convex tests.
    const auto& Reach = Physical.GetWeightMap(EWeightMapTargetCommon::MaxDistance).Values;
    constexpr double ReachMarginCm = 2.0;
    int32 Kept = 0, Culled = 0;
    for (auto& Pair : Bodies)
    {
        const FTransform BoneTransform = RefComponentTransform(Skeleton, Skeleton.FindBoneIndex(Pair.Key));
        const double BoneScale = BoneTransform.GetScale3D().GetAbsMax();
        Pair.Value->AggGeom.ConvexElems.RemoveAll([&](const FKConvexElem& Convex)
        {
            for (int32 Vertex = 0; Vertex < Physical.Vertices.Num(); ++Vertex)
            {
                if (Reach[Vertex] <= 0.001f) continue;
                const FVector Local = BoneTransform.InverseTransformPosition(FVector(Physical.Vertices[Vertex]));
                const double Distance = Convex.GetChaosConvexMesh()->SignedDistance(Chaos::FVec3(Local)) * BoneScale;
                if (Distance <= Reach[Vertex] + ReachMarginCm) { ++Kept; return false; }
            }
            ++Culled;
            return true;
        });
    }
    Asset->SkeletalBodySetups.RemoveAll([](const USkeletalBodySetup* Body) { return Body->AggGeom.ConvexElems.IsEmpty(); });
    for (auto It = Bodies.CreateIterator(); It; ++It) if (It.Value()->AggGeom.ConvexElems.IsEmpty()) It.RemoveCurrent();
    UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_COLLIDER_REACH kept_hulls=%d culled_unreachable=%d margin_cm=%.1f"), Kept, Culled, ReachMarginCm);
    // Chaos cloth tests every particle against every convex face, which measured
    // ~6 FPS with the reachable hulls. Each hull becomes a few inscribed spheres
    // (cheap in Chaos); the residual against the source surface is reported.
    constexpr double GridCm = 1.5, CoveredCm = 0.5;
    constexpr int32 MaxSpheresPerHull = 10;
    double WorstGapCm = 0, GapSum = 0;
    int32 GapCount = 0, SphereCount = 0;
    for (auto& Pair : Bodies)
    {
        const FTransform BoneTransform = RefComponentTransform(Skeleton, Skeleton.FindBoneIndex(Pair.Key));
        const double BoneScale = BoneTransform.GetScale3D().GetAbsMax();
        auto& Geometry = Pair.Value->AggGeom;
        for (const FKConvexElem& Convex : Geometry.ConvexElems)
        {
            const auto& Hull = *Convex.GetChaosConvexMesh();
            const FBox Box = Convex.ElemBox;
            const double Step = GridCm / BoneScale;
            struct FCandidate { FVector Center; double Radius; };
            TArray<FCandidate> Candidates;
            for (double X = Box.Min.X; X <= Box.Max.X; X += Step)
                for (double Y = Box.Min.Y; Y <= Box.Max.Y; Y += Step)
                    for (double Z = Box.Min.Z; Z <= Box.Max.Z; Z += Step)
                    {
                        const double Phi = Hull.SignedDistance(Chaos::FVec3(X, Y, Z));
                        if (Phi < -0.3 / BoneScale) Candidates.Add({FVector(X, Y, Z), -Phi});
                    }
            TArray<double> Gap;
            Gap.Init(TNumericLimits<double>::Max(), Convex.VertexData.Num());
            for (int32 Added = 0; Added < MaxSpheresPerHull && !Candidates.IsEmpty(); ++Added)
            {
                int32 Best = INDEX_NONE;
                double BestScore = 0;
                for (int32 C = 0; C < Candidates.Num(); ++C)
                {
                    double Score = 0;
                    for (int32 V = 0; V < Convex.VertexData.Num(); ++V)
                    {
                        const double NewGap = (FVector::Distance(Convex.VertexData[V], Candidates[C].Center) - Candidates[C].Radius) * BoneScale;
                        Score += FMath::Max(0.0, FMath::Min(Gap[V], 10.0) - FMath::Max(NewGap, 0.0));
                    }
                    if (Score > BestScore) { BestScore = Score; Best = C; }
                }
                if (Best == INDEX_NONE || BestScore < 1.0) break;
                FKSphereElem Sphere(float(Candidates[Best].Radius));
                Sphere.Center = Candidates[Best].Center;
                Geometry.SphereElems.Add(Sphere);
                ++SphereCount;
                for (int32 V = 0; V < Convex.VertexData.Num(); ++V)
                    Gap[V] = FMath::Min(Gap[V], (FVector::Distance(Convex.VertexData[V], Candidates[Best].Center) - Candidates[Best].Radius) * BoneScale);
            }
            for (const double Value : Gap) { WorstGapCm = FMath::Max(WorstGapCm, Value); GapSum += Value; ++GapCount; }
            int32 Covered = 0;
            for (const double Value : Gap) Covered += Value <= CoveredCm;
            UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_COLLIDER_SPHERES bone=%s source_points=%d within_%.1fcm=%d"),
                *Pair.Key.ToString(), Convex.VertexData.Num(), CoveredCm, Covered);
        }
        Geometry.ConvexElems.Empty();
        Pair.Value->InvalidatePhysicsData();
    }
    UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_COLLIDER_APPROXIMATION spheres=%d source_surface_gap_mean_cm=%.3f max_cm=%.3f"),
        SphereCount, GapCount ? GapSum / GapCount : 0.0, WorstGapCm);
    Asset->UpdateBodySetupIndexMap();
    Asset->UpdateBoundsBodiesArray();
    // Report particles that start inside a collider: Chaos collides every cage,
    // while in the source only cages with collision enabled react to colliders.
    const auto& MaxDistance = Physical.GetWeightMap(EWeightMapTargetCommon::MaxDistance).Values;
    for (const FCage& Cage : Data.Cages)
    {
        TSet<uint32> CageVertices;
        for (int32 T = Cage.FirstTriangle; T < Cage.FirstTriangle + Cage.NumTriangles; ++T)
            for (int32 C = 0; C < 3; ++C) CageVertices.Add(Physical.Indices[T * 3 + C]);
        int32 Inside = 0, Dynamic = 0;
        for (const uint32 Vertex : CageVertices)
        {
            if (MaxDistance[Vertex] <= 0.001f) continue;
            ++Dynamic;
            const FVector Point(Physical.Vertices[Vertex]);
            bool bInside = false;
            for (const auto& Pair : Bodies)
            {
                const FTransform BoneTransform = RefComponentTransform(Skeleton, Skeleton.FindBoneIndex(Pair.Key));
                const FVector Local = BoneTransform.InverseTransformPosition(Point);
                for (const auto& Sphere : Pair.Value->AggGeom.SphereElems)
                    bInside |= FVector::Distance(Local, Sphere.Center) < Sphere.Radius;
                if (bInside) break;
            }
            Inside += bInside;
        }
        UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_REST_INSIDE_COLLIDER cage=%s source_collision=%d dynamic=%d inside=%d"),
            *Cage.Name, Cage.bCollision, Dynamic, Inside);
    }
    OutFriction = float(FrictionSum / Colliders->Num());
    OutThickness = float(ThicknessMax);
    return Asset;
}
}

bool UGratiaClothPortLibrary::BuildSourceClothCages(USkeletalMesh* SkeletalMesh, const FString& JSONPath, const TArray<FName>& ExcludedBoneRoots,
    float StiffnessHalfPoint, float PressureScale)
{
    using namespace GratiaClothPort;
    if (!FMath::IsFinite(StiffnessHalfPoint) || StiffnessHalfPoint <= 0 || !FMath::IsFinite(PressureScale) || PressureScale < 0) return false;
    SourceStiffnessHalfPoint = StiffnessHalfPoint;
    SourcePressureScale = PressureScale;
    UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_CALIBRATION stiffness_half_point=%.4f pressure_scale=%.4f"), StiffnessHalfPoint, PressureScale);
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
    const int64 EdgeTarget = Targets ? Targets->GetValueByNameString(TEXT("EdgeStiffness")) : INDEX_NONE;
    const int64 AreaTarget = Targets ? Targets->GetValueByNameString(TEXT("AreaStiffness")) : INDEX_NONE;
    const int64 BendingTarget = Targets ? Targets->GetValueByNameString(TEXT("BendingStiffness")) : INDEX_NONE;
    if (PressureTarget == INDEX_NONE || EdgeTarget == INDEX_NONE || AreaTarget == INDEX_NONE || BendingTarget == INDEX_NONE) return false;
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
    AddMask(Lod, (uint32)EdgeTarget, TEXT("SourceTensionByCage"), Data.EdgeMask);
    AddMask(Lod, (uint32)AreaTarget, TEXT("SourceShearByCage"), Data.AreaMask);
    AddMask(Lod, (uint32)BendingTarget, TEXT("SourceBendingByCage"), Data.BendingMask);
    auto* Config = NewObject<UChaosClothConfig>(Asset);
    Config->MassMode = EClothMassMode::TotalMass;
    Config->TotalMass = 1.f;
    // Per-cage source values live in the weight maps; the ranges are identity.
    Config->EdgeStiffnessWeighted = {0.f, 1.f};
    Config->BendingStiffnessWeighted = {0.f, 1.f};
    Config->AreaStiffnessWeighted = {0.f, 1.f};
    Config->TetherStiffness = {0.8f, 0.8f};
    Config->TetherScale = {1.05f, 1.05f};
    Config->AnimDriveStiffness = {0.f, 1.f};
    Config->AnimDriveDamping = {0.2f, 0.4f};
    Config->DampingCoefficient = 0.1f;
    Config->LocalDampingCoefficient = 0.05f;
    Config->GravityScale = Data.Gravity;
    Config->Pressure = {0.f, SourcePressureScale};
    // Hands already use swept capsules; CCD against every collider sphere cost
    // most of the frame in the editor measurement.
    Config->bUseCCD = false;
    Config->bUseSelfCollisions = false;
    auto* Shared = NewObject<UChaosClothSharedSimConfig>(Asset);
    Shared->IterationCount = 5;
    Shared->MaxIterationCount = 8;
    Shared->SubdivisionCount = 2;
    Asset->ClothConfigs.Empty();
    Asset->ClothConfigs.Add(Config->GetClass()->GetFName(), Config);
    Asset->ClothConfigs.Add(Shared->GetClass()->GetFName(), Shared);
    float Friction = 0, Thickness = 0.1f;
    UPhysicsAsset* CollisionAsset = BuildSourceColliders(Json, SkeletalMesh, Transform, Asset, Data, Lod.PhysicalMeshData, Friction, Thickness);
    if (!CollisionAsset) return false;
    Config->FrictionCoefficient = Friction;
    Config->CollisionThickness = FMath::Max(Thickness, 0.1f);
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
        UE_LOG(LogGratiaClothPort, Display, TEXT("SOURCE_CLOTH_SECTION section=%d material=%d render_vertices=%d dynamic_vertices=%d rest_error_cm=%.8f"),
            Mapping.Section, Section.MaterialIndex, Section.SoftVertices.Num(), Mapping.DynamicVertices, Mapping.MaxRestError);
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
    // The section mapping is not part of the derived-data key: without a new key the
    // build would reuse a cached LOD model holding the previous binding.
    SkeletalMesh->InvalidateDeriveDataCacheGUID();
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
