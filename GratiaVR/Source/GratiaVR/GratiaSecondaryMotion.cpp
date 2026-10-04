#include "GratiaSecondaryMotion.h"
#include "GratiaCharacterProfile.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaInteraction.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaPhysics, Log, All);

UGratiaSecondaryMotion::UGratiaSecondaryMotion()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGratiaSecondaryMotion::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    if (!Character.IsValid()) return;
    Driver = NewObject<UPhysicalAnimationComponent>(GetOwner(), TEXT("GratiaPhysicalAnimation"));
    Driver->RegisterComponent();
    Driver->AddTickPrerequisiteComponent(this);
    Driver->AddTickPrerequisiteComponent(Character->CharacterMesh);
    RefreshSettings(true);
}

void UGratiaSecondaryMotion::RefreshSettings(bool bForce)
{
    if (!Character.IsValid() || !Driver || !Character->Interaction || !Character->CharacterMesh) return;
    UGratiaCharacterProfile* CharacterProfile = Character->CharacterProfile;
    auto* Interaction = Character->Interaction.Get();
    const int32 Signature = Interaction->Quality | (Interaction->bHairMotion << 2)
        | (Interaction->bClothMotion << 3) | (Interaction->bBodyMotion << 4)
        | (Interaction->bPhysicalMotion << 5) | (Character->IsIdlePreview() << 6) | (Interaction->bEarMotion << 7);
    if (!bForce && Signature == SettingsSignature && LastProfile.Get() == CharacterProfile) return;
    LastProfile = CharacterProfile;
    SettingsSignature = Signature;
    bWaitForDriverTick = true;
    USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    Mesh->SetAllBodiesSimulatePhysics(false);
    Mesh->SetAllBodiesPhysicsBlendWeight(0.0f);
    ActiveBones.Reset();
    Driver->SetSkeletalMeshComponent(Mesh);
    if (!CharacterProfile || !CharacterProfile->Capabilities.bSecondaryPhysics || !Mesh->GetSkeletalMeshAsset()
        || !Mesh->GetPhysicsAsset() || !Interaction->bPhysicalMotion || !Character->IsIdlePreview() || bFault) return;

    // Sort in skeleton order so each selected chain begins at its anchored root.
    const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    TArray<const FGratiaSecondaryBoneDefinition*> Candidates;
    for (const auto& Definition : CharacterProfile->SecondaryBones)
        if (Definition.bSafeSimulation && Skeleton.FindBoneIndex(Definition.Bone) != INDEX_NONE)
            Candidates.Add(&Definition);
    Candidates.Sort([&Skeleton](const FGratiaSecondaryBoneDefinition& A, const FGratiaSecondaryBoneDefinition& B)
        { return Skeleton.FindBoneIndex(A.Bone) < Skeleton.FindBoneIndex(B.Bone); });
    const int32 Profile = FMath::Clamp(Interaction->Quality, 0, 2);
    const FGratiaQualityProfile Quality = CharacterProfile->GetQualitySettings(Profile);
    int32 Counts[5] = {};
    for (const auto* Definition : Candidates)
    {
        const uint8 Group = Definition->Group;
        if (Group < 1 || Group > 4) continue;
        const bool bEnabled = Group == 4 ? Interaction->bEarMotion : Group == 1 ? Interaction->bHairMotion :
            Group == 2 ? Interaction->bClothMotion : Interaction->bBodyMotion;
        if (!bEnabled || Counts[Group] >= Quality.GetGroupCap(Group) || ActiveBones.Num() >= Quality.TotalBodyCap) continue;
        const FName Name = Definition->Bone;
        const FGratiaSecondaryGroupSettings GroupSettings = CharacterProfile->GetSecondaryGroupSettings(Group);
        FBodyInstance* Body = Mesh->GetBodyInstance(Name);
        if (!Body) continue;
        Mesh->SetBodySimulatePhysics(Name, true);
        Body->SetEnableGravity(false);
        Body->PhysicsBlendWeight = GroupSettings.BlendWeight;
        FPhysicalAnimationData Drive;
        Drive.bIsLocalSimulation = true;
        Drive.OrientationStrength = GroupSettings.OrientationStrength;
        Drive.AngularVelocityStrength = GroupSettings.AngularVelocityStrength;
        Drive.MaxAngularForce = GroupSettings.MaxAngularForce;
        Driver->ApplyPhysicalAnimationSettings(Name, Drive);
        Body->SetLinearVelocity(FVector::ZeroVector, false);
        Body->SetAngularVelocityInRadians(FVector::ZeroVector, false);
        ActiveBones.Add(Name);
        ++Counts[Group];
    }
    // Individual body blend weights are used; bBlendPhysics=true would override them with 1.
    Mesh->SetEnablePhysicsBlending(false);
    UE_LOG(LogGratiaPhysics, Display, TEXT("SECONDARY PHYSICS profile=%d active=%d hair=%d cloth=%d body=%d ears_tail=%d"),
        Profile, ActiveBones.Num(), Counts[1], Counts[2], Counts[3], Counts[4]);
}

void UGratiaSecondaryMotion::ResetPhysics()
{
    bFault = false;
    RefreshSettings(true);
}

