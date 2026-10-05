#include "GratiaSoftBodySetupLibrary.h"

#include "Chaos/Convex.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConvexElem.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaSoftBodySetup, Log, All);

namespace GratiaSoftBodySetup
{
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
    UE_LOG(LogGratiaSoftBodySetup, Display, TEXT("SOFT_BODY_SOURCE_FRAME bones=%d scale=%.8f max_error_cm=%.8f axes=%d%d%d signs=%.0f,%.0f,%.0f"),
        Source.Num(), Out.Scale, Out.MaximumError, Out.Axes[0], Out.Axes[1], Out.Axes[2], Out.Signs[0], Out.Signs[1], Out.Signs[2]);
    return Out.MaximumError <= 0.05;
}

FTransform RefComponentTransform(const FReferenceSkeleton& Skeleton, int32 BoneIndex)
{
    FTransform Result = Skeleton.GetRefBonePose()[BoneIndex];
    for (int32 Parent = Skeleton.GetParentIndex(BoneIndex); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
        Result *= Skeleton.GetRefBonePose()[Parent];
    return Result;
}
}

bool UGratiaSoftBodySetupLibrary::MeasureSoftBone(USkeletalMesh* SkeletalMesh, FName RootBone, EGratiaBoneAxis& ForwardAxis,
    float& LengthCm, float& ContactRadiusCm, float& ContactCenterAlongBone)
{
    using namespace GratiaSoftBodySetup;
    if (!SkeletalMesh || !SkeletalMesh->GetImportedModel() || SkeletalMesh->GetImportedModel()->LODModels.IsEmpty()) return false;
    const FReferenceSkeleton& Ref = SkeletalMesh->GetRefSkeleton();
    const int32 Bone = Ref.FindBoneIndex(RootBone);
    if (Bone == INDEX_NONE) return false;
    TArray<FVector> Points;
    for (const FSkelMeshSection& Section : SkeletalMesh->GetImportedModel()->LODModels[0].Sections)
        for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
        {
            int32 Dominant = INDEX_NONE; uint16 Best = 0;
            for (int32 I = 0; I < MAX_TOTAL_INFLUENCES; ++I)
                if (Vertex.InfluenceWeights[I] > Best && Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[I]))
                { Best = Vertex.InfluenceWeights[I]; Dominant = Section.BoneMap[Vertex.InfluenceBones[I]]; }
            if (Dominant == Bone && Best >= 32768) Points.Add(FVector(Vertex.Position));
        }
    if (Points.Num() < 16) return false;
    const FTransform BoneCS = RefComponentTransform(Ref, Bone);
    FVector Centroid = FVector::ZeroVector;
    for (const FVector& Point : Points) Centroid += Point;
    Centroid /= Points.Num();
    const FVector Local = BoneCS.GetRotation().UnrotateVector((Centroid - BoneCS.GetLocation()).GetSafeNormal());
    const int32 Major = FMath::Abs(Local.X) >= FMath::Abs(Local.Y) && FMath::Abs(Local.X) >= FMath::Abs(Local.Z) ? 0 : FMath::Abs(Local.Y) >= FMath::Abs(Local.Z) ? 1 : 2;
    const bool bNegative = Local[Major] < 0;
    ForwardAxis = static_cast<EGratiaBoneAxis>(Major * 2 + (bNegative ? 1 : 0));
    FVector AxisLocal = FVector::ZeroVector; AxisLocal[Major] = bNegative ? -1 : 1;
    const FVector Axis = BoneCS.GetRotation().RotateVector(AxisLocal);
    double Farthest = 0;
    TArray<double> Distances;
    for (const FVector& Point : Points)
    {
        Farthest = FMath::Max(Farthest, FVector::DotProduct(Point - BoneCS.GetLocation(), Axis));
        Distances.Add(FVector::Distance(Point, Centroid));
    }
    Distances.Sort();
    LengthCm = float(FMath::Max(Farthest, 1.0));
    ContactRadiusCm = float(Distances[FMath::Clamp(int32(Distances.Num() * 0.8), 0, Distances.Num() - 1)]);
    ContactCenterAlongBone = FMath::Clamp(float(FVector::DotProduct(Centroid - BoneCS.GetLocation(), Axis) / LengthCm), 0.0f, 1.5f);
    UE_LOG(LogGratiaSoftBodySetup, Display, TEXT("SOFT_BODY_BONE %s skin_vertices=%d axis=%d length_cm=%.2f contact_radius_cm=%.2f center_along=%.2f"),
        *RootBone.ToString(), Points.Num(), int32(ForwardAxis), LengthCm, ContactRadiusCm, ContactCenterAlongBone);
    return true;
}

