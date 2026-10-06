#include "GratiaSoftBodyInteraction.h"

#include "GratiaAnimInstance.h"
#include "GratiaBodySurface.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaSoftBody, Log, All);

namespace
{
FVector GratiaSoftBodyAxisVector(EGratiaBoneAxis Axis)
{
    switch (Axis)
    {
    case EGratiaBoneAxis::XNegative: return -FVector::XAxisVector;
    case EGratiaBoneAxis::YPositive: return FVector::YAxisVector;
    case EGratiaBoneAxis::YNegative: return -FVector::YAxisVector;
    case EGratiaBoneAxis::ZPositive: return FVector::ZAxisVector;
    case EGratiaBoneAxis::ZNegative: return -FVector::ZAxisVector;
    default: return FVector::XAxisVector;
    }
}

constexpr int32 GratiaPressSphereSlots = 24;
constexpr int32 GratiaPressZoneSlots = 4;

const TArray<FName>& GratiaPressNames(bool bZones)
{
    static const TArray<FName> Spheres = [] { TArray<FName> N; for (int32 I = 0; I < GratiaPressSphereSlots; ++I) N.Add(*FString::Printf(TEXT("Sphere%02d"), I)); return N; }();
    static const TArray<FName> Zones = [] { TArray<FName> N; for (int32 I = 0; I < GratiaPressZoneSlots; ++I) N.Add(*FString::Printf(TEXT("Zone%d"), I)); return N; }();
    return bZones ? Zones : Spheres;
}

FTransform GratiaSoftBodyRefTransform(const FReferenceSkeleton& Ref, int32 Index)
{
    FTransform Result = Ref.GetRefBonePose()[Index];
    for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
        Result *= Ref.GetRefBonePose()[Parent];
    return Result;
}
}

UGratiaSoftBodyInteraction::UGratiaSoftBodyInteraction()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGratiaSoftBodyInteraction::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    // Hand spheres and grabs must reach the anim proxy before the mesh evaluates.
    if (Character.IsValid() && Character->CharacterMesh) Character->CharacterMesh->AddTickPrerequisiteComponent(this);
}

bool UGratiaSoftBodyInteraction::IsEnabled() const
{
    return Character.IsValid() && Character->CharacterProfile && Character->Interaction && Character->CharacterMesh
        && Character->CharacterProfile->SoftBody.bEnabled && Character->CharacterProfile->Capabilities.bSecondaryPhysics
        && Character->Interaction->bBodyMotion && Character->Interaction->bPhysicalMotion && !bFault;
}

void UGratiaSoftBodyInteraction::ClearHands()
{
    for (FHand& Hand : Hands) Hand = FHand();
}

void UGratiaSoftBodyInteraction::ResetSoftBody()
{
    bFault = false; FaultReason.Reset();
    ClearHands();
    Squash.Reset();
    SquashDirection.Reset();
    SquashSide.Reset();
    SquashGrip.Reset();
    PushToAnimation(true);
}

void UGratiaSoftBodyInteraction::SetFault(const FString& Reason)
{
    if (bFault) return;
    bFault = true; FaultReason = Reason;
    UE_LOG(LogGratiaSoftBody, Error, TEXT("SOFT_BODY_FAULT %s; reset required"), *Reason);
    ClearHands();
    PushToAnimation(true);
}

