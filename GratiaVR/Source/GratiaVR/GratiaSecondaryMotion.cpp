#include "GratiaSecondaryMotion.h"
#include "GratiaCharacterProfile.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaInteraction.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "GratiaHandPressure.h"

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
        | (Interaction->bPhysicalMotion << 5) | (Character->IsAnimatedPreview() << 6) | (Interaction->bEarMotion << 7);
    if (!bForce && Signature == SettingsSignature && LastProfile.Get() == CharacterProfile) return;
    LastProfile = CharacterProfile;
    SettingsSignature = Signature;
    ClearHands();
    bWaitForDriverTick = true;
    USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    Mesh->SetAllBodiesSimulatePhysics(false);
    Mesh->SetAllBodiesPhysicsBlendWeight(0.0f);
    ActiveBones.Reset();
    Driver->SetSkeletalMeshComponent(Mesh);
    if (!CharacterProfile || !CharacterProfile->Capabilities.bSecondaryPhysics || !Mesh->GetSkeletalMeshAsset()
        || !Mesh->GetPhysicsAsset() || !Interaction->bPhysicalMotion || !Character->IsAnimatedPreview() || bFault) return;

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

void UGratiaSecondaryMotion::ClearHands()
{
    for (FPhysicsHand& Hand : Hands) Hand = FPhysicsHand();
}

void UGratiaSecondaryMotion::SubmitHand(bool bLeft, const FVector& Position, bool bAllowed, float Delta, float Trigger)
{
    FPhysicsHand& Hand = Hands[bLeft ? 0 : 1];
    const UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    if (!bAllowed || !Profile || !Profile->HandPhysics.bEnabled || ActiveBones.IsEmpty() || bFault
        || Position.ContainsNaN() || !FMath::IsFinite(Trigger) || !FMath::IsFinite(Delta) || Delta <= 0 || Delta > 0.1f)
    { Hand = FPhysicsHand(); return; }
    // A new/recovered hand is seeded without sweeping from its parked location.
    const bool bContinuous = Hand.bReady && FVector::Distance(Hand.Current, Position) <= Profile->HandPhysics.MaxTravelCm;
    if (!bContinuous) { Hand.GrabbedBone = NAME_None; Hand.bGrabArmed = false; }
    Hand.bGrabPressed = false;
    if (Trigger <= 0.25f)
    {
        if (!Hand.GrabbedBone.IsNone()) UE_LOG(LogGratiaPhysics, Display, TEXT("HAND_GRAB hand=%d release=trigger bone=%s"), bLeft ? 0 : 1, *Hand.GrabbedBone.ToString());
        Hand.GrabbedBone = NAME_None; Hand.bGrabArmed = true;
    }
    else if (Trigger >= 0.65f && Hand.bGrabArmed)
    {
        Hand.bGrabPressed = Profile->HandPhysics.bAllowGrab && bContinuous;
        Hand.bGrabArmed = false;
    }
    Hand.Previous = bContinuous ? Hand.Current : Position;
    Hand.Current = Position;
    Hand.Delta = Delta;
    Hand.bPending = bContinuous;
    Hand.bReady = true;
}

FString UGratiaSecondaryMotion::GetHandDiagnostics() const
{
    return FString::Printf(TEXT("Grab L=%s R=%s | pushes L=%d R=%d"), *Hands[0].GrabbedBone.ToString(),
        *Hands[1].GrabbedBone.ToString(), HandPushCounts[0], HandPushCounts[1]);
}

