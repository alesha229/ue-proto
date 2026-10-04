#include "GratiaSecondaryMotion.h"
#include "GratiaSecondaryBones.h"
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
    if (!Character.IsValid() || !Driver || !Character->Interaction) return;
    auto* Interaction = Character->Interaction.Get();
    const int32 Signature = Interaction->Quality | (Interaction->bHairMotion << 2)
        | (Interaction->bClothMotion << 3) | (Interaction->bBodyMotion << 4)
        | (Interaction->bPhysicalMotion << 5) | (Character->IsIdlePreview() << 6) | (Interaction->bEarMotion << 7);
    if (!bForce && Signature == SettingsSignature) return;
    SettingsSignature = Signature;
    bWaitForDriverTick = true;
    USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    Mesh->SetAllBodiesSimulatePhysics(false);
    Mesh->SetAllBodiesPhysicsBlendWeight(0.0f);
    ActiveBones.Reset();
    Driver->SetSkeletalMeshComponent(Mesh);
    if (!Mesh->GetPhysicsAsset() || !Interaction->bPhysicalMotion || !Character->IsIdlePreview() || bFault) return;

    // Sort in skeleton order so each selected chain begins at its anchored root.
    const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    TArray<const FGratiaSecondaryBoneDef*> Candidates;
    for (const auto& Definition : GratiaSecondaryBones::Definitions)
        if (Definition.bSafeDefaultSimulation && Skeleton.FindBoneIndex(FName(Definition.Name)) != INDEX_NONE)
            Candidates.Add(&Definition);
    Candidates.Sort([&Skeleton](const FGratiaSecondaryBoneDef& A, const FGratiaSecondaryBoneDef& B)
        { return Skeleton.FindBoneIndex(FName(A.Name)) < Skeleton.FindBoneIndex(FName(B.Name)); });
    const int32 Profile = FMath::Clamp(Interaction->Quality, 0, 2);
    const int32 Caps[3][5] = {{0, 0, 0, 4, 11}, {0, 32, 16, 4, 11}, {0, 150, 150, 150, 150}};
    int32 Counts[5] = {};
    for (const auto* Definition : Candidates)
    {
        const uint8 Group = Definition->Group;
        const bool bEnabled = Group == 4 ? Interaction->bEarMotion : Group == 1 ? Interaction->bHairMotion :
            Group == 2 ? Interaction->bClothMotion : Interaction->bBodyMotion;
        if (!bEnabled || Counts[Group] >= Caps[Profile][Group]) continue;
        const FName Name(Definition->Name);
        FBodyInstance* Body = Mesh->GetBodyInstance(Name);
        if (!Body) continue;
        Mesh->SetBodySimulatePhysics(Name, true);
        Body->SetEnableGravity(false);
        Body->PhysicsBlendWeight = Group == 3 ? 0.25f : 0.55f;
        FPhysicalAnimationData Drive;
        Drive.bIsLocalSimulation = true;
        Drive.OrientationStrength = Group == 3 ? 650.0f : Group == 4 ? 450.0f : Group == 2 ? 260.0f : 180.0f;
        Drive.AngularVelocityStrength = Group == 3 ? 75.0f : 40.0f;
        Drive.MaxAngularForce = Group == 3 ? 120.0f : 35.0f;
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
    if (!Character.IsValid() || !FMath::IsFinite(Delta)) return;
    CheckSeconds += Delta;
    if (CheckSeconds < 0.25f) return;
    CheckSeconds = 0.0f;
    auto* Mesh = Character->CharacterMesh.Get();
    for (FName Name : ActiveBones)
    {
        const FTransform Pose = Mesh->GetSocketTransform(Name);
        const FTransform Target = Driver->GetBodyTargetTransform(Name);
        if (Pose.ContainsNaN() || Target.ContainsNaN() ||
            FVector::Distance(Pose.GetLocation(), Target.GetLocation()) > 35.0)
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
    if (!Character.IsValid() || !Character->CharacterMesh->GetPhysicsAsset())
    { Failure = TEXT("Cooked character PhysicsAsset is missing"); return false; }
    auto* Mesh = Character->CharacterMesh.Get();
    auto* Settings = Character->Interaction.Get();
    bool Pass = Mesh->GetPhysicsAsset()->SkeletalBodySetups.Num() == 176
        && Mesh->GetPhysicsAsset()->ConstraintSetup.Num() == 146;
    Character->ResetToIdle();
    for (int32 Profile = 0; Profile < 3; ++Profile)
    {
        Settings->Quality = Profile; Settings->bEarMotion = Settings->bHairMotion = Settings->bClothMotion = Settings->bBodyMotion = Settings->bPhysicalMotion = true;
        RefreshSettings(true);
        const int32 Expected[] = {15, 63, 146};
        Pass &= ActiveBones.Num() == Expected[Profile];
        for (FName Name : ActiveBones)
        {
            FBodyInstance* Body = Mesh->GetBodyInstance(Name);
            Pass &= Body && Body->IsInstanceSimulatingPhysics() && !Mesh->GetSocketTransform(Name).ContainsNaN();
        }
        for (FName Name : {FName("root"), FName("DEF-foot_L"), FName("DEF-foot_R"), FName("DEF-thigh_L"), FName("DEF-spine_003")})
        {
            FBodyInstance* Body = Mesh->GetBodyInstance(Name);
            Pass &= Body && !Body->IsInstanceSimulatingPhysics();
        }
    }
    Settings->bPhysicalMotion = false; RefreshSettings(true); Pass &= ActiveBones.IsEmpty();
    Settings->bPhysicalMotion = true; Settings->Quality = 1; RefreshSettings(true);
    Pass &= !bFault;
    if (!Pass) Failure = TEXT("Physics body coverage, quality budget, core anchoring or disable/reset check failed");
    return Pass;
}