void UGratiaSoftBodyInteraction::UpdateZones()
{
    Zones.Reset();
    if (!Character.IsValid() || !Character->CharacterProfile || !Character->CharacterMesh) return;
    const auto* Profile = Character->CharacterProfile.Get();
    auto* Mesh = Character->CharacterMesh.Get();
    const auto* Asset = Mesh->GetSkeletalMeshAsset();
    if (!Asset) return;
    if (LastProfile.Get() != Profile)
    {
        LastProfile = Profile;
        ColliderOffsets.Reset(); ColliderRadii.Reset();
        const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
        for (const FGratiaBodyColliderSphere& Sphere : Profile->SoftBody.BodyColliders)
        {
            const int32 Bone = Ref.FindBoneIndex(Sphere.Bone);
            if (Bone == INDEX_NONE) continue;
            ColliderOffsets.Emplace(Bone, GratiaSoftBodyRefTransform(Ref, Bone).InverseTransformPosition(Sphere.RefCenterCm));
            ColliderRadii.Add(Sphere.RadiusCm);
        }
    }
    const double Scale = Mesh->GetComponentTransform().GetScale3D().GetAbsMax();
    for (int32 ChainIndex = 0; ChainIndex < Profile->SoftBody.Chains.Num(); ++ChainIndex)
    {
        const FGratiaSoftBodyChain& Chain = Profile->SoftBody.Chains[ChainIndex];
        for (const FName Bone : Chain.RootBones)
        {
            const int32 Index = Mesh->GetBoneIndex(Bone);
            if (Index == INDEX_NONE) continue;
            const FTransform World = Mesh->GetBoneTransform(Index);
            // Bones are scaled by the FBX import; direction uses rotation only, lengths are cm.
            const FVector Direction = World.GetRotation().RotateVector(GratiaSoftBodyAxisVector(Chain.ForwardAxis));
            FZone Zone;
            Zone.Chain = Chain.Name; Zone.Bone = Bone; Zone.ChainIndex = ChainIndex;
            Zone.Tip = World.GetLocation() + Direction * Chain.DummyBoneLengthCm * Scale;
            Zone.Center = World.GetLocation() + Direction * Chain.DummyBoneLengthCm * Chain.ContactCenterAlongBone * Scale;
            Zone.Radius = Chain.ContactRadiusCm * Scale;
            Zone.Axis = GratiaSoftBodyAxisVector(Chain.ForwardAxis);
            Zone.Rotation = World.GetRotation();
            Zone.Pivot = World.GetLocation();
            // One surface for everything: press, squash and haptics use the same fitted shape
            // that stops the hand, so nothing starts before the palm reaches the skin.
            FVector SurfaceA, SurfaceB;
            float SurfaceRadius = 0.0f;
            if (Character->BodySurface && Character->BodySurface->GetBoneCapsule(Bone, SurfaceA, SurfaceB, SurfaceRadius) && SurfaceRadius > 0.5f)
            {
                Zone.Center = (SurfaceA + SurfaceB) * 0.5;
                Zone.Radius = SurfaceRadius;
            }
            if (Zone.Center.ContainsNaN() || Zone.Tip.ContainsNaN()) { SetFault(TEXT("non-finite soft body zone")); return; }
            Zones.Add(Zone);
        }
    }
    ZonesFrame = GFrameCounter;
}

void UGratiaSoftBodyInteraction::GetConformSpheres(const FVector& Point, float Range, TArray<FVector4>& OutWorld, float ZoneShrinkCm) const
{
    OutWorld.Reset();
    if (!IsEnabled() || !Character.IsValid()) return;
    const auto* Mesh = Character->CharacterMesh.Get();
    const double Scale = Mesh->GetComponentTransform().GetScale3D().GetAbsMax();
    const double Shrink = FMath::Max(0.0f, ZoneShrinkCm) * Scale;
    for (const FZone& Zone : Zones)
        if (FVector::Distance(Zone.Center, Point) < Range + Zone.Radius)
            OutWorld.Emplace(Zone.Center.X, Zone.Center.Y, Zone.Center.Z, FMath::Max(Zone.Radius * 0.35, Zone.Radius - Shrink));
    for (int32 I = 0; I < ColliderOffsets.Num(); ++I)
    {
        const FVector Center = Mesh->GetBoneTransform(ColliderOffsets[I].Key).TransformPosition(ColliderOffsets[I].Value);
        const double Radius = ColliderRadii[I] * Scale;
        if (FVector::Distance(Center, Point) < Range + Radius) OutWorld.Emplace(Center.X, Center.Y, Center.Z, Radius);
    }
}

