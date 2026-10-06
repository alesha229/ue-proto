#include "GratiaCharacterProfile.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Sound/SoundBase.h"

int32 FGratiaQualityProfile::GetGroupCap(uint8 Group) const
{
    switch (Group)
    {
    case 1: return FMath::Max(0, HairCap);
    case 2: return FMath::Max(0, ClothCap);
    case 3: return FMath::Max(0, BodyCap);
    case 4: return FMath::Max(0, EarCap);
    default: return 0;
    }
}

UGratiaCharacterProfile::UGratiaCharacterProfile()
{
    FGratiaQualityProfile Low; Low.Name = TEXT("Low"); Low.HairCap = 0; Low.ClothCap = 0; Low.TotalBodyCap = 15;
    FGratiaQualityProfile Medium; Medium.Name = TEXT("Medium");
    FGratiaQualityProfile High; High.Name = TEXT("High"); High.HairCap = High.ClothCap = High.BodyCap = High.EarCap = 150; High.TotalBodyCap = 256;
    QualityProfiles = {Low, Medium, High};
    for (uint8 Group = 1; Group <= 4; ++Group)
    {
        FGratiaSecondaryGroupSettings Settings;
        Settings.Group = Group;
        if (Group == 2) { Settings.OrientationStrength = 260.0f; Settings.SpringLimitDegrees = 4.0f; Settings.HeadInertiaScale = 0.0f; }
        if (Group == 3)
        {
            Settings.BlendWeight = 0.25f; Settings.OrientationStrength = 650.0f;
            Settings.AngularVelocityStrength = 75.0f; Settings.MaxAngularForce = 120.0f;
            Settings.SpringLimitDegrees = 1.0f; Settings.HeadInertiaScale = 0.0f;
        }
        if (Group == 4) { Settings.OrientationStrength = 450.0f; Settings.SpringLimitDegrees = 4.0f; }
        SecondaryGroups.Add(Settings);
    }
}

FPrimaryAssetId UGratiaCharacterProfile::GetPrimaryAssetId() const
{
    return FPrimaryAssetId(TEXT("CharacterProfile"), ProfileId.IsNone() ? GetFName() : ProfileId);
}

FName UGratiaCharacterProfile::ResolveBone(FName Semantic) const
{
    const FName* Found = SemanticBones.Find(Semantic);
    return Found ? *Found : NAME_None;
}

FName UGratiaCharacterProfile::ResolveMorph(FName Semantic) const
{
    const FName* Found = SemanticMorphs.Find(Semantic);
    return Found ? *Found : NAME_None;
}

FGratiaQualityProfile UGratiaCharacterProfile::GetQualitySettings(int32 Quality) const
{
    return QualityProfiles.IsEmpty() ? FGratiaQualityProfile() : QualityProfiles[FMath::Clamp(Quality, 0, QualityProfiles.Num() - 1)];
}

FGratiaSecondaryGroupSettings UGratiaCharacterProfile::GetSecondaryGroupSettings(uint8 Group) const
{
    const FGratiaSecondaryGroupSettings* Found = SecondaryGroups.FindByPredicate(
        [Group](const FGratiaSecondaryGroupSettings& Value) { return Value.Group == Group; });
    return Found ? *Found : FGratiaSecondaryGroupSettings();
}

const FGratiaSecondaryBoneDefinition* UGratiaCharacterProfile::FindSecondaryBone(FName Bone) const
{
    return SecondaryBones.FindByPredicate([Bone](const FGratiaSecondaryBoneDefinition& Value) { return Value.Bone == Bone; });
}

