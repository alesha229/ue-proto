#include "GratiaSoftBodyInteraction.h"

#include "GratiaAnimInstance.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaSoftBody, Log, All);

namespace
{
FVector AxisVector(EGratiaBoneAxis Axis)
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

FTransform RefComponentTransform(const FReferenceSkeleton& Ref, int32 Index)
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
            ColliderOffsets.Emplace(Bone, RefComponentTransform(Ref, Bone).InverseTransformPosition(Sphere.RefCenterCm));
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
            const FVector Direction = World.GetRotation().RotateVector(AxisVector(Chain.ForwardAxis));
            FZone Zone;
            Zone.Chain = Chain.Name; Zone.Bone = Bone; Zone.ChainIndex = ChainIndex;
            Zone.Tip = World.GetLocation() + Direction * Chain.DummyBoneLengthCm * Scale;
            Zone.Center = World.GetLocation() + Direction * Chain.DummyBoneLengthCm * Chain.ContactCenterAlongBone * Scale;
            Zone.Radius = Chain.ContactRadiusCm * Scale;
            if (Zone.Center.ContainsNaN() || Zone.Tip.ContainsNaN()) { SetFault(TEXT("non-finite soft body zone")); return; }
            Zones.Add(Zone);
        }
    }
    ZonesFrame = GFrameCounter;
}

void UGratiaSoftBodyInteraction::GetConformSpheres(const FVector& Point, float Range, TArray<FVector4>& OutWorld) const
{
    OutWorld.Reset();
    if (!IsEnabled() || !Character.IsValid()) return;
    for (const FZone& Zone : Zones)
        if (FVector::Distance(Zone.Center, Point) < Range + Zone.Radius) OutWorld.Emplace(Zone.Center.X, Zone.Center.Y, Zone.Center.Z, Zone.Radius);
    const auto* Mesh = Character->CharacterMesh.Get();
    const double Scale = Mesh->GetComponentTransform().GetScale3D().GetAbsMax();
    for (int32 I = 0; I < ColliderOffsets.Num(); ++I)
    {
        const FVector Center = Mesh->GetBoneTransform(ColliderOffsets[I].Key).TransformPosition(ColliderOffsets[I].Value);
        const double Radius = ColliderRadii[I] * Scale;
        if (FVector::Distance(Center, Point) < Range + Radius) OutWorld.Emplace(Center.X, Center.Y, Center.Z, Radius);
    }
}

void UGratiaSoftBodyInteraction::SubmitHand(bool bLeft, const FVector& Visible, const FVector& Raw, bool bAllowed, float Delta, float Trigger,
    const TArray<FVector>& Fingers)
{
    FHand& Hand = Hands[bLeft ? 0 : 1];
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
    const FVector Press = bNearZone ? Visible + (Raw - Visible).GetClampedToMaxSize(Settings.SoftPressDepthCm * Scale) : Visible;
    Hand.bPress = bNearZone && !Press.Equals(Visible, 0.01);

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
            const double Distance = FVector::Distance(Press, Zone.Center);
            const FHand& Other = Hands[bLeft ? 1 : 0];
            if (!Chain.bAllowGrab || Other.GrabBone == Zone.Bone || Distance > Zone.Radius + Settings.GrabRadiusCm * Scale || Distance >= Best) continue;
            Best = Distance; Hand.GrabBone = Zone.Bone; Hand.GrabOffset = Zone.Tip - Press;
        }
        UE_LOG(LogGratiaSoftBody, Display, TEXT("SOFT_BODY_GRAB hand=%s bone=%s"), bLeft ? TEXT("L") : TEXT("R"), *Hand.GrabBone.ToString());
    }
    if (!Hand.GrabBone.IsNone())
    {
        const FZone* Zone = Zones.FindByPredicate([&Hand](const FZone& Value) { return Value.Bone == Hand.GrabBone; });
        if (!Zone || FVector::Distance(Press + Hand.GrabOffset, Zone->Tip) > Settings.GrabBreakDistanceCm * Scale) ReleaseGrab(Hand);
    }
    Hand.Visible = Visible; Hand.Raw = Raw; Hand.Press = Press; Hand.Fingers = Fingers;
    Hand.SubmitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    Hand.bReady = true;
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
    Anim->SoftBodyInput.bReset |= bReset || bEnabled != bWasEnabled;
    bWasEnabled = bEnabled;
    if (!bEnabled) return;
    const FGratiaSoftBodySettings& Settings = Character->CharacterProfile->SoftBody;
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
        Add(Hand.Press, Settings.PalmRadiusCm);
        for (const FVector& Finger : Hand.Fingers) Add(Finger, Settings.FingerRadiusCm);
        if (Hand.GrabBone.IsNone()) continue;
        const FZone* Zone = Zones.FindByPredicate([&Hand](const FZone& Value) { return Value.Bone == Hand.GrabBone; });
        if (!Zone) continue;
        const auto& Chain = Settings.Chains[Zone->ChainIndex];
        FGratiaSoftBodyGrab Grab;
        Grab.RootBone = Hand.GrabBone;
        Grab.TargetCS = Component.InverseTransformPosition(Hand.Press + Hand.GrabOffset);
        Grab.Movement = Chain.GrabMovement;
        Grab.MaxStretchCm = Chain.MaxGrabStretchCm / Scale;
        Anim->SoftBodyInput.Grabs.Add(Grab);
    }
}

void UGratiaSoftBodyInteraction::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    if (!Character.IsValid()) return;
    if (!IsEnabled()) { ClearHands(); PushToAnimation(false); return; }
    if (ZonesFrame != GFrameCounter) UpdateZones();
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    for (FHand& Hand : Hands) if (Hand.bReady && Now - Hand.SubmitTime > 0.1) Hand = FHand();
    PushToAnimation(false);
    if (Now >= NextDiagnosticTime)
    {
        NextDiagnosticTime = Now + 2.0;
        UE_LOG(LogGratiaSoftBody, Display, TEXT("SOFT_BODY %s"), *GetDiagnostics());
    }
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