void UGratiaSoftBodyInteraction::SubmitHand(bool bLeft, const FVector& Visible, const FVector& Raw, bool bAllowed, float Delta, float Grab,
    const TArray<FVector>& Fingers, bool bPoseOwned, FName SqueezeBone, float Squeeze)
{
    FHand& Hand = Hands[bLeft ? 0 : 1];
    const float Trigger = Grab;
    const bool bValid = bAllowed && IsEnabled() && !Visible.ContainsNaN() && !Raw.ContainsNaN() && FMath::IsFinite(Trigger)
        && FMath::IsFinite(Delta) && Delta > 0 && Delta <= 0.1f;
    if (!bValid) { Hand = FHand(); return; }
    if (ZonesFrame != GFrameCounter) UpdateZones();
    if (bFault) { Hand = FHand(); return; }
    const FGratiaSoftBodySettings& Settings = Character->CharacterProfile->SoftBody;
    const double Scale = Character->CharacterMesh->GetComponentTransform().GetScale3D().GetAbsMax();
    const bool bContinuous = Hand.bReady && FVector::Distance(Hand.Raw, Raw) <= Settings.MaxHandSpeedCmPerSecond * Delta;
    if (!bContinuous) { ReleaseGrab(Hand); Hand.bGrabArmed = false; }

    // Soft tissue yields: near a zone the hand may follow the raw controller deeper
    // than the rigid contact proxies allow, by at most SoftPressDepthCm.
    bool bNearZone = false;
    for (const FZone& Zone : Zones)
        bNearZone |= FVector::Distance(Raw, Zone.Center) < Zone.Radius + (Settings.PalmRadiusCm + Settings.SoftPressDepthCm) * Scale;
    const FVector Press = bNearZone && !bPoseOwned ? Visible + (Raw - Visible).GetClampedToMaxSize(Settings.SoftPressDepthCm * Scale) : Visible;
    Hand.bPress = bNearZone && !Press.Equals(Visible, 0.01);
    const FVector Grabber = bPoseOwned ? Raw : Press;

    // Penetration depth of palm and fingers into the deepest zone drives vibration.
    double Depth = 0;
    for (const FZone& Zone : Zones)
    {
        Depth = FMath::Max(Depth, Zone.Radius + Settings.PalmRadiusCm * Scale - FVector::Distance(Raw, Zone.Center));
        for (const FVector& Finger : Fingers)
            Depth = FMath::Max(Depth, Zone.Radius + Settings.FingerRadiusCm * Scale - FVector::Distance(Finger, Zone.Center));
    }
    Depth /= Scale;
    const bool bTouching = Depth > 0;
    if (bTouching && !Hand.bTouching) Hand.PulseRemaining = Settings.HapticPulseSeconds;
    Hand.bTouching = bTouching;
    Hand.PulseRemaining = FMath::Max(0.0f, Hand.PulseRemaining - Delta);
    const float T = FMath::Clamp(float(Depth) / Settings.HapticFullDepthCm, 0.0f, 1.0f);
    float Amplitude = Settings.HapticMaxAmplitude * FMath::Pow(T, Settings.HapticDepthExponent);
    if (Hand.PulseRemaining > 0) Amplitude = FMath::Max(Amplitude, Settings.HapticContactPulse);
    Hand.DepthCm = float(FMath::Max(Depth, 0.0));
    Hand.Amplitude = Settings.bHaptics ? FMath::Clamp(Amplitude, 0.0f, 1.0f) : 0.0f;
    Hand.Frequency = Hand.Amplitude > 0 ? FMath::Lerp(Settings.HapticMinFrequency, Settings.HapticMaxFrequency, T) : 0.0f;

    // Trigger grab, VRChat PhysBone style: the nearest grabbable tip follows the hand.
    bool bPressed = false;
    if (Trigger <= 0.25f) { ReleaseGrab(Hand); Hand.bGrabArmed = true; }
    else if (Trigger >= 0.65f && Hand.bGrabArmed && bContinuous) { bPressed = true; Hand.bGrabArmed = false; }
    if (bPressed && Hand.GrabBone.IsNone())
    {
        double Best = DBL_MAX;
        for (const FZone& Zone : Zones)
        {
            const auto& Chain = Character->CharacterProfile->SoftBody.Chains[Zone.ChainIndex];
            // Gap between the hand (palm sphere or any finger) and the zone surface.
            double Gap = FVector::Distance(Press, Zone.Center) - Zone.Radius - Settings.PalmRadiusCm * Scale;
            for (const FVector& Finger : Fingers)
                Gap = FMath::Min(Gap, FVector::Distance(Finger + (Press - Visible), Zone.Center) - Zone.Radius - Settings.FingerRadiusCm * Scale);
            const FHand& Other = Hands[bLeft ? 1 : 0];
            if (!Chain.bAllowGrab || Other.GrabBone == Zone.Bone || Gap > Settings.GrabRadiusCm * Scale || Gap >= Best) continue;
            Best = Gap; Hand.GrabBone = Zone.Bone; Hand.GrabOffset = Zone.Tip - Grabber;
        }
        if (Hand.GrabBone.IsNone())
        {
            GrabSpringBone(Hand, Hands[bLeft ? 1 : 0], Press, Visible, Fingers);
            if (!Hand.GrabBone.IsNone()) Hand.GrabOffset = Character->CharacterMesh->GetBoneLocation(Hand.GrabBone) - Grabber;
        }
        UE_LOG(LogGratiaSoftBody, Display, TEXT("SOFT_BODY_GRAB hand=%s bone=%s spring=%d"), bLeft ? TEXT("L") : TEXT("R"), *Hand.GrabBone.ToString(), Hand.GrabSpringChain);
    }
    if (!Hand.GrabBone.IsNone())
    {
        // A spring bone follows the hand until the hand leaves its reach (the strand slips out).
        if (Hand.GrabSpringChain != INDEX_NONE)
        {
            if (FVector::Distance(Grabber + Hand.GrabOffset, Character->CharacterMesh->GetBoneLocation(Hand.GrabBone)) > Settings.GrabBreakDistanceCm * Scale)
                ReleaseGrab(Hand);
        }
        else
        {
            const FZone* Zone = Zones.FindByPredicate([&Hand](const FZone& Value) { return Value.Bone == Hand.GrabBone; });
            if (!Zone || FVector::Distance(Grabber + Hand.GrabOffset, Zone->Tip) > Settings.GrabBreakDistanceCm * Scale) ReleaseGrab(Hand);
        }
    }
    Hand.Visible = Visible; Hand.Raw = Raw; Hand.Press = Press; Hand.Fingers = Fingers; Hand.Grabber = Grabber;
    Hand.SqueezeBone = SqueezeBone; Hand.Squeeze = Squeeze;
    // Fingers were measured on the visible hand; the pressed hand is drawn at the press point.
    for (FVector& Finger : Hand.Fingers) Finger += Press - Visible;
    Hand.SubmitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    Hand.bReady = true;
}