void UGratiaSecondaryMotion::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    RefreshSettings();
    // ApplyPhysicalAnimationSettings builds target bodies in the driver's tick.
    // GetBodyTargetTransform assumes those arrays already exist in UE 5.8.
    if (bWaitForDriverTick) { bWaitForDriverTick = false; return; }
    if (!Character.IsValid() || !Character->CharacterProfile || !FMath::IsFinite(Delta)) return;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    CheckSeconds += Delta;
    if (CheckSeconds < Profile->PhysicsSafetyCheckSeconds) return;
    CheckSeconds = 0.0f;
    auto* Mesh = Character->CharacterMesh.Get();
    for (FName Name : ActiveBones)
    {
        const FTransform Pose = Mesh->GetSocketTransform(Name);
        const FTransform Target = Driver->GetBodyTargetTransform(Name);
        if (Pose.ContainsNaN() || Target.ContainsNaN() ||
            FVector::Distance(Pose.GetLocation(), Target.GetLocation()) > Profile->MaxPhysicsTargetDeviationCm)
        {
            bFault = true;
            UE_LOG(LogGratiaPhysics, Error, TEXT("SECONDARY PHYSICS FAULT %s; disabling simulation until reset"), *Name.ToString());
            RefreshSettings(true);
            break;
        }
    }
}

bool UGratiaSecondaryMotion::RunChecks(FString& Failure)
{
    if (!Character.IsValid() || !Character->CharacterProfile || !Character->CharacterMesh || !Character->Interaction)
    { Failure = TEXT("Character profile, mesh or interaction component is missing"); return false; }
    UGratiaCharacterProfile* CharacterProfile = Character->CharacterProfile;
    auto* Mesh = Character->CharacterMesh.Get();
    auto* Settings = Character->Interaction.Get();
    TArray<FString> Errors, Warnings;
    bool Pass = CharacterProfile->ValidateProfile(Errors, Warnings);
    if (!CharacterProfile->Capabilities.bSecondaryPhysics)
    {
        RefreshSettings(true);
        Pass &= ActiveBones.IsEmpty();
        if (!Pass) Failure = TEXT("Profile validation or disabled optional-physics check failed: ") + FString::Join(Errors, TEXT("; "));
        return Pass;
    }
    if (!Mesh->GetPhysicsAsset()) { Failure = TEXT("Profile requires a cooked PhysicsAsset but none is assigned"); return false; }
    int32 AvailableGroups[5] = {};
    for (const auto& Bone : CharacterProfile->SecondaryBones)
        if (Bone.bSafeSimulation && Bone.Group >= 1 && Bone.Group <= 4 && Mesh->GetBodyInstance(Bone.Bone)) ++AvailableGroups[Bone.Group];
    const int32 PreviousQuality = Settings->Quality;
    const bool PreviousHair = Settings->bHairMotion, PreviousCloth = Settings->bClothMotion;
    const bool PreviousBody = Settings->bBodyMotion, PreviousEars = Settings->bEarMotion, PreviousPhysics = Settings->bPhysicalMotion;
    Character->ResetToIdle();
    for (int32 Profile = 0; Profile < 3; ++Profile)
    {
        Settings->Quality = Profile; Settings->bEarMotion = Settings->bHairMotion = Settings->bClothMotion = Settings->bBodyMotion = Settings->bPhysicalMotion = true;
        RefreshSettings(true);
        const FGratiaQualityProfile Quality = CharacterProfile->GetQualitySettings(Profile);
        int32 Expected = 0;
        for (uint8 Group = 1; Group <= 4; ++Group) Expected += FMath::Min(AvailableGroups[Group], Quality.GetGroupCap(Group));
        Expected = FMath::Min(Expected, Quality.TotalBodyCap);
        Pass &= ActiveBones.Num() == Expected;
        for (FName Name : ActiveBones)
        {
            FBodyInstance* Body = Mesh->GetBodyInstance(Name);
            Pass &= Body && Body->IsInstanceSimulatingPhysics() && !Mesh->GetSocketTransform(Name).ContainsNaN();
        }
        for (FName Semantic : {FName("Root"), FName("LeftFoot"), FName("RightFoot"), FName("LeftThigh"), FName("RightThigh"), FName("UpperChest")})
        {
            const FName Name = CharacterProfile->ResolveBone(Semantic);
            if (Name.IsNone()) continue;
            FBodyInstance* Body = Mesh->GetBodyInstance(Name);
            // Some model physics assets omit these bodies; no body means no simulation.
            Pass &= !Body || !Body->IsInstanceSimulatingPhysics();
        }
    }
    Settings->bPhysicalMotion = false; RefreshSettings(true); Pass &= ActiveBones.IsEmpty();
    Settings->bPhysicalMotion = PreviousPhysics; Settings->Quality = PreviousQuality;
    Settings->bHairMotion = PreviousHair; Settings->bClothMotion = PreviousCloth;
    Settings->bBodyMotion = PreviousBody; Settings->bEarMotion = PreviousEars;
    RefreshSettings(true);
    Pass &= !bFault;
    if (!Pass) Failure = TEXT("Physics body coverage, quality budget, core anchoring or disable/reset check failed");
    return Pass;
}
