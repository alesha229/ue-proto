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
    TArray<FVector> Points, Shared;
    for (const FSkelMeshSection& Section : SkeletalMesh->GetImportedModel()->LODModels[0].Sections)
        for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
        {
            int32 Dominant = INDEX_NONE; uint16 Best = 0, Own = 0;
            for (int32 I = 0; I < MAX_TOTAL_INFLUENCES; ++I)
                if (Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[I]))
                {
                    if (Section.BoneMap[Vertex.InfluenceBones[I]] == Bone) Own = FMath::Max(Own, Vertex.InfluenceWeights[I]);
                    if (Vertex.InfluenceWeights[I] > Best) { Best = Vertex.InfluenceWeights[I]; Dominant = Section.BoneMap[Vertex.InfluenceBones[I]]; }
                }
            if (Dominant == Bone && Best >= 32768) Points.Add(FVector(Vertex.Position));
            if (Own >= 13107) Shared.Add(FVector(Vertex.Position));
        }
    // A soft bone that shares its flesh with a limb (thigh) rarely dominates: use its shared skin.
    if (Points.Num() < 16 || Shared.Num() > Points.Num() * 3) Points = MoveTemp(Shared);
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
    TArray<double> Distances, Radial;
    for (const FVector& Point : Points)
    {
        const double Along = FVector::DotProduct(Point - BoneCS.GetLocation(), Axis);
        Farthest = FMath::Max(Farthest, Along);
        Distances.Add(FVector::Distance(Point, Centroid));
        Radial.Add((Point - BoneCS.GetLocation() - Axis * Along).Size());
    }
    Distances.Sort(); Radial.Sort();
    LengthCm = float(FMath::Max(Farthest, 1.0));
    // A round part (breast) fits a ball around its centroid; flesh along a limb, a cylinder around
    // the axis at the skin (median: contact starts at the skin, not at the outer clothing layer).
    const int32 P80 = FMath::Clamp(int32(Distances.Num() * 0.8), 0, Distances.Num() - 1);
    ContactRadiusCm = float(FMath::Min(Distances[P80], Radial[Radial.Num() / 2]));
    ContactCenterAlongBone = FMath::Clamp(float(FVector::DotProduct(Centroid - BoneCS.GetLocation(), Axis) / LengthCm), 0.0f, 1.5f);
    UE_LOG(LogGratiaSoftBodySetup, Display, TEXT("SOFT_BODY_BONE %s skin_vertices=%d axis=%d length_cm=%.2f contact_radius_cm=%.2f center_along=%.2f"),
        *RootBone.ToString(), Points.Num(), int32(ForwardAxis), LengthCm, ContactRadiusCm, ContactCenterAlongBone);
    return true;
}

bool UGratiaSoftBodySetupLibrary::MeasureBodySurface(USkeletalMesh* SkeletalMesh, const TArray<FName>& IncludeSlots,
    const TArray<FName>& ExcludeBones, int32 MinVertices, float RadiusPercentile, TArray<FGratiaSurfaceCapsule>& Capsules)
{
    return MeasureLimbSurface(SkeletalMesh, IncludeSlots, ExcludeBones, TArray<FName>(), MinVertices, RadiusPercentile, Capsules);
}