void UGratiaSoftBodyInteraction::GrabSpringBone(FHand& Hand, const FHand& Other, const FVector& Press, const FVector& Visible, const TArray<FVector>& Fingers)
{
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    const auto* Anim = Cast<UGratiaAnimInstance>(Mesh->GetAnimInstance());
    if (Profile->SpringChains.IsEmpty() || !Anim || !Mesh->GetSkeletalMeshAsset()) return;
    const FReferenceSkeleton& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    // Chain of every bone below a running, grabbable spring root (parents precede children).
    TArray<int32> ChainOf;
    ChainOf.Init(INDEX_NONE, Ref.GetNum());
    TBitArray<> Root(false, Ref.GetNum());
    for (int32 Chain = 0; Chain < Profile->SpringChains.Num(); ++Chain)
    {
        const FGratiaSpringChain& Definition = Profile->SpringChains[Chain];
        if (!Definition.bAllowGrab || (Anim->SpringGroups >> Definition.Group & 1) == 0) continue;
        for (const FName Bone : Definition.RootBones)
            if (const int32 Index = Ref.FindBoneIndex(Bone); Index != INDEX_NONE) { ChainOf[Index] = Chain; Root[Index] = true; }
    }
    const FGratiaSoftBodySettings& Settings = Profile->SoftBody;
    const double Scale = Mesh->GetComponentTransform().GetScale3D().GetAbsMax();
    double Best = Profile->SpringGrabRadiusCm * Scale;
    for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
    {
        const int32 Parent = Ref.GetParentIndex(Index);
        if (ChainOf[Index] == INDEX_NONE && Parent != INDEX_NONE) ChainOf[Index] = ChainOf[Parent];
        // Roots stay on the animated pose: only the bones below them can be held.
        if (ChainOf[Index] == INDEX_NONE || Root[Index]) continue;
        const FName Name = Ref.GetBoneName(Index);
        if (Other.GrabBone == Name) continue;
        const FVector Location = Mesh->GetBoneLocation(Name);
        const double Thickness = Profile->SpringChains[ChainOf[Index]].CollisionRadiusCm * Scale;
        double Gap = FVector::Distance(Press, Location) - Settings.PalmRadiusCm * Scale - Thickness;
        for (const FVector& Finger : Fingers)
            Gap = FMath::Min(Gap, FVector::Distance(Finger + (Press - Visible), Location) - Settings.FingerRadiusCm * Scale - Thickness);
        if (Gap >= Best) continue;
        Best = Gap; Hand.GrabBone = Name; Hand.GrabSpringChain = ChainOf[Index];
    }
}

