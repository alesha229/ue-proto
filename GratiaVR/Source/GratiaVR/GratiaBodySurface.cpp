#include "GratiaBodySurface.h"

#include "GratiaCharacterProfile.h"
#include "GratiaContactSolver.h"
#include "GratiaHandAnimInstance.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaSoftBodyInteraction.h"
#include "Components/SkeletalMeshComponent.h"

UGratiaBodySurface::UGratiaBodySurface()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UGratiaBodySurface::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
}

void UGratiaBodySurface::Resolve() const
{
    const UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    if (ResolvedProfile.Get() == Profile && (Profile == nullptr || !Resolved.IsEmpty() || Profile->BodySurface.IsEmpty())) return;
    ResolvedProfile = Profile;
    Resolved.Reset();
    if (!Profile || !Character->CharacterMesh) return;
    for (const FGratiaSurfaceCapsule& Capsule : Profile->BodySurface)
    {
        const int32 Bone = Character->CharacterMesh->GetBoneIndex(Capsule.Bone);
        if (Bone == INDEX_NONE || Capsule.StartCm.ContainsNaN() || Capsule.EndCm.ContainsNaN() || !(Capsule.RadiusCm > 0)) continue;
        Resolved.Add({Bone, Capsule.Bone, Capsule.StartCm, Capsule.EndCm, Capsule.WrapAxis.GetSafeNormal(), Capsule.RadiusCm});
    }
}

bool UGratiaBodySurface::HasSurface() const
{
    Resolve();
    return !Resolved.IsEmpty();
}

void UGratiaBodySurface::WorldCapsule(const FResolved& Capsule, FVector& A, FVector& B, float& Radius) const
{
    const USkeletalMeshComponent* Mesh = Character->CharacterMesh.Get();
    const FTransform Bone = Mesh->GetBoneTransform(Capsule.Bone);
    A = Bone.TransformPosition(Capsule.Start);
    B = Bone.TransformPosition(Capsule.End);
    Radius = Capsule.Radius * float(Mesh->GetComponentTransform().GetScale3D().GetAbsMax());
}

FVector UGratiaBodySurface::WorldWrap(const FResolved& Capsule) const
{
    if (Capsule.Wrap.IsNearlyZero()) return FVector::ZeroVector;
    return Character->CharacterMesh->GetBoneTransform(Capsule.Bone).TransformVectorNoScale(Capsule.Wrap).GetSafeNormal();
}

void UGratiaBodySurface::Evaluate(const FVector& Point, const FVector& A, const FVector& B, float Radius, FGratiaSurfaceHit& Out)
{
    const FVector Closest = FMath::ClosestPointOnSegment(Point, A, B);
    const FVector Offset = Point - Closest;
    const double Distance = Offset.Size();
    const FVector Axis = FVector::DistSquared(A, B) > 0.25 ? (B - A).GetSafeNormal() : FVector::ZeroVector;
    FVector Normal = Distance > 1.0e-4 ? Offset / Distance : FVector::ZeroVector;
    if (Normal.IsZero()) Normal = Axis.IsZero() ? FVector::UpVector : FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal();
    if (Normal.IsZero()) Normal = FVector::ForwardVector;
    Out.Point = Closest + Normal * Radius;
    Out.Normal = Normal;
    Out.Axis = Axis;
    Out.WrapAxis = Axis;
    Out.SegmentA = A; Out.SegmentB = B;
    Out.Radius = Radius;
    Out.Gap = float(Distance - Radius);
}

bool UGratiaBodySurface::FindNearest(const FVector& Point, float MaxGapCm, FGratiaSurfaceHit& Out, bool bIncludeSoftZones) const
{
    Resolve();
    if (!Character.IsValid() || !Character->CharacterMesh || Point.ContainsNaN()) return false;
    bool bFound = false;
    FGratiaSurfaceHit Hit;
    for (const FResolved& Capsule : Resolved)
    {
        FVector A, B; float Radius;
        WorldCapsule(Capsule, A, B, Radius);
        Evaluate(Point, A, B, Radius, Hit);
        if (Hit.Gap <= MaxGapCm && (!bFound || Hit.Gap < Out.Gap))
        {
            Out = Hit; Out.Bone = Capsule.Name; Out.bSoftZone = false; bFound = true;
            const FVector Wrap = WorldWrap(Capsule);
            if (!Wrap.IsZero()) Out.WrapAxis = Wrap;
        }
    }
    if (bIncludeSoftZones && Character->SoftBodyInteraction && Character->SoftBodyInteraction->IsEnabled())
    {
        for (const auto& Zone : Character->SoftBodyInteraction->GetZones())
        {
            Evaluate(Point, Zone.Center, Zone.Center, Zone.Radius, Hit);
            if (Hit.Gap <= MaxGapCm && (!bFound || Hit.Gap < Out.Gap))
            {
                Out = Hit; Out.Bone = Zone.Bone; Out.bSoftZone = true; bFound = true;
            }
        }
    }
    return bFound;
}