bool UGratiaSoftBodySetupLibrary::BuildBodyColliders(USkeletalMesh* SkeletalMesh, const FString& JSONPath, const TArray<FName>& SoftBones,
    float RangeCm, TArray<FGratiaBodyColliderSphere>& Colliders)
{
    using namespace GratiaSoftBodySetup;
    Colliders.Reset();
    FString Text;
    TSharedPtr<FJsonObject> Json;
    if (!SkeletalMesh || !FFileHelper::LoadFileToString(Text, *JSONPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json)) return false;
    FSourceTransform Transform;
    if (!Calibrate(SkeletalMesh, Json, Transform)) return false;
    const TArray<TSharedPtr<FJsonValue>>* Sources;
    if (!Json->TryGetArrayField(TEXT("colliders"), Sources) || Sources->IsEmpty()) return false;
    const FReferenceSkeleton& Ref = SkeletalMesh->GetRefSkeleton();
    TArray<FVector> SoftPoints;
    for (const FName Bone : SoftBones)
    {
        const int32 Index = Ref.FindBoneIndex(Bone);
        if (Index == INDEX_NONE) return false;
        SoftPoints.Add(RefComponentTransform(Ref, Index).GetLocation());
    }
    double GapSum = 0, WorstGap = 0;
    int32 GapCount = 0;
    for (const auto& SourceValue : *Sources)
    {
        const auto Source = SourceValue->AsObject();
        const TArray<TSharedPtr<FJsonValue>> *Vertices, *Weights;
        FString Name;
        if (!Source.IsValid() || !Source->TryGetStringField(TEXT("name"), Name) || !Source->TryGetArrayField(TEXT("vertices"), Vertices)
            || !Source->TryGetArrayField(TEXT("weights"), Weights) || Vertices->Num() != Weights->Num()) return false;
        TMap<FName, TArray<FVector>> Groups;
        for (int32 I = 0; I < Vertices->Num(); ++I)
        {
            FVector Position;
            if (!ReadVector((*Vertices)[I], Position)) return false;
            FName Dominant; double Best = 0;
            for (const auto& WeightValue : (*Weights)[I]->AsArray())
            {
                const auto Weight = WeightValue->AsObject();
                FString Bone;
                if (Weight.IsValid() && Weight->TryGetStringField(TEXT("bone"), Bone) && Number(Weight, TEXT("weight"), 0) > Best)
                { Best = Number(Weight, TEXT("weight"), 0); Dominant = FName(*Bone); }
            }
            if (!Dominant.IsNone() && Ref.FindBoneIndex(Dominant) != INDEX_NONE) Groups.FindOrAdd(Dominant).Add(Transform.Position(Position));
        }
        for (auto& Pair : Groups)
        {
            if (Pair.Value.Num() < 8) continue;
            bool bNear = false;
            for (const FVector& Point : Pair.Value) for (const FVector& Soft : SoftPoints) bNear |= FVector::Distance(Point, Soft) < RangeCm;
            if (!bNear) continue;
            // Inscribed spheres of the group's convex hull, greedily covering its surface points.
            FKConvexElem Hull;
            Hull.VertexData = Pair.Value;
            Hull.UpdateElemBox();
            UBodySetup* Cooker = NewObject<UBodySetup>(GetTransientPackage());
            Cooker->AggGeom.ConvexElems.Add(Hull);
            Cooker->CreatePhysicsMeshes();
            const auto& Convex = Cooker->AggGeom.ConvexElems[0];
            if (!Convex.GetChaosConvexMesh()) continue;
            const FBox Box = Convex.ElemBox;
            struct FCandidate { FVector Center; double Radius; };
            TArray<FCandidate> Candidates;
            for (double X = Box.Min.X; X <= Box.Max.X; X += 1.5)
                for (double Y = Box.Min.Y; Y <= Box.Max.Y; Y += 1.5)
                    for (double Z = Box.Min.Z; Z <= Box.Max.Z; Z += 1.5)
                    {
                        const double Phi = Convex.GetChaosConvexMesh()->SignedDistance(Chaos::FVec3(X, Y, Z));
                        if (Phi < -0.3) Candidates.Add({FVector(X, Y, Z), -Phi});
                    }
            TArray<double> Gap;
            Gap.Init(TNumericLimits<double>::Max(), Pair.Value.Num());
            for (int32 Added = 0; Added < 8 && !Candidates.IsEmpty(); ++Added)
            {
                int32 Best = INDEX_NONE; double BestScore = 0;
                for (int32 C = 0; C < Candidates.Num(); ++C)
                {
                    double Score = 0;
                    for (int32 V = 0; V < Pair.Value.Num(); ++V)
                        Score += FMath::Max(0.0, FMath::Min(Gap[V], 10.0) - FMath::Max(FVector::Distance(Pair.Value[V], Candidates[C].Center) - Candidates[C].Radius, 0.0));
                    if (Score > BestScore) { BestScore = Score; Best = C; }
                }
                if (Best == INDEX_NONE || BestScore < 1.0) break;
                FGratiaBodyColliderSphere& Sphere = Colliders.AddDefaulted_GetRef();
                Sphere.Bone = Pair.Key; Sphere.RefCenterCm = Candidates[Best].Center; Sphere.RadiusCm = float(Candidates[Best].Radius);
                for (int32 V = 0; V < Pair.Value.Num(); ++V)
                    Gap[V] = FMath::Min(Gap[V], FVector::Distance(Pair.Value[V], Candidates[Best].Center) - Candidates[Best].Radius);
            }
            for (const double Value : Gap) { GapSum += Value; WorstGap = FMath::Max(WorstGap, Value); ++GapCount; }
        }
        UE_LOG(LogGratiaSoftBodySetup, Display, TEXT("SOFT_BODY_SOURCE_COLLIDER %s vertices=%d"), *Name, Vertices->Num());
    }
    UE_LOG(LogGratiaSoftBodySetup, Display, TEXT("SOFT_BODY_COLLIDERS spheres=%d range_cm=%.1f surface_gap_mean_cm=%.2f max_cm=%.2f"),
        Colliders.Num(), RangeCm, GapCount ? GapSum / GapCount : 0.0, WorstGap);
    return !Colliders.IsEmpty();
}