void UGratiaSoftBodyInteraction::PushToAnimation(bool bReset)
{
    if (!Character.IsValid() || !Character->CharacterMesh) return;
    auto* Anim = Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance());
    if (!Anim) return;
    const bool bEnabled = IsEnabled();
    Anim->bSoftBody = bEnabled;
    Anim->SoftBodyInput.HandSpheres.Reset();
    Anim->SoftBodyInput.Grabs.Reset();
    Anim->SoftBodyInput.Scales.Reset();
    Anim->SoftBodyInput.bReset |= bReset || bEnabled != bWasEnabled;
    bWasEnabled = bEnabled;
    if (!bEnabled) return;
    const FGratiaSoftBodySettings& Settings = Character->CharacterProfile->SoftBody;
    Anim->SoftBodyInput.SoftPushFraction = FMath::Clamp(Settings.HandPushFraction, 0.0f, 1.0f);
    for (const FZone& Zone : Zones)
    {
        const FVector2D* State = Squash.Find(Zone.Bone);
        if (!State || FMath::Abs(State->X) < 0.002f) continue;
        Anim->SoftBodyInput.Scales.Emplace(Zone.Bone, GetSquashScale(Zone.Bone));
    }
    const FTransform Component = Character->CharacterMesh->GetComponentTransform();
    const double Scale = Component.GetScale3D().GetAbsMax();
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    for (const FHand& Hand : Hands)
    {
        if (!Hand.bReady || Now - Hand.SubmitTime > 0.1) continue;
        auto Add = [&](const FVector& World, float RadiusCm)
        {
            const FVector Local = Component.InverseTransformPosition(World);
            Anim->SoftBodyInput.HandSpheres.Emplace(Local.X, Local.Y, Local.Z, RadiusCm);
        };
        // Soft parts collide with reduced spheres (the surface yields first, then the whole part
        // swings); spring chains with the full hand. A hand that holds or squeezes a part does not
        // also push it: the grab pulled the part in while the spheres threw it out (shaking).
        if (Hand.GrabBone.IsNone() && Hand.SqueezeBone.IsNone())
        {
            Add(Hand.Press, Settings.PalmRadiusCm);
            for (const FVector& Finger : Hand.Fingers) Add(Finger, Settings.FingerRadiusCm);
        }
        if (Hand.GrabBone.IsNone()) continue;
        if (Hand.GrabSpringChain != INDEX_NONE)
        {
            if (!Character->CharacterProfile->SpringChains.IsValidIndex(Hand.GrabSpringChain)) continue;
            const FGratiaSpringChain& Spring = Character->CharacterProfile->SpringChains[Hand.GrabSpringChain];
            FGratiaSoftBodyGrab Grab;
            Grab.RootBone = Hand.GrabBone;
            Grab.TargetCS = Component.InverseTransformPosition(Hand.Grabber + Hand.GrabOffset);
            Grab.Movement = Spring.GrabMovement;
            Grab.MaxStretchCm = Spring.MaxGrabStretchCm / Scale;
            Anim->SoftBodyInput.Grabs.Add(Grab);
            continue;
        }
        const FZone* Zone = Zones.FindByPredicate([&Hand](const FZone& Value) { return Value.Bone == Hand.GrabBone; });
        if (!Zone) continue;
        const auto& Chain = Settings.Chains[Zone->ChainIndex];
        FGratiaSoftBodyGrab Grab;
        Grab.RootBone = Hand.GrabBone;
        Grab.TargetCS = Component.InverseTransformPosition(Hand.Grabber + Hand.GrabOffset);
        Grab.Movement = Chain.GrabMovement;
        Grab.MaxStretchCm = Chain.MaxGrabStretchCm / Scale;
        Anim->SoftBodyInput.Grabs.Add(Grab);
    }
}

void UGratiaSoftBodyInteraction::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    if (!Character.IsValid()) return;
    if (!IsEnabled()) { ClearHands(); Squash.Reset(); PushToAnimation(false); PushPress(); return; }
    if (ZonesFrame != GFrameCounter) UpdateZones();
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    for (FHand& Hand : Hands) if (Hand.bReady && Now - Hand.SubmitTime > 0.1) Hand = FHand();
    UpdateSquash(Delta);
    PushToAnimation(false);
    PushPress();
    if (Now >= NextDiagnosticTime)
    {
        NextDiagnosticTime = Now + 2.0;
        UE_LOG(LogGratiaSoftBody, Display, TEXT("SOFT_BODY %s"), *GetDiagnostics());
    }
}