bool UGratiaBodySurface::FindOnBone(FName Bone, const FVector& Point, FGratiaSurfaceHit& Out) const
{
    Resolve();
    // A long part may have several capsules on its bone: the nearest one.
    bool bFound = false;
    FGratiaSurfaceHit Hit;
    for (const FResolved& Capsule : Resolved)
    {
        if (Capsule.Name != Bone) continue;
        FVector A, B; float Radius;
        WorldCapsule(Capsule, A, B, Radius);
        Evaluate(Point, A, B, Radius, Hit);
        if (bFound && Hit.Gap >= Out.Gap) continue;
        Out = Hit; Out.Bone = Bone; Out.bSoftZone = false;
        const FVector Wrap = WorldWrap(Capsule);
        if (!Wrap.IsZero()) Out.WrapAxis = Wrap;
        bFound = true;
    }
    return bFound;
}

bool UGratiaBodySurface::GetBoneCapsule(FName Bone, FVector& A, FVector& B, float& Radius) const
{
    Resolve();
    for (const FResolved& Capsule : Resolved)
        if (Capsule.Name == Bone) { WorldCapsule(Capsule, A, B, Radius); return true; }
    return false;
}

void UGratiaBodySurface::GatherContactShapes(float ExtraRadiusCm, TArray<FGratiaContactShape>& Out) const
{
    Resolve();
    if (!Character.IsValid() || !Character->CharacterMesh) return;
    for (const FResolved& Capsule : Resolved)
    {
        FVector A, B; float Radius;
        WorldCapsule(Capsule, A, B, Radius);
        Out.Add(FGratiaContactShape::Capsule(A, B, Radius + ExtraRadiusCm));
    }
    if (Character->SoftBodyInteraction && Character->SoftBodyInteraction->IsEnabled())
        for (const auto& Zone : Character->SoftBodyInteraction->GetZones())
            Out.Add(FGratiaContactShape::Sphere(Zone.Center, Zone.Radius + ExtraRadiusCm));
}

void UGratiaBodySurface::GatherConformShapes(const FVector& Point, float RangeCm, TArray<FVector4>& Spheres,
    TArray<FGratiaConformCapsule>& Capsules, float SoftZoneShrinkCm) const
{
    Spheres.Reset(); Capsules.Reset();
    Resolve();
    if (!Character.IsValid() || !Character->CharacterMesh) return;
    for (const FResolved& Capsule : Resolved)
    {
        FGratiaConformCapsule World;
        WorldCapsule(Capsule, World.A, World.B, World.Radius);
        if (FMath::PointDistToSegment(Point, World.A, World.B) < RangeCm + World.Radius) Capsules.Add(World);
    }
    if (Character->SoftBodyInteraction && Character->SoftBodyInteraction->IsEnabled())
    {
        const double Shrink = FMath::Max(0.0f, SoftZoneShrinkCm);
        for (const auto& Zone : Character->SoftBodyInteraction->GetZones())
            if (FVector::Distance(Point, Zone.Center) < RangeCm + Zone.Radius)
                Spheres.Emplace(Zone.Center.X, Zone.Center.Y, Zone.Center.Z, FMath::Max(Zone.Radius * 0.35, Zone.Radius - Shrink));
    }
}

void UGratiaBodySurface::PalmWorld(const FTransform& Hand, const FGratiaPalmFrame& Palm, FVector& Point, FVector& Normal, FVector& Finger)
{
    Point = Hand.TransformPosition(Palm.Point);
    Normal = Hand.TransformVectorNoScale(Palm.Normal).GetSafeNormal();
    Finger = Hand.TransformVectorNoScale(Palm.Finger).GetSafeNormal();
}