bool UGratiaSoftBodySetupLibrary::MeasureLimbSurface(USkeletalMesh* SkeletalMesh, const TArray<FName>& IncludeSlots,
    const TArray<FName>& ExcludeBones, const TArray<FName>& MergeIntoParent, int32 MinVertices, float RadiusPercentile,
    TArray<FGratiaSurfaceCapsule>& Capsules)
{
    using namespace GratiaSoftBodySetup;
    Capsules.Reset();
    if (!SkeletalMesh || !SkeletalMesh->GetImportedModel() || SkeletalMesh->GetImportedModel()->LODModels.IsEmpty()) return false;
    const FReferenceSkeleton& Ref = SkeletalMesh->GetRefSkeleton();
    const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
    TMap<int32, TArray<FVector>> Buckets;
    for (const FSkelMeshSection& Section : SkeletalMesh->GetImportedModel()->LODModels[0].Sections)
    {
        const FName Slot = Materials.IsValidIndex(Section.MaterialIndex) ? Materials[Section.MaterialIndex].ImportedMaterialSlotName : NAME_None;
        if (!IncludeSlots.IsEmpty() && !IncludeSlots.Contains(Slot)) continue;
        for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
        {
            // Merged soft bones count for their parent (one limb surface).
            TMap<int32, uint32, TInlineSetAllocator<8>> Weights;
            for (int32 I = 0; I < MAX_TOTAL_INFLUENCES; ++I)
                if (Vertex.InfluenceWeights[I] > 0 && Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[I]))
                {
                    int32 Owner = Section.BoneMap[Vertex.InfluenceBones[I]];
                    if (MergeIntoParent.Contains(Ref.GetBoneName(Owner)) && Ref.GetParentIndex(Owner) != INDEX_NONE) Owner = Ref.GetParentIndex(Owner);
                    Weights.FindOrAdd(Owner) += Vertex.InfluenceWeights[I];
                }
            int32 Dominant = INDEX_NONE; uint32 Best = 0;
            for (const TPair<int32, uint32>& Pair : Weights) if (Pair.Value > Best) { Best = Pair.Value; Dominant = Pair.Key; }
            // Strongly owned vertices only (>= 0.6): blended transitions (hip into thigh)
            // would make the capsule fat.
            if (Dominant != INDEX_NONE && Best >= 39322) Buckets.FindOrAdd(Dominant).Add(FVector(Vertex.Position));
        }
    }
    auto Percentile = [](TArray<double> Values, double Fraction)
    {
        Values.Sort();
        return Values[FMath::Clamp(int32(Values.Num() * Fraction), 0, Values.Num() - 1)];
    };
    for (const TPair<int32, TArray<FVector>>& Bucket : Buckets)
    {
        const FName Name = Ref.GetBoneName(Bucket.Key);
        const TArray<FVector>& Points = Bucket.Value;
        if (ExcludeBones.Contains(Name) || Points.Num() < MinVertices) continue;
        const FTransform BoneCS = RefComponentTransform(Ref, Bucket.Key);
        const FVector Origin = BoneCS.GetLocation();
        // Along the bone toward the child that continues the main chain (most descendants:
        // thigh -> shin, not a stocking bow); otherwise the skin's principal axis.
        FVector Direction = FVector::ZeroVector;
        int32 BestDescendants = -1;
        for (int32 Child = 0; Child < Ref.GetNum(); ++Child)
        {
            if (Ref.GetParentIndex(Child) != Bucket.Key) continue;
            int32 Descendants = 0;
            for (int32 Other = Child + 1; Other < Ref.GetNum(); ++Other)
                for (int32 Parent = Ref.GetParentIndex(Other); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
                    if (Parent == Child) { ++Descendants; break; }
            const FVector Offset = RefComponentTransform(Ref, Child).GetLocation() - Origin;
            if (Offset.Size() < 1.0) continue;
            if (Descendants > BestDescendants || (Descendants == BestDescendants && Offset.Size() > Direction.Size()))
            { BestDescendants = Descendants; Direction = Offset; }
        }
        if (Direction.Size() < 1.0)
        {
            FVector Mean = FVector::ZeroVector;
            for (const FVector& Point : Points) Mean += Point;
            Mean /= Points.Num();
            FMatrix Covariance(EForceInit::ForceInitToZero);
            for (const FVector& Point : Points)
            {
                const FVector D = Point - Mean;
                for (int32 R = 0; R < 3; ++R) for (int32 C = 0; C < 3; ++C) Covariance.M[R][C] += D[R] * D[C];
            }
            Direction = FVector(1, 1, 1);
            for (int32 Iteration = 0; Iteration < 32; ++Iteration)
                Direction = FVector(Covariance.TransformVector(Direction)).GetSafeNormal();
        }
        Direction = Direction.GetSafeNormal();
        if (Direction.IsNearlyZero()) continue;
        FVector Center = FVector::ZeroVector;
        TArray<double> Along;
        for (const FVector& Point : Points)
        {
            const FVector Relative = Point - Origin;
            const double T = FVector::DotProduct(Relative, Direction);
            Along.Add(T);
            Center += Relative - Direction * T;
        }
        Center /= Points.Num();
        TArray<double> Distances;
        for (const FVector& Point : Points)
        {
            const FVector Relative = Point - Origin;
            Distances.Add((Relative - Direction * FVector::DotProduct(Relative, Direction) - Center).Size());
        }
        const double Low = Percentile(Along, 0.05), High = Percentile(Along, 0.95);
        // Cross-section shape around the centre line: principal width direction and half extents.
        double Sxx = 0, Sxy = 0, Syy = 0;
        const FVector U = FVector::CrossProduct(Direction, FMath::Abs(Direction.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
        const FVector V = FVector::CrossProduct(Direction, U);
        for (const FVector& Point : Points)
        {
            const FVector Relative = Point - Origin;
            const FVector Radial = Relative - Direction * FVector::DotProduct(Relative, Direction) - Center;
            const double X = FVector::DotProduct(Radial, U), Y = FVector::DotProduct(Radial, V);
            Sxx += X * X; Sxy += X * Y; Syy += Y * Y;
        }
        const double Theta = 0.5 * FMath::Atan2(2.0 * Sxy, Sxx - Syy);
        const FVector Wide = (U * FMath::Cos(Theta) + V * FMath::Sin(Theta)).GetSafeNormal();
        const FVector Narrow = FVector::CrossProduct(Direction, Wide).GetSafeNormal();
        TArray<double> WideExtent, NarrowExtent;
        for (const FVector& Point : Points)
        {
            const FVector Relative = Point - Origin;
            const FVector Radial = Relative - Direction * FVector::DotProduct(Relative, Direction) - Center;
            WideExtent.Add(FMath::Abs(FVector::DotProduct(Radial, Wide)));
            NarrowExtent.Add(FMath::Abs(FVector::DotProduct(Radial, Narrow)));
        }
        const double HalfWide = Percentile(WideExtent, 0.85), HalfNarrow = Percentile(NarrowExtent, 0.85);
        FVector StartPoint, EndPoint, WrapAxis = FVector::ZeroVector;
        double Radius;
        // A short, flat part (torso slice) is a stadium across its width; limbs run along the bone.
        if (HalfWide > 1.3 * HalfNarrow && High - Low < 2.0 * HalfWide)
        {
            Radius = HalfNarrow;
            const FVector Mid = Origin + Center + Direction * ((Low + High) * 0.5);
            StartPoint = Mid - Wide * (HalfWide - HalfNarrow);
            EndPoint = Mid + Wide * (HalfWide - HalfNarrow);
            WrapAxis = BoneCS.InverseTransformVectorNoScale(Direction).GetSafeNormal();
        }
        else
        {
            // A limb tapers (thick upper thigh, slim knee): long parts get one capsule per half,
            // each with its own radius, so the outer surface stays close along the length.
            const double Fraction = FMath::Clamp(RadiusPercentile, 0.05f, 0.95f);
            const double Overall = Percentile(Distances, Fraction);
            const int32 Pieces = High - Low > 3.0 * Overall ? 2 : 1;
            for (int32 Piece = 0; Piece < Pieces; ++Piece)
            {
                const double From = FMath::Lerp(Low, High, double(Piece) / Pieces), To = FMath::Lerp(Low, High, double(Piece + 1) / Pieces);
                TArray<double> Local;
                for (int32 I = 0; I < Points.Num(); ++I)
                    if (Along[I] >= From - 0.5 && Along[I] <= To + 0.5) Local.Add(Distances[I]);
                Radius = Local.Num() >= 8 ? Percentile(Local, Fraction) : Overall;
                // Hemispherical caps add the radius at each end; pull the segment in by half of it
                // (only at the outer ends; the halves meet in the middle).
                double A = From + (Piece == 0 ? Radius * 0.5 : 0.0), B = To - (Piece == Pieces - 1 ? Radius * 0.5 : 0.0);
                if (B < A) A = B = (From + To) * 0.5;
                FGratiaSurfaceCapsule Capsule;
                Capsule.Bone = Name;
                Capsule.StartCm = BoneCS.InverseTransformPosition(Origin + Center + Direction * A);
                Capsule.EndCm = BoneCS.InverseTransformPosition(Origin + Center + Direction * B);
                Capsule.RadiusCm = float(FMath::Max(Radius, 0.5));
                Capsules.Add(Capsule);
                UE_LOG(LogGratiaSoftBodySetup, Display, TEXT("BODY_SURFACE %s piece=%d vertices=%d radius_cm=%.2f length_cm=%.2f"),
                    *Name.ToString(), Piece, Local.Num(), Capsule.RadiusCm, float(B - A));
            }
            continue;
        }
        FGratiaSurfaceCapsule Capsule;
        Capsule.Bone = Name;
        Capsule.StartCm = BoneCS.InverseTransformPosition(StartPoint);
        Capsule.EndCm = BoneCS.InverseTransformPosition(EndPoint);
        Capsule.RadiusCm = float(FMath::Max(Radius, 0.5));
        Capsule.WrapAxis = WrapAxis;
        Capsules.Add(Capsule);
        UE_LOG(LogGratiaSoftBodySetup, Display, TEXT("BODY_SURFACE %s slice vertices=%d radius_cm=%.2f length_cm=%.2f section=%.1fx%.1f"),
            *Name.ToString(), Points.Num(), Capsule.RadiusCm, float(FVector::Distance(StartPoint, EndPoint)), HalfWide * 2, HalfNarrow * 2);
    }
    return !Capsules.IsEmpty();
}

namespace GratiaSoftBodySetup
{
/** Vertex positions per dominant bone (weight >= MinWeight), from the included material slots. */
TMap<int32, TArray<FVector>> OwnedVertices(USkeletalMesh* SkeletalMesh, const TArray<FName>& IncludeSlots, uint16 MinWeight = 39322)
{
    TMap<int32, TArray<FVector>> Buckets;
    const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
    for (const FSkelMeshSection& Section : SkeletalMesh->GetImportedModel()->LODModels[0].Sections)
    {
        const FName Slot = Materials.IsValidIndex(Section.MaterialIndex) ? Materials[Section.MaterialIndex].ImportedMaterialSlotName : NAME_None;
        if (!IncludeSlots.IsEmpty() && !IncludeSlots.Contains(Slot)) continue;
        for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
        {
            int32 Dominant = INDEX_NONE; uint16 Best = 0;
            for (int32 I = 0; I < MAX_TOTAL_INFLUENCES; ++I)
                if (Vertex.InfluenceWeights[I] > Best && Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[I]))
                { Best = Vertex.InfluenceWeights[I]; Dominant = Section.BoneMap[Vertex.InfluenceBones[I]]; }
            if (Dominant != INDEX_NONE && Best >= MinWeight) Buckets.FindOrAdd(Dominant).Add(FVector(Vertex.Position));
        }
    }
    return Buckets;
}

/** Bones (and shares) of the skin inside a sphere: a fitted surface follows them like that skin. */
TArray<FGratiaSurfaceInfluence> RegionInfluences(USkeletalMesh* SkeletalMesh, const TArray<FName>& IncludeSlots, const FVector& Center, double Radius,
    int32 OwnerBone)
{
    TMap<int32, double> Sum;
    double Total = 0.0;
    const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
    for (const FSkelMeshSection& Section : SkeletalMesh->GetImportedModel()->LODModels[0].Sections)
    {
        const FName Slot = Materials.IsValidIndex(Section.MaterialIndex) ? Materials[Section.MaterialIndex].ImportedMaterialSlotName : NAME_None;
        if (!IncludeSlots.IsEmpty() && !IncludeSlots.Contains(Slot)) continue;
        for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
        {
            if (FVector::Distance(FVector(Vertex.Position), Center) > Radius) continue;
            // Only the part's own skin (dominated by its bone), not the chest/hip around it.
            int32 Dominant = INDEX_NONE; uint16 Best = 0;
            for (int32 I = 0; I < MAX_TOTAL_INFLUENCES; ++I)
                if (Vertex.InfluenceWeights[I] > Best && Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[I]))
                { Best = Vertex.InfluenceWeights[I]; Dominant = Section.BoneMap[Vertex.InfluenceBones[I]]; }
            if (Dominant != OwnerBone) continue;
            for (int32 I = 0; I < MAX_TOTAL_INFLUENCES; ++I)
                if (Vertex.InfluenceWeights[I] > 0 && Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[I]))
                {
                    const double Weight = Vertex.InfluenceWeights[I] / 65535.0;
                    Sum.FindOrAdd(Section.BoneMap[Vertex.InfluenceBones[I]]) += Weight;
                    Total += Weight;
                }
        }
    }
    Sum.ValueSort([](double A, double B) { return A > B; });
    TArray<FGratiaSurfaceInfluence> Out;
    const FReferenceSkeleton& Ref = SkeletalMesh->GetRefSkeleton();
    for (const TPair<int32, double>& Pair : Sum)
    {
        if (Out.Num() == 3 || Pair.Value < 0.08 * Total) break;
        FGratiaSurfaceInfluence& Influence = Out.AddDefaulted_GetRef();
        Influence.Bone = Ref.GetBoneName(Pair.Key);
        Influence.Weight = float(Pair.Value / FMath::Max(Total, 1.0e-6));
    }
    return Out;
}