void UGratiaSoftBodyInteraction::UpdateSquash(float Delta)
{
    const auto* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    if (!Profile || !FMath::IsFinite(Delta) || Delta <= 0.0f) return;
    const FGratiaSoftBodySettings& Settings = Profile->SoftBody;
    const double Scale = Character->CharacterMesh->GetComponentTransform().GetScale3D().GetAbsMax();
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const float Step = FMath::Min(Delta, 0.05f);
    for (const FZone& Zone : Zones)
    {
        // Deepest hand sphere (palm or finger) inside the zone, relative to the zone radius;
        // the direction from the zone centre to it is where the part is squeezed.
        double Depth = 0.0, Extent = Zone.Radius;
        FVector Deepest = FVector::ZeroVector;
        float Cup = 0.0f;
        const FHand* Cupping = nullptr;
        auto Consider = [&](const FVector& Point, double RadiusCm)
        {
            const double Value = Zone.Radius + RadiusCm * Scale - FVector::Distance(Point, Zone.Center);
            if (Value > Depth) { Depth = Value; Deepest = Point; }
        };
        for (const FHand& Hand : Hands)
        {
            if (!Hand.bReady || Now - Hand.SubmitTime > 0.1) continue;
            Consider(Hand.Press, Settings.PalmRadiusCm);
            for (const FVector& Finger : Hand.Fingers) Consider(Finger, Settings.FingerRadiusCm);
            if (Hand.SqueezeBone == Zone.Bone && Hand.Squeeze >= Cup) { Cup = FMath::Clamp(Hand.Squeeze, 0.0f, 1.0f); Cupping = &Hand; }
        }
        if (Depth > 0.0)
        {
            const FVector Local = Zone.Rotation.UnrotateVector(Deepest - Zone.Center).GetSafeNormal();
            if (!Local.IsNearlyZero() && !Cupping)
            {
                FVector& Direction = SquashDirection.FindOrAdd(Zone.Bone, Local);
                Direction = FMath::Lerp(Direction, Local, FMath::Min(1.0f, Step * 20.0f)).GetSafeNormal();
                if (Direction.IsNearlyZero()) Direction = Local;
            }
            // Scaled about the pivot, the skin under the hand moves by squash x its distance from
            // the pivot along the press: squash = depth / distance keeps the skin on the hand.
            const FVector Outward = (Deepest - Zone.Center).GetSafeNormal();
            Extent = FMath::Max(0.5 * Zone.Radius, FVector::DotProduct(Zone.Center - Zone.Pivot, Outward) + Zone.Radius);
        }
        // A cupping hand squeezes like a ball in the hand: toward the palm and across the curl of
        // the fingers, the flesh going out along the knuckle line (user: a trigger squeeze felt
        // like pressing straight in). A press without a cup still flattens along the press.
        float& Grip = SquashGrip.FindOrAdd(Zone.Bone);
        if (Cupping)
        {
            const FVector Palm = Zone.Rotation.UnrotateVector(Cupping->Press - Zone.Center).GetSafeNormal();
            if (!Palm.IsNearlyZero())
            {
                FVector& Direction = SquashDirection.FindOrAdd(Zone.Bone, Palm);
                Direction = FMath::Lerp(Direction, Palm, FMath::Min(1.0f, Step * 20.0f)).GetSafeNormal();
                if (Direction.IsNearlyZero()) Direction = Palm;
                // Fingers: thumb, index, middle, ring, pinky (distal joint and tip each).
                FVector Knuckles = Cupping->Fingers.Num() >= 10 ? Zone.Rotation.UnrotateVector(
                    Cupping->Fingers[8] + Cupping->Fingers[9] - Cupping->Fingers[2] - Cupping->Fingers[3]) : FVector::ZeroVector;
                Knuckles = FVector::VectorPlaneProject(Knuckles, Direction).GetSafeNormal();
                if (Knuckles.IsNearlyZero()) { FVector Other; Direction.FindBestAxisVectors(Knuckles, Other); }
                FVector& Side = SquashSide.FindOrAdd(Zone.Bone, Knuckles);
                // The knuckle line has no sign: follow the closer orientation.
                const FVector Toward = FVector::DotProduct(Side, Knuckles) < 0 ? -Knuckles : Knuckles;
                Side = FVector::VectorPlaneProject(FMath::Lerp(Side, Toward, FMath::Min(1.0f, Step * 20.0f)), Direction).GetSafeNormal();
                if (Side.IsNearlyZero()) Side = Knuckles;
            }
            Grip = FMath::FInterpTo(Grip, 1.0f, Step, 12.0f);
        }
        else if (Depth > 0.0) Grip = FMath::FInterpTo(Grip, 0.0f, Step, 12.0f);
        // A cupping hand squeezes by its trigger/grip; a press by its depth.
        const float Target = FMath::Min(Settings.SquashAmount,
            FMath::Max(Settings.SquashResponse * float(Depth / FMath::Max(Extent, 0.1)), Settings.SquashAmount * Cup));
        // Underdamped spring: soft follow while pressed, a short wobble on release.
        FVector2D& State = Squash.FindOrAdd(Zone.Bone);
        constexpr float Stiffness = 260.0f, Damping = 11.0f;
        for (int32 I = 0; I < 4; ++I)
        {
            const float Dt = Step / 4.0f;
            State.Y += (Stiffness * (Target - State.X) - Damping * State.Y) * Dt;
            State.X = FMath::Clamp(State.X + State.Y * Dt, -0.25f, 0.8f);
        }
        if (!FMath::IsFinite(State.X) || !FMath::IsFinite(State.Y)) State = FVector2D::ZeroVector;
    }
}