FTransform UGratiaBodySurface::SolveWrap(const FTransform& Hand, const FGratiaPalmFrame& Palm, const FGratiaSurfaceHit& Hit, float PalmThicknessCm,
    float FingerClearanceCm)
{
    FVector Point, Normal, Finger;
    PalmWorld(Hand, Palm, Point, Normal, Finger);
    const FVector TargetNormal = -Hit.Normal;
    // Around a limb the fingers run across its wrap axis; keep the side closer to the current hand.
    FVector TargetFinger = Hit.WrapAxis.IsNearlyZero()
        ? FVector::VectorPlaneProject(Finger, TargetNormal).GetSafeNormal()
        : FVector::CrossProduct(Hit.WrapAxis, Hit.Normal).GetSafeNormal();
    if (TargetFinger.IsNearlyZero()) TargetFinger = FVector::CrossProduct(TargetNormal, FVector::UpVector).GetSafeNormal();
    if (TargetFinger.IsNearlyZero()) TargetFinger = FVector::CrossProduct(TargetNormal, FVector::ForwardVector).GetSafeNormal();
    if (FVector::DotProduct(TargetFinger, Finger) < 0) TargetFinger = -TargetFinger;
    const FQuat Current = FRotationMatrix::MakeFromXZ(Finger, Normal).ToQuat();
    const FQuat Target = FRotationMatrix::MakeFromXZ(TargetFinger, TargetNormal).ToQuat();
    FTransform Out = Hand;
    Out.SetRotation((Target * Current.Inverse() * Hand.GetRotation()).GetNormalized());
    const FVector PalmPoint = Hit.Point + Hit.Normal * PalmThicknessCm;
    Out.SetLocation(PalmPoint - Out.TransformVector(Palm.Point));
    // The open hand is relaxed (fingers bent to the palm side): rest it on its lowest point so
    // nothing starts inside the part; the fingers then curl until they touch it.
    for (int32 Iteration = 0; Iteration < 4; ++Iteration)
    {
        double Lift = 0.0;
        for (const FVector& Rest : Palm.RestPoints)
        {
            const FVector World = Out.TransformPosition(Rest);
            Lift = FMath::Max(Lift, Hit.Radius + FingerClearanceCm - FMath::PointDistToSegment(World, Hit.SegmentA, Hit.SegmentB));
        }
        if (Lift <= 0.01) break;
        Out.AddToTranslation(Hit.Normal * Lift);
    }
    return Out.ContainsNaN() ? Hand : Out;
}

FTransform UGratiaBodySurface::LeanToSurface(const FTransform& Hand, const FGratiaPalmFrame& Palm, const FGratiaSurfaceHit& Hit, float Weight,
    float MaxDegrees, float PalmThicknessCm)
{
    FVector Point, Normal, Finger;
    PalmWorld(Hand, Palm, Point, Normal, Finger);
    Weight = FMath::Clamp(Weight, 0.0f, 1.0f);
    // Only a palm that already faces the body leans onto it.
    if (Weight <= 0 || FVector::DotProduct(Normal, -Hit.Normal) < -0.2) return Hand;
    const FQuat Full = FQuat::FindBetweenNormals(Normal, -Hit.Normal);
    const float Angle = Full.GetAngle();
    const float Limit = FMath::DegreesToRadians(FMath::Max(0.0f, MaxDegrees));
    const float Fraction = Angle * Weight > Limit && Angle > 1.0e-4f ? Limit / Angle : Weight;
    FTransform Out = Hand;
    Out.SetRotation((FQuat::Slerp(FQuat::Identity, Full, Fraction) * Hand.GetRotation()).GetNormalized());
    // Draw the palm the rest of the way onto the surface (at most 2.5 cm).
    const FVector PalmPoint = Point - Hit.Normal * (Weight * FMath::Clamp(Hit.Gap - PalmThicknessCm, 0.0f, 2.5f));
    Out.SetLocation(PalmPoint - Out.TransformVector(Palm.Point));
    return Out.ContainsNaN() ? Hand : Out;
}

FString UGratiaBodySurface::GetDiagnostics() const
{
    Resolve();
    return FString::Printf(TEXT("body surface capsules=%d"), Resolved.Num());
}