double PercentileOf(TArray<double> Values, double Fraction)
{
    if (Values.IsEmpty()) return 0.0;
    Values.Sort();
    return Values[FMath::Clamp(int32(Values.Num() * Fraction), 0, Values.Num() - 1)];
}
}

bool UGratiaSoftBodySetupLibrary::MeasureSphereSurface(USkeletalMesh* SkeletalMesh, const TArray<FName>& IncludeSlots, const TArray<FName>& Bones,
    float RadiusPercentile, TArray<FGratiaSurfaceCapsule>& Capsules)
{
    using namespace GratiaSoftBodySetup;
    Capsules.Reset();
    if (!SkeletalMesh || !SkeletalMesh->GetImportedModel() || SkeletalMesh->GetImportedModel()->LODModels.IsEmpty()) return false;
    const FReferenceSkeleton& Ref = SkeletalMesh->GetRefSkeleton();
    const TMap<int32, TArray<FVector>> Buckets = OwnedVertices(SkeletalMesh, IncludeSlots);
    for (const FName Name : Bones)
    {
        const int32 Bone = Ref.FindBoneIndex(Name);
        const TArray<FVector>* Points = Bone != INDEX_NONE ? Buckets.Find(Bone) : nullptr;
        if (!Points || Points->Num() < 16) continue;
        // Algebraic sphere fit |p|^2 + D.p + G = 0 about the centroid (well conditioned).
        FVector Mean = FVector::ZeroVector;
        for (const FVector& Point : *Points) Mean += Point;
        Mean /= Points->Num();
        double M[4][4] = {}, R[4] = {};
        for (const FVector& Point : *Points)
        {
            const FVector P = Point - Mean;
            const double Row[4] = {P.X, P.Y, P.Z, 1.0};
            const double Rhs = -P.SizeSquared();
            for (int32 I = 0; I < 4; ++I) { R[I] += Row[I] * Rhs; for (int32 J = 0; J < 4; ++J) M[I][J] += Row[I] * Row[J]; }
        }
        // Gaussian elimination with partial pivoting.
        int32 Order[4] = {0, 1, 2, 3};
        bool bSolved = true;
        for (int32 C = 0; C < 4 && bSolved; ++C)
        {
            int32 Pivot = C;
            for (int32 I = C + 1; I < 4; ++I) if (FMath::Abs(M[I][C]) > FMath::Abs(M[Pivot][C])) Pivot = I;
            if (FMath::Abs(M[Pivot][C]) < 1.0e-9) { bSolved = false; break; }
            for (int32 J = 0; J < 4; ++J) Swap(M[C][J], M[Pivot][J]);
            Swap(R[C], R[Pivot]);
            for (int32 I = C + 1; I < 4; ++I)
            {
                const double F = M[I][C] / M[C][C];
                for (int32 J = C; J < 4; ++J) M[I][J] -= F * M[C][J];
                R[I] -= F * R[C];
            }
        }
        double X[4] = {};
        for (int32 I = 3; I >= 0 && bSolved; --I)
        {
            double Sum = R[I];
            for (int32 J = I + 1; J < 4; ++J) Sum -= M[I][J] * X[J];
            X[I] = Sum / M[I][I];
        }
        const FVector Center = Mean + FVector(-X[0] / 2, -X[1] / 2, -X[2] / 2);
        TArray<double> Distances;
        for (const FVector& Point : *Points) Distances.Add(FVector::Distance(Point, Center));
        const double Radius = PercentileOf(Distances, FMath::Clamp(RadiusPercentile, 0.05f, 0.95f));
        if (!bSolved || Center.ContainsNaN() || !(Radius > 0.5) || Radius > 60.0) continue;
        const FTransform BoneCS = RefComponentTransform(Ref, Bone);
        FGratiaSurfaceCapsule Capsule;
        Capsule.Bone = Name;
        Capsule.StartCm = Capsule.EndCm = BoneCS.InverseTransformPosition(Center);
        Capsule.RadiusCm = float(Radius);
        Capsule.Influences = RegionInfluences(SkeletalMesh, IncludeSlots, Center, Radius * 1.15, Bone);
        Capsules.Add(Capsule);
        FString Shares;
        for (const FGratiaSurfaceInfluence& Influence : Capsule.Influences) Shares += FString::Printf(TEXT(" %s=%.2f"), *Influence.Bone.ToString(), Influence.Weight);
        UE_LOG(LogGratiaSoftBodySetup, Display, TEXT("BODY_SURFACE %s sphere vertices=%d radius_cm=%.2f follows%s"), *Name.ToString(), Points->Num(), Capsule.RadiusCm, *Shares);
    }
    return !Capsules.IsEmpty();
}