FVector UGratiaSoftBodyInteraction::GetSquashScale(FName Bone) const
{
    const FVector2D* State = Squash.Find(Bone);
    const auto* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    if (!State || !Profile) return FVector::OneVector;
    const FZone* Zone = Zones.FindByPredicate([Bone](const FZone& Value) { return Value.Bone == Bone; });
    const FVector* Stored = SquashDirection.Find(Bone);
    const FVector Direction = Stored ? *Stored : Zone ? Zone->Axis : FVector::XAxisVector;
    // Press: compressed along the press direction, volume-preserving bulge across it.
    // Cup (ball in the hand): palm and finger curl both close in, the flesh goes out along the
    // knuckle line (at most 1.5x). Frame: press N, curl C, knuckles L; per bone axis the factors
    // blend by the squared frame components (exact for axis-aligned frames).
    const float X = float(State->X);
    const float Bulge = FMath::Clamp(Profile->SoftBody.SquashBulge, 0.0f, 1.0f);
    const float Along = FMath::Clamp(1.0f - X, 0.4f, 1.4f);
    const float Across = 1.0f + (1.0f / FMath::Sqrt(Along) - 1.0f) * Bulge;
    const float Ball = FMath::Clamp(1.0f - 0.75f * X, 0.4f, 1.4f);
    const float Out = FMath::Min(1.5f, 1.0f + (1.0f / Ball - 1.0f) * Bulge);
    const float* GripState = SquashGrip.Find(Bone);
    const float Grip = GripState ? FMath::Clamp(*GripState, 0.0f, 1.0f) : 0.0f;
    const FVector* StoredSide = SquashSide.Find(Bone);
    FVector Side = StoredSide ? FVector::VectorPlaneProject(*StoredSide, Direction).GetSafeNormal() : FVector::ZeroVector;
    if (Side.IsNearlyZero()) { FVector Other; Direction.FindBestAxisVectors(Side, Other); }
    const FVector Curl = FVector::CrossProduct(Direction, Side).GetSafeNormal();
    const float ScaleN = FMath::Lerp(Along, Ball, Grip), ScaleC = FMath::Lerp(Across, Ball, Grip), ScaleL = FMath::Lerp(Across, Out, Grip);
    FVector Result;
    for (int32 Axis = 0; Axis < 3; ++Axis)
        Result[Axis] = FMath::Pow(ScaleN, float(FMath::Square(Direction[Axis]))) * FMath::Pow(ScaleC, float(FMath::Square(Curl[Axis])))
            * FMath::Pow(ScaleL, float(FMath::Square(Side[Axis])));
    return Result.ContainsNaN() ? FVector::OneVector : Result;
}