bool UGratiaCharacterProfile::ValidateProfile(TArray<FString>& Errors, TArray<FString>& Warnings) const
{
    Errors.Reset(); Warnings.Reset();
    if (!Mesh) { Errors.Add(TEXT("Mesh is required.")); return false; }
    if (ProfileId.IsNone()) Warnings.Add(TEXT("ProfileId is empty; the asset name is used as its primary identifier."));
    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    if (ExpectedBoneCount > 0 && Ref.GetNum() != ExpectedBoneCount)
        Errors.Add(FString::Printf(TEXT("Bone count is %d; this profile expects %d."), Ref.GetNum(), ExpectedBoneCount));
    if (ExpectedMorphCount > 0 && Mesh->GetMorphTargets().Num() != ExpectedMorphCount)
        Errors.Add(FString::Printf(TEXT("Morph count is %d; this profile expects %d."), Mesh->GetMorphTargets().Num(), ExpectedMorphCount));
    if (ForwardAxis.ContainsNaN() || UpAxis.ContainsNaN() || ForwardAxis.IsNearlyZero() || UpAxis.IsNearlyZero()
        || FMath::Abs(FVector::DotProduct(ForwardAxis.GetSafeNormal(), UpAxis.GetSafeNormal())) > 0.01)
        Errors.Add(TEXT("ForwardAxis and UpAxis must be finite, non-zero, orthogonal directions."));
    if (!FMath::IsFinite(MaxHeadYawDegrees) || MaxHeadYawDegrees < 0.0f || MaxHeadYawDegrees > 90.0f
        || !FMath::IsFinite(MaxHeadPitchDegrees) || MaxHeadPitchDegrees < 0.0f || MaxHeadPitchDegrees > 90.0f
        || !FMath::IsFinite(GazeInterpSpeed) || GazeInterpSpeed < 0.0f)
        Errors.Add(TEXT("Gaze angle/interpolation settings are invalid."));
    for (const auto& Pair : SemanticBones)
        if (Pair.Value.IsNone() || Ref.FindBoneIndex(Pair.Value) == INDEX_NONE)
            Errors.Add(FString::Printf(TEXT("Semantic bone %s refers to missing bone %s."), *Pair.Key.ToString(), *Pair.Value.ToString()));
    for (const auto& Pair : SemanticMorphs)
        if (Pair.Value.IsNone() || !Mesh->FindMorphTarget(Pair.Value))
            Errors.Add(FString::Printf(TEXT("Semantic morph %s refers to missing morph %s."), *Pair.Key.ToString(), *Pair.Value.ToString()));

    if (Capabilities.bBlink && (ResolveMorph(TEXT("BlinkLeft")).IsNone() || ResolveMorph(TEXT("BlinkRight")).IsNone()))
        Errors.Add(TEXT("Blink capability requires BlinkLeft and BlinkRight morph mappings."));
    if (Capabilities.bGaze && ResolveBone(TEXT("Head")).IsNone())
        Errors.Add(TEXT("Gaze capability requires the Head semantic bone."));
    if (Capabilities.bFacialReactions && ResolveMorph(TEXT("Smile")).IsNone())
        Errors.Add(TEXT("FacialReactions capability requires at least the Smile semantic morph."));
    if (Capabilities.bReactionAnimations && !ReactSoft && ReactionClips.IsEmpty())
        Errors.Add(TEXT("ReactionAnimations capability requires a default soft clip or explicit zone routing."));
    if (!Idle) Warnings.Add(TEXT("No idle clip: the model remains in its reference pose unless AnimationClass supplies animation."));
    if (const auto* BlueprintClass = Cast<UAnimBlueprintGeneratedClass>(AnimationClass.Get()))
        if (BlueprintClass->GetTargetSkeleton() != Mesh->GetSkeleton())
            Errors.Add(TEXT("AnimationClass targets another skeleton. Retarget the Animation Blueprint before assigning it."));
    for (const UAnimSequence* Clip : {Idle.Get(), Arms.Get(), Head.Get(), ReactSoft.Get(), ReactBright.Get()})
        if (Clip && Clip->GetSkeleton() != Mesh->GetSkeleton())
            Errors.Add(FString::Printf(TEXT("Animation %s uses a different skeleton. Retarget it before assigning."), *Clip->GetName()));
    for (const auto& Pair : ReactionClips)
        if (Pair.Key.IsNone() || !Pair.Value || Pair.Value->GetSkeleton() != Mesh->GetSkeleton())
            Errors.Add(FString::Printf(TEXT("Reaction routing %s requires a clip on this profile's skeleton."), *Pair.Key.ToString()));
    if (StrongReactionClip && StrongReactionClip->GetSkeleton() != Mesh->GetSkeleton())
        Errors.Add(TEXT("StrongReactionClip uses a different skeleton."));
    for (const auto& Pair : MoodReactionClips)
        if (Pair.Key.IsNone() || !Pair.Value || Pair.Value->GetSkeleton() != Mesh->GetSkeleton())
            Errors.Add(FString::Printf(TEXT("Mood reaction %s requires a clip on this profile's skeleton."), *Pair.Key.ToString()));
    for (int32 Index = 0; Index < PerformanceClips.Num(); ++Index)
    {
        for (int32 Part = 0; Part < PerformanceClips[Index].NumParts(); ++Part)
        {
            const UAnimSequence* Clip = PerformanceClips[Index].GetPart(Part);
            if (!Clip || Clip->GetSkeleton() != Mesh->GetSkeleton() || Clip->GetPlayLength() <= 0.0f)
                Errors.Add(FString::Printf(TEXT("Performance clip %d (%s) part %d requires a non-empty clip on this profile's skeleton."),
                    Index, *PerformanceClips[Index].Name.ToString(), Part));
        }
        const FGratiaPerformanceScene& Scene = PerformanceClips[Index].Scene;
        if (Scene.Music && (!FMath::IsFinite(Scene.Music->GetDuration()) || Scene.Music->GetDuration() <= 0.0f))
            Errors.Add(FString::Printf(TEXT("Performance %s music has no finite duration."), *PerformanceClips[Index].Name.ToString()));
        if (Scene.PartnerMesh)
        {
            const FReferenceSkeleton& PartnerRef = Scene.PartnerMesh->GetRefSkeleton();
            for (const FGratiaPartnerAim& Aim : Scene.PartnerPose)
                if (PartnerRef.FindBoneIndex(Aim.Bone) == INDEX_NONE || PartnerRef.FindBoneIndex(Aim.Child) == INDEX_NONE
                    || Aim.From.ContainsNaN() || Aim.To.ContainsNaN() || Aim.From.Equals(Aim.To, 0.1))
                    Errors.Add(FString::Printf(TEXT("Performance %s partner aim %s->%s is not on %s or has no direction."),
                        *PerformanceClips[Index].Name.ToString(), *Aim.Bone.ToString(), *Aim.Child.ToString(), *Scene.PartnerMesh->GetName()));
        }
        else if (!Scene.PartnerPose.IsEmpty())
            Errors.Add(FString::Printf(TEXT("Performance %s has a partner pose without a partner mesh."), *PerformanceClips[Index].Name.ToString()));
        if (Scene.bHasViewpoint && (Scene.Viewpoint.ContainsNaN() || !Scene.Viewpoint.IsRotationNormalized()))
            Errors.Add(FString::Printf(TEXT("Performance %s viewpoint is not a valid transform."), *PerformanceClips[Index].Name.ToString()));
    }
    auto ValidateReactionSound = [&Errors](const USoundBase* Sound, const FString& Label)
    {
        if (!IsValid(Sound)) { Errors.Add(Label + TEXT(" has no valid sound resource.")); return; }
        if (!FMath::IsFinite(Sound->GetDuration()) || Sound->IsLooping())
            Errors.Add(Label + TEXT(" must be a finite, non-looping acknowledgement."));
    };
    if (DefaultReactionSound) ValidateReactionSound(DefaultReactionSound.Get(), TEXT("DefaultReactionSound"));
    for (const auto& Pair : ReactionSounds)
    {
        if (Pair.Key.IsNone()) Errors.Add(TEXT("Reaction sound routing requires a non-empty zone name or Default key."));
        ValidateReactionSound(Pair.Value.Get(), FString::Printf(TEXT("Reaction sound %s"), *Pair.Key.ToString()));
    }
    if (!Capabilities.bSound && (DefaultReactionSound || !ReactionSounds.IsEmpty()))
        Warnings.Add(TEXT("Reaction sounds are assigned but the Sound capability is disabled."));

    TSet<FName> ZoneNames;
    if (Capabilities.bContacts && ContactZones.IsEmpty()) Errors.Add(TEXT("Contacts capability is enabled without character contact zones."));
    for (const auto& Zone : ContactZones)
    {
        if (Zone.Name.IsNone() || ZoneNames.Contains(Zone.Name)) Errors.Add(TEXT("Contact zone names must be unique and non-empty."));
        ZoneNames.Add(Zone.Name);
        if (ResolveBone(Zone.BoneSemantic).IsNone())
            Errors.Add(FString::Printf(TEXT("Contact zone %s has no mapping for %s."), *Zone.Name.ToString(), *Zone.BoneSemantic.ToString()));
        if (!FMath::IsFinite(Zone.Radius) || Zone.Radius <= 0.0f || Zone.Offset.ContainsNaN())
            Errors.Add(FString::Printf(TEXT("Contact zone %s has invalid geometry."), *Zone.Name.ToString()));
        if (Capabilities.bReactionAnimations && !ReactSoft && !ReactionClips.Contains(TEXT("Default"))
            && !ReactionClips.Contains(Zone.Name))
            Warnings.Add(FString::Printf(TEXT("Contact zone %s has no animation routing; its animation response is unavailable."), *Zone.Name.ToString()));
    }
    const float Times[] = {ContactSettings.HoldSeconds, ContactSettings.CooldownSeconds, ContactSettings.SingleTouchSeconds,
        ContactSettings.ReactionSeconds, ContactSettings.ReactionMinimumIntervalSeconds, ContactSettings.CaptionSeconds, ContactSettings.ContactRecoverySeconds,
        ContactSettings.ReactionInterpSpeed, ContactSettings.ImpulseDecaySpeed};
    for (float Time : Times) if (!FMath::IsFinite(Time) || Time < 0.0f) Errors.Add(TEXT("Contact durations must be finite and non-negative."));
    if (!FMath::IsFinite(ContactSettings.HandRadiusCm) || ContactSettings.HandRadiusCm <= 0.0f
        || !FMath::IsFinite(ContactSettings.TouchPaddingCm) || ContactSettings.TouchPaddingCm < 0.0f
        || !FMath::IsFinite(ContactSettings.HoverPaddingCm) || ContactSettings.HoverPaddingCm < ContactSettings.TouchPaddingCm)
        Errors.Add(TEXT("Contact hand/padding dimensions must be finite; hover padding must include touch padding."));
    const float PositiveSettings[] = {ContactSettings.DemoIntervalSeconds, ContactSettings.MaxHandSpeedCmPerSecond,
        ContactSettings.ImpulseSpeedCmPerSecond, ContactSettings.StrongReactionSpeedCmPerSecond, ContactSettings.MaxHandCorrectionCm};
    for (float Value : PositiveSettings)
        if (!FMath::IsFinite(Value) || Value <= 0.0f) Errors.Add(TEXT("Contact speed/recovery limits must be finite and positive."));
    if (ContactSettings.CaptionOffset.ContainsNaN() || !FMath::IsFinite(ContactSettings.HoldReactionWeight)
        || ContactSettings.HoldReactionWeight < 0.0f || ContactSettings.HoldReactionWeight > 1.0f)
        Errors.Add(TEXT("Caption offset or hold reaction weight is invalid."));
    TSet<FName> ProxyNames;
    if (Capabilities.bContacts && CollisionProxies.IsEmpty())
        Errors.Add(TEXT("Contacts capability requires separately authored hand collision proxies."));
    for (const auto& Proxy : CollisionProxies)
    {
        if (Proxy.Name.IsNone() || ProxyNames.Contains(Proxy.Name)) Errors.Add(TEXT("Collision proxy names must be unique and non-empty."));
        ProxyNames.Add(Proxy.Name);
        if (ResolveBone(Proxy.StartBoneSemantic).IsNone()
            || (Proxy.Shape == EGratiaCollisionProxyShape::Capsule && ResolveBone(Proxy.EndBoneSemantic).IsNone()))
            Errors.Add(FString::Printf(TEXT("Collision proxy %s has missing semantic endpoints."), *Proxy.Name.ToString()));
        if (Proxy.Shape != EGratiaCollisionProxyShape::Sphere && Proxy.Shape != EGratiaCollisionProxyShape::Capsule)
            Errors.Add(TEXT("Unsupported collision proxy shape."));
        if (!FMath::IsFinite(Proxy.Radius) || Proxy.Radius <= 0.0f || Proxy.StartOffset.ContainsNaN() || Proxy.EndOffset.ContainsNaN())
            Errors.Add(FString::Printf(TEXT("Collision proxy %s has invalid geometry."), *Proxy.Name.ToString()));
    }

    UPhysicsAsset* Physics = PhysicsAsset ? PhysicsAsset.Get() : Mesh->GetPhysicsAsset();
    if (Capabilities.bSecondaryPhysics && (!Physics || SecondaryBones.IsEmpty()))
        Errors.Add(TEXT("SecondaryPhysics capability requires a PhysicsAsset and secondary bone definitions."));
    if (Physics)
    {
        if (ExpectedPhysicsBodyCount > 0 && Physics->SkeletalBodySetups.Num() != ExpectedPhysicsBodyCount)
            Errors.Add(TEXT("Physics body count differs from this profile's regression expectation."));
        if (ExpectedConstraintCount > 0 && Physics->ConstraintSetup.Num() != ExpectedConstraintCount)
            Errors.Add(TEXT("Physics constraint count differs from this profile's regression expectation."));
    }
    TSet<FName> SecondaryNames;
    for (const auto& Bone : SecondaryBones)
    {
        if (Bone.Bone.IsNone() || SecondaryNames.Contains(Bone.Bone) || Ref.FindBoneIndex(Bone.Bone) == INDEX_NONE)
            Errors.Add(FString::Printf(TEXT("Secondary bone %s is absent or duplicated."), *Bone.Bone.ToString()));
        SecondaryNames.Add(Bone.Bone);
        if (Bone.Group < 1 || Bone.Group > 4 || !FMath::IsFinite(Bone.RestLengthCm) || Bone.RestLengthCm < 0.0f)
            Errors.Add(FString::Printf(TEXT("Secondary bone %s has invalid group/rest length."), *Bone.Bone.ToString()));
        if (Capabilities.bSecondaryPhysics && Bone.bSafeSimulation && Physics && Physics->FindBodyIndex(Bone.Bone) == INDEX_NONE)
            Errors.Add(FString::Printf(TEXT("Simulated secondary bone %s has no physics body."), *Bone.Bone.ToString()));
    }
    if (Capabilities.bLocalSprings && SecondaryBones.IsEmpty()) Errors.Add(TEXT("LocalSprings capability requires secondary bone definitions."));
    if (QualityProfiles.Num() != 3) Errors.Add(TEXT("Supply three quality profiles in Low/Medium/High order."));
    for (const auto& Quality : QualityProfiles)
        if (Quality.HairCap < 0 || Quality.ClothCap < 0 || Quality.BodyCap < 0 || Quality.EarCap < 0 || Quality.TotalBodyCap < 0)
            Errors.Add(TEXT("Quality body caps cannot be negative."));
    TSet<uint8> Groups;
    for (const auto& Settings : SecondaryGroups)
    {
        if (Settings.Group < 1 || Settings.Group > 4 || Groups.Contains(Settings.Group))
            Errors.Add(TEXT("Secondary group settings must have unique group numbers from 1 to 4."));
        Groups.Add(Settings.Group);
        const float NonNegative[] = {Settings.OrientationStrength, Settings.AngularVelocityStrength, Settings.MaxAngularForce,
            Settings.SpringLimitDegrees, Settings.SpringStiffness, Settings.SpringDamping};
        for (float Value : NonNegative) if (!FMath::IsFinite(Value) || Value < 0.0f) Errors.Add(TEXT("Secondary motion settings must be finite and non-negative."));
        if (!FMath::IsFinite(Settings.BlendWeight) || Settings.BlendWeight < 0.0f || Settings.BlendWeight > 1.0f || !FMath::IsFinite(Settings.HeadInertiaScale))
            Errors.Add(TEXT("Secondary motion blend/inertia settings are invalid."));
        if (Settings.SpringLocalAxis.ContainsNaN() || Settings.SpringLocalAxis.IsNearlyZero())
            Errors.Add(TEXT("SpringLocalAxis must be a finite, non-zero direction."));
    }
    for (const auto& Bone : SecondaryBones)
        if (!Groups.Contains(Bone.Group)) Errors.Add(FString::Printf(TEXT("Secondary group %d has no settings."), Bone.Group));
    if (bRequirePlantedIdle && (ResolveBone(TEXT("Root")).IsNone() || ResolveBone(TEXT("LeftFoot")).IsNone() || ResolveBone(TEXT("RightFoot")).IsNone()))
        Errors.Add(TEXT("Planted-idle verification requires Root, LeftFoot and RightFoot semantic bones."));
    const float PlantedLimits[] = {MaxIdleFootDriftCm, MaxIdleFootRotationDegrees, MaxIdleRootDriftCm, MaxIdleRootRotationDegrees};
    for (float Value : PlantedLimits)
        if (!FMath::IsFinite(Value) || Value < 0.0f) Errors.Add(TEXT("Planted-idle limits must be finite and non-negative."));
    if (!FMath::IsFinite(MaxSecondaryCollisionSizeCm) || MaxSecondaryCollisionSizeCm <= 0.0f
        || !FMath::IsFinite(MaxPhysicsTargetDeviationCm) || MaxPhysicsTargetDeviationCm <= 0.0f
        || !FMath::IsFinite(PhysicsSafetyCheckSeconds) || PhysicsSafetyCheckSeconds <= 0.0f)
        Errors.Add(TEXT("Physics safety limits must be finite and positive."));
    const float HandPositive[] = {HandPhysics.RadiusCm, HandPhysics.MaxSpeedCmPerSecond, HandPhysics.MaxTravelCm, HandPhysics.GrabBreakDistanceCm};
    for (float Value : HandPositive)
        if (!FMath::IsFinite(Value) || Value <= 0.0f) Errors.Add(TEXT("Hand physics geometry/speed limits must be finite and positive."));
    const float HandNonnegative[] = {HandPhysics.Stiffness, HandPhysics.Damping, HandPhysics.MaxForce, HandPhysics.GrabStiffness, HandPhysics.GrabDamping};
    for (float Value : HandNonnegative)
        if (!FMath::IsFinite(Value) || Value < 0.0f) Errors.Add(TEXT("Hand physics force settings must be finite and nonnegative."));
    if (SoftBody.bEnabled)
    {
        if (SoftBody.Chains.IsEmpty()) Errors.Add(TEXT("Soft body enabled without chains."));
        for (const FGratiaSoftBodyChain& Chain : SoftBody.Chains)
        {
            if (Chain.Name.IsNone() || Chain.RootBones.IsEmpty()) Errors.Add(TEXT("Soft body chain needs a name and root bones."));
            for (const FName Bone : Chain.RootBones)
                if (Ref.FindBoneIndex(Bone) == INDEX_NONE) Errors.Add(FString::Printf(TEXT("Soft body root bone %s is missing."), *Bone.ToString()));
            const float Values[] = {Chain.DummyBoneLengthCm, Chain.ContactRadiusCm, Chain.CollisionRadiusCm, Chain.Damping, Chain.Stiffness,
                Chain.WorldDampingLocation, Chain.WorldDampingRotation, Chain.LimitAngleDegrees, Chain.GravityScale, Chain.GrabMovement, Chain.MaxGrabStretchCm};
            for (float Value : Values)
                if (!FMath::IsFinite(Value) || Value < 0) Errors.Add(TEXT("Soft body chain values must be finite and nonnegative."));
        }
        for (const FGratiaBodyColliderSphere& Sphere : SoftBody.BodyColliders)
            if (Ref.FindBoneIndex(Sphere.Bone) == INDEX_NONE || !FMath::IsFinite(Sphere.RadiusCm) || Sphere.RadiusCm <= 0 || Sphere.RefCenterCm.ContainsNaN())
                Errors.Add(TEXT("Soft body collider sphere is invalid."));
        const float Positive[] = {SoftBody.PalmRadiusCm, SoftBody.FingerRadiusCm, SoftBody.GrabBreakDistanceCm,
            SoftBody.MaxHandSpeedCmPerSecond, SoftBody.HapticFullDepthCm, SoftBody.HapticDepthExponent};
        for (float Value : Positive)
            if (!FMath::IsFinite(Value) || Value <= 0) Errors.Add(TEXT("Soft body hand/haptic values must be finite and positive."));
    }
    for (const FGratiaSurfaceCapsule& Capsule : BodySurface)
    {
        if (Ref.FindBoneIndex(Capsule.Bone) == INDEX_NONE)
            Errors.Add(FString::Printf(TEXT("Body surface capsule refers to missing bone %s."), *Capsule.Bone.ToString()));
        if (Capsule.StartCm.ContainsNaN() || Capsule.EndCm.ContainsNaN() || !FMath::IsFinite(Capsule.RadiusCm) || Capsule.RadiusCm <= 0)
            Errors.Add(FString::Printf(TEXT("Body surface capsule %s needs finite ends and a positive radius."), *Capsule.Bone.ToString()));
    }
    TSet<int32> SpringRoots;
    for (const FGratiaSpringChain& Chain : SpringChains)
    {
        if (Chain.Name.IsNone() || Chain.RootBones.IsEmpty()) Errors.Add(TEXT("Spring chain needs a name and root bones."));
        if (Chain.Group != 1 && Chain.Group != 2 && Chain.Group != 4)
            Errors.Add(FString::Printf(TEXT("Spring chain %s group must be 1 (hair), 2 (clothing) or 4 (ears/tail)."), *Chain.Name.ToString()));
        for (const FName Bone : Chain.RootBones)
        {
            const int32 Index = Ref.FindBoneIndex(Bone);
            if (Index == INDEX_NONE) Errors.Add(FString::Printf(TEXT("Spring chain root bone %s is missing."), *Bone.ToString()));
            else SpringRoots.Add(Index);
            if (SoftBody.Chains.ContainsByPredicate([Bone](const FGratiaSoftBodyChain& Soft) { return Soft.RootBones.Contains(Bone); }))
                Errors.Add(FString::Printf(TEXT("Bone %s is both a soft body and a spring chain root."), *Bone.ToString()));
        }
        const float Values[] = {Chain.TipLengthCm, Chain.Damping, Chain.Stiffness, Chain.WorldDampingLocation, Chain.WorldDampingRotation,
            Chain.CollisionRadiusCm, Chain.LimitAngleDegrees, Chain.GravityScale, Chain.GrabMovement, Chain.MaxGrabStretchCm};
        for (float Value : Values)
            if (!FMath::IsFinite(Value) || Value < 0) Errors.Add(FString::Printf(TEXT("Spring chain %s values must be finite and nonnegative."), *Chain.Name.ToString()));
    }
    // A bone below a spring root is simulated by KawaiiPhysics; a Chaos body on it would fight it.
    for (const FGratiaSecondaryBoneDefinition& Bone : SecondaryBones)
    {
        if (!Bone.bSafeSimulation) continue;
        for (int32 Index = Ref.FindBoneIndex(Bone.Bone); Index != INDEX_NONE; Index = Ref.GetParentIndex(Index))
            if (SpringRoots.Contains(Index))
            { Errors.Add(FString::Printf(TEXT("Secondary bone %s is in a spring chain; turn off its Chaos simulation."), *Bone.Bone.ToString())); break; }
    }
    if (!FMath::IsFinite(SpringGrabRadiusCm) || SpringGrabRadiusCm < 0 || !FMath::IsFinite(SpringMinColliderRadiusCm) || SpringMinColliderRadiusCm < 0)
        Errors.Add(TEXT("Spring grab and collider radii must be finite and nonnegative."));
    if (!SpringChains.IsEmpty() && (!FMath::IsFinite(SpringHandRadiusCm) || SpringHandRadiusCm <= 0)) Errors.Add(TEXT("Spring hand radius must be positive."));
    const float HandSurfaceValues[] = {HandSurface.PalmContactRadiusCm, HandSurface.PalmThicknessCm, HandSurface.AdaptDistanceCm,
        HandSurface.GripReachCm, HandSurface.GripBreakDistanceCm, HandSurface.GripBlendSeconds};
    for (const float Value : HandSurfaceValues)
        if (!FMath::IsFinite(Value) || Value < 0) Errors.Add(TEXT("Hand surface distances and times must be finite and non-negative."));
    if (HandSurface.GripReleaseInput >= HandSurface.GripStartInput)
        Errors.Add(TEXT("Hand surface grip release input must be below the start input."));
    return Errors.IsEmpty();
}
