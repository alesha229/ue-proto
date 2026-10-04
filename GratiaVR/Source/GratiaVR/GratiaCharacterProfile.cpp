#include "GratiaCharacterProfile.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

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
    if (Capabilities.bReactionAnimations && !ReactSoft && !ReactBright)
        Errors.Add(TEXT("ReactionAnimations capability is enabled without a reaction clip."));
    if (!Idle) Warnings.Add(TEXT("No idle clip: the model remains in its reference pose unless AnimationClass supplies animation."));
    for (const UAnimSequence* Clip : {Idle.Get(), Arms.Get(), Head.Get(), ReactSoft.Get(), ReactBright.Get()})
        if (Clip && Clip->GetSkeleton() != Mesh->GetSkeleton())
            Errors.Add(FString::Printf(TEXT("Animation %s uses a different skeleton. Retarget it before assigning."), *Clip->GetName()));

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
    }
    const float Times[] = {ContactSettings.HoldSeconds, ContactSettings.CooldownSeconds, ContactSettings.SingleTouchSeconds,
        ContactSettings.ReactionSeconds, ContactSettings.CaptionSeconds};
    for (float Time : Times) if (!FMath::IsFinite(Time) || Time < 0.0f) Errors.Add(TEXT("Contact durations must be finite and non-negative."));
    if (!FMath::IsFinite(ContactSettings.HandRadiusCm) || ContactSettings.HandRadiusCm <= 0.0f
        || !FMath::IsFinite(ContactSettings.TouchPaddingCm) || ContactSettings.TouchPaddingCm < 0.0f
        || !FMath::IsFinite(ContactSettings.HoverPaddingCm) || ContactSettings.HoverPaddingCm < ContactSettings.TouchPaddingCm)
        Errors.Add(TEXT("Contact hand/padding dimensions must be finite; hover padding must include touch padding."));

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
    }
    for (const auto& Bone : SecondaryBones)
        if (!Groups.Contains(Bone.Group)) Errors.Add(FString::Printf(TEXT("Secondary group %d has no settings."), Bone.Group));
    return Errors.IsEmpty();
}