void UGratiaSoftBodyInteraction::PushPress()
{
    const auto* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    UMaterialParameterCollection* Collection = Profile ? Profile->SoftBody.PressCollection.Get() : nullptr;
    UMaterialParameterCollectionInstance* Instance = Collection && GetWorld() ? GetWorld()->GetParameterCollectionInstance(Collection) : nullptr;
    PressSpheres = 0;
    if (!Instance) return;
    const TArray<FName>& SphereNames = GratiaPressNames(false);
    const TArray<FName>& ZoneNames = GratiaPressNames(true);
    const FLinearColor Empty(0.0f, 0.0f, -1.0e5f, 0.0f);
    if (!bPressDeformation || !IsEnabled() || !Character->CharacterMesh)
    {
        // Disabled press: one clear, then the materials stay undeformed.
        if (bPressCleared) return;
        for (const FName& Name : SphereNames) Instance->SetVectorParameterValue(Name, Empty);
        for (const FName& Name : ZoneNames) Instance->SetVectorParameterValue(Name, Empty);
        Instance->SetVectorParameterValue(TEXT("Config"), FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
        bPressCleared = true;
        return;
    }
    bPressCleared = false;
    const FGratiaSoftBodySettings& Settings = Profile->SoftBody;
    const double Scale = Character->CharacterMesh->GetComponentTransform().GetScale3D().GetAbsMax();
    const double Now = GetWorld()->GetTimeSeconds();
    int32 Slot = 0;
    auto Add = [&](const FVector& Point, double Radius)
    {
        if (Slot < SphereNames.Num() && !Point.ContainsNaN())
            Instance->SetVectorParameterValue(SphereNames[Slot++], FLinearColor(Point.X, Point.Y, Point.Z, Radius));
    };
    for (const FHand& Hand : Hands)
    {
        if (!Hand.bReady || Now - Hand.SubmitTime > 0.1) continue;
        Add(Hand.Press, Settings.PressPalmRadiusCm * Scale);
        for (const FVector& Finger : Hand.Fingers) Add(Finger, Settings.FingerRadiusCm * Scale);
    }
    PressSpheres = Slot;
    for (; Slot < SphereNames.Num(); ++Slot) Instance->SetVectorParameterValue(SphereNames[Slot], Empty);
    // The material has a few zone-mask slots: the zones nearest the hands get them (a dent only
    // appears under a hand), so any number of soft zones can dent.
    TArray<int32, TInlineAllocator<16>> Order;
    for (int32 Index = 0; Index < Zones.Num(); ++Index) Order.Add(Index);
    auto HandGap = [&](const FZone& Zone)
    {
        double Best = DBL_MAX;
        for (const FHand& Hand : Hands)
            if (Hand.bReady && Now - Hand.SubmitTime <= 0.1) Best = FMath::Min(Best, FVector::Distance(Hand.Press, Zone.Center) - Zone.Radius);
        return Best;
    };
    if (Zones.Num() > ZoneNames.Num())
        Order.Sort([&](int32 A, int32 B) { return HandGap(Zones[A]) < HandGap(Zones[B]); });
    for (int32 ZoneSlot = 0; ZoneSlot < ZoneNames.Num(); ++ZoneSlot)
    {
        if (!Order.IsValidIndex(ZoneSlot)) { Instance->SetVectorParameterValue(ZoneNames[ZoneSlot], Empty); continue; }
        const FZone& Zone = Zones[Order[ZoneSlot]];
        Instance->SetVectorParameterValue(ZoneNames[ZoneSlot], FLinearColor(Zone.Center.X, Zone.Center.Y, Zone.Center.Z, Zone.Radius * Settings.PressZoneScale));
    }
    Instance->SetVectorParameterValue(TEXT("Config"), FLinearColor(Settings.PressSoftnessCm * Scale, 1.0f,
        Settings.PressZoneFalloffCm * Scale, FMath::Clamp(Settings.PressStrength, 0.0f, 1.0f)));
}

bool UGratiaSoftBodyInteraction::RunChecks(FString& Failure)
{
    if (!Character.IsValid() || !Character->CharacterProfile || !Character->CharacterMesh)
    { Failure = TEXT("Soft body requires a character, profile and mesh"); return false; }
    if (!Character->CharacterProfile->SoftBody.bEnabled)
    {
        UE_LOG(LogGratiaSoftBody, Display, TEXT("SOFT_BODY_CHECK skip=profile capability unavailable"));
        return true;
    }
    UpdateZones();
    if (bFault) { Failure = TEXT("Soft body fault: ") + FaultReason; return false; }
    int32 Expected = 0;
    for (const auto& Chain : Character->CharacterProfile->SoftBody.Chains) Expected += Chain.RootBones.Num();
    if (Zones.Num() != Expected) { Failure = FString::Printf(TEXT("Soft body zones %d of %d root bones resolved"), Zones.Num(), Expected); return false; }
    const auto* Anim = Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance());
    if (IsEnabled() && (!Anim || Anim->GetActiveSoftBodyChainCount() != Character->CharacterProfile->SoftBody.Chains.Num()))
    { Failure = TEXT("KawaiiPhysics chains are not evaluated by the character animation"); return false; }
    return true;
}

FString UGratiaSoftBodyInteraction::GetDiagnostics() const
{
    const auto* Anim = Character.IsValid() && Character->CharacterMesh ? Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance()) : nullptr;
    return FString::Printf(TEXT("zones=%d chains=%d L depth=%.1fcm vib=%.2f grab=%s press=%s R depth=%.1fcm vib=%.2f grab=%s press=%s state=%s%s"),
        Zones.Num(), Anim ? Anim->GetActiveSoftBodyChainCount() : 0,
        Hands[0].DepthCm, Hands[0].Amplitude, *Hands[0].GrabBone.ToString(), Hands[0].bPress ? TEXT("y") : TEXT("n"),
        Hands[1].DepthCm, Hands[1].Amplitude, *Hands[1].GrabBone.ToString(), Hands[1].bPress ? TEXT("y") : TEXT("n"),
        bFault ? TEXT("FAULT ") : IsEnabled() ? TEXT("running") : TEXT("disabled"), *FaultReason);
}