bool UGratiaSoftBodySetupLibrary::MeasureTorsoSlices(USkeletalMesh* SkeletalMesh, const TArray<FName>& IncludeSlots, const TArray<FName>& VertexBones,
    const TArray<FName>& AttachBones, float StepCm, TArray<FGratiaSurfaceCapsule>& Capsules)
{
    using namespace GratiaSoftBodySetup;
    Capsules.Reset();
    if (!SkeletalMesh || !SkeletalMesh->GetImportedModel() || SkeletalMesh->GetImportedModel()->LODModels.IsEmpty() || StepCm < 1.0f) return false;
    const FReferenceSkeleton& Ref = SkeletalMesh->GetRefSkeleton();
    // Every vertex by its dominant bone: belly skin and clothing are blended between bones.
    const TMap<int32, TArray<FVector>> Buckets = OwnedVertices(SkeletalMesh, IncludeSlots, 0);
    TArray<FVector> Points;
    for (const FName Name : VertexBones)
        if (const TArray<FVector>* Owned = Buckets.Find(Ref.FindBoneIndex(Name))) Points.Append(*Owned);
    TArray<TPair<int32, FVector>> Attach;
    for (const FName Name : AttachBones)
        if (const int32 Bone = Ref.FindBoneIndex(Name); Bone != INDEX_NONE) Attach.Emplace(Bone, RefComponentTransform(Ref, Bone).GetLocation());
    if (Points.Num() < 50 || Attach.IsEmpty()) return false;
    TArray<double> Heights;
    for (const FVector& Point : Points) Heights.Add(Point.Z);
    const double Bottom = PercentileOf(Heights, 0.02), Top = PercentileOf(Heights, 0.98);
    UE_LOG(LogGratiaSoftBodySetup, Display, TEXT("BODY_SURFACE torso range z=%.1f..%.1f vertices=%d"), Bottom, Top, Points.Num());
    for (double Z = Bottom + StepCm * 0.5; Z < Top; Z += StepCm)
    {
        TArray<FVector2D> Band;
        for (const FVector& Point : Points)
            if (FMath::Abs(Point.Z - Z) <= StepCm * 0.6) Band.Emplace(Point.X, Point.Y);
        if (Band.Num() < 30) continue;
        // Principal directions of the ring, then robust extents (5th..95th percentile) on each.
        FVector2D Mean = FVector2D::ZeroVector;
        for (const FVector2D& P : Band) Mean += P;
        Mean /= Band.Num();
        double Sxx = 0, Sxy = 0, Syy = 0;
        for (const FVector2D& P : Band) { const FVector2D D = P - Mean; Sxx += D.X * D.X; Sxy += D.X * D.Y; Syy += D.Y * D.Y; }
        const double Theta = 0.5 * FMath::Atan2(2.0 * Sxy, Sxx - Syy);
        const FVector2D Wide(FMath::Cos(Theta), FMath::Sin(Theta)), Narrow(-Wide.Y, Wide.X);
        TArray<double> U, V;
        for (const FVector2D& P : Band) { U.Add(FVector2D::DotProduct(P - Mean, Wide)); V.Add(FVector2D::DotProduct(P - Mean, Narrow)); }
        const double U0 = PercentileOf(U, 0.05), U1 = PercentileOf(U, 0.95), V0 = PercentileOf(V, 0.05), V1 = PercentileOf(V, 0.95);
        double HalfWide = (U1 - U0) * 0.5, HalfNarrow = (V1 - V0) * 0.5;
        FVector2D Center = Mean + Wide * ((U0 + U1) * 0.5) + Narrow * ((V0 + V1) * 0.5);
        FVector2D Axis = Wide;
        if (HalfNarrow > HalfWide) { Swap(HalfWide, HalfNarrow); Axis = Narrow; }
        const FVector Mid(Center.X, Center.Y, Z);
        const FVector Along(Axis.X, Axis.Y, 0.0);
        int32 Best = 0;
        for (int32 I = 1; I < Attach.Num(); ++I)
            if (FMath::Abs(Attach[I].Value.Z - Z) < FMath::Abs(Attach[Best].Value.Z - Z)) Best = I;
        const FTransform BoneCS = RefComponentTransform(Ref, Attach[Best].Key);
        FGratiaSurfaceCapsule Capsule;
        Capsule.Bone = Ref.GetBoneName(Attach[Best].Key);
        Capsule.StartCm = BoneCS.InverseTransformPosition(Mid - Along * (HalfWide - HalfNarrow));
        Capsule.EndCm = BoneCS.InverseTransformPosition(Mid + Along * (HalfWide - HalfNarrow));
        Capsule.RadiusCm = float(FMath::Max(HalfNarrow, 1.0));
        Capsule.WrapAxis = BoneCS.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
        Capsules.Add(Capsule);
        UE_LOG(LogGratiaSoftBodySetup, Display, TEXT("BODY_SURFACE torso z=%.1f bone=%s vertices=%d section=%.1fx%.1f"),
            Z, *Capsule.Bone.ToString(), Band.Num(), HalfWide * 2, HalfNarrow * 2);
    }
    return !Capsules.IsEmpty();
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