void UGratiaSecondaryMotion::ApplyHandPressure()
{
    if (!Character.IsValid() || !Character->CharacterProfile || !Character->CharacterMesh || bFault) return;
    const FGratiaHandPhysicsSettings& Settings = Character->CharacterProfile->HandPhysics;
    if (!Settings.bEnabled) { ClearHands(); return; }
    auto* Mesh = Character->CharacterMesh.Get();
    for (int32 Index = 0; Index < 2; ++Index)
    {
        FPhysicsHand& Hand = Hands[Index];
        if (!Hand.bPending) continue;
        Hand.bPending = false; // never replay stale tracking data
        const FVector Velocity = ((Hand.Current - Hand.Previous) / Hand.Delta).GetClampedToMaxSize(Settings.MaxSpeedCmPerSecond);
        if (Hand.bGrabPressed)
        {
            Hand.bGrabPressed = false;
            float BestDistance = Settings.RadiusCm;
            for (FName Bone : ActiveBones)
            {
                if (Hands[1 - Index].GrabbedBone == Bone) continue;
                FBodyInstance* Body = Mesh->GetBodyInstance(Bone);
                FVector Point;
                const float Distance = Body && Body->IsInstanceSimulatingPhysics() ? Body->GetDistanceToBody(Hand.Current, Point) : -1;
                if (!FMath::IsFinite(Distance) || Distance < 0 || Distance >= BestDistance || Point.ContainsNaN()) continue;
                BestDistance = Distance; Hand.GrabbedBone = Bone;
                Hand.GrabLocalPoint = Body->GetUnrealWorldTransform().InverseTransformPosition(Point);
                Hand.GrabOffset = Point - Hand.Current;
            }
            UE_LOG(LogGratiaPhysics, Display, TEXT("HAND_GRAB hand=%d acquire=%s"), Index, *Hand.GrabbedBone.ToString());
        }
        if (!Hand.GrabbedBone.IsNone())
        {
            FBodyInstance* Body = Mesh->GetBodyInstance(Hand.GrabbedBone);
            if (!Settings.bAllowGrab || !ActiveBones.Contains(Hand.GrabbedBone) || !Body || !Body->IsInstanceSimulatingPhysics())
            { Hand.GrabbedBone = NAME_None; continue; }
            const FVector Point = Body->GetUnrealWorldTransform().TransformPosition(Hand.GrabLocalPoint);
            const FVector Error = Hand.Current + Hand.GrabOffset - Point;
            if (Error.ContainsNaN() || Error.Size() > Settings.GrabBreakDistanceCm)
            {
                UE_LOG(LogGratiaPhysics, Display, TEXT("HAND_GRAB hand=%d release=break_distance"), Index);
                Hand.GrabbedBone = NAME_None; continue;
            }
            const FVector Force = (Error * Settings.GrabStiffness +
                (Velocity - Body->GetUnrealWorldVelocityAtPoint(Point)) * Settings.GrabDamping).GetClampedToMaxSize(Settings.MaxForce);
            if (!Force.ContainsNaN()) Body->AddImpulseAtPosition(Force * Hand.Delta, Point);
            continue; // grab and pressure share the same per-hand force budget
        }
        struct FPush { FBodyInstance* Body; FVector Point, Direction; double Force; FName Bone; };
        TArray<FPush> Pushes;
        double Total = 0;
        for (FName Bone : ActiveBones)
        {
            FBodyInstance* Body = Mesh->GetBodyInstance(Bone);
            if (!Body || !Body->IsInstanceSimulatingPhysics()) continue;
            FHitResult Hit;
            const bool bSwept = Body->Sweep(Hit, Hand.Previous, Hand.Current, FQuat::Identity,
                FCollisionShape::MakeSphere(Settings.RadiusCm), false);
            FVector Closest;
            const float Distance = Body->GetDistanceToBody(Hand.Current, Closest);
            const bool bOverlap = FMath::IsFinite(Distance) && Distance >= 0 && Distance < Settings.RadiusCm;
            if (!bSwept && !bOverlap) continue;
            FVector Normal = bSwept ? Hit.Normal : (Hand.Current - Closest).GetSafeNormal();
            if (Normal.IsNearlyZero()) Normal = (Hand.Current - Body->GetUnrealWorldTransform().GetLocation()).GetSafeNormal();
            if (Normal.ContainsNaN() || Normal.IsNearlyZero()) continue;
            Normal.Normalize();
            const FVector Point = bSwept ? Hit.ImpactPoint : Closest;
            if (Point.ContainsNaN()) continue;
            const FVector BodyVelocity = Body->GetUnrealWorldVelocityAtPoint(Point);
            const double ClosingSpeed = -FVector::DotProduct(Velocity - BodyVelocity, Normal);
            // Crossing a thin body remains a hit even when the endpoint is outside.
            const double Depth = bOverlap ? Settings.RadiusCm - Distance : 0.0;
            const double Force = GratiaHandPressure::Force(Depth, ClosingSpeed, Settings.Stiffness, Settings.Damping, Settings.MaxForce);
            if (Force <= 0) continue;
            Pushes.Add({Body, Point, -Normal, Force, Bone});
            Total += Force;
        }
        // One aggregate budget per hand prevents dense overlapping chains multiplying force.
        const double Scale = Total > Settings.MaxForce ? Settings.MaxForce / Total : 1.0;
        for (const FPush& Push : Pushes)
            Push.Body->AddImpulseAtPosition(Push.Direction * Push.Force * Scale * Hand.Delta, Push.Point);
        if (!Pushes.IsEmpty())
        {
            ++HandPushCounts[Index];
            const double Now = GetWorld()->GetTimeSeconds();
            if (Now >= NextHandLog[Index])
            {
                UE_LOG(LogGratiaPhysics, Display, TEXT("HAND_PHYSICS hand=%d bodies=%d first=%s impulse=%.3f pushes=%d"),
                    Index, Pushes.Num(), *Pushes[0].Bone.ToString(), FMath::Min(Total, double(Settings.MaxForce)) * Hand.Delta, HandPushCounts[Index]);
                NextHandLog[Index] = Now + 1.0;
            }
        }
    }
}

void UGratiaSecondaryMotion::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    RefreshSettings();
    // ApplyPhysicalAnimationSettings builds target bodies in the driver's tick.
    // GetBodyTargetTransform assumes those arrays already exist in UE 5.8.
    if (bWaitForDriverTick) { bWaitForDriverTick = false; return; }
    ApplyHandPressure();
    if (!Character.IsValid() || !Character->CharacterProfile || !FMath::IsFinite(Delta)) return;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    CheckSeconds += Delta;
    if (CheckSeconds < Profile->PhysicsSafetyCheckSeconds) return;
    CheckSeconds = 0.0f;
    auto* Mesh = Character->CharacterMesh.Get();
    for (FName Name : ActiveBones)
    {
        const FBodyInstance* Body = Mesh->GetBodyInstance(Name);
        const FTransform Pose = Body ? Body->GetUnrealWorldTransform() : Mesh->GetSocketTransform(Name);
        const FTransform Target = Driver->GetBodyTargetTransform(Name);
        if (!Body || Pose.ContainsNaN() || Target.ContainsNaN() || Body->GetUnrealWorldVelocity().ContainsNaN()
            || Body->GetUnrealWorldAngularVelocityInRadians().ContainsNaN() ||
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
            if (Body)
            {
                const FVector Size = Body->GetBodyBounds().GetSize();
                const double Limit = CharacterProfile->MaxSecondaryCollisionSizeCm * Character->GetActorScale3D().GetAbs().GetMax();
                Pass &= !Size.ContainsNaN() && Size.GetMax() > 0 && Size.GetMax() <= Limit;
            }
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
