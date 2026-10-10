#include "GratiaPlayBody.h"
#include "GratiaPlaySettings.h"
#include "GratiaPlaySubsystem.h"
#include "GratiaGarments.h"
#include "GratiaPlayProp.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaCharacterProfile.h"
#include "GratiaAnimInstance.h"
#include "GratiaSecondaryMotion.h"
#include "GratiaStage1Runtime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaPlayBody, Log, All);

namespace
{
    FVector ProfileAxis(const UGratiaCharacterProfile* Profile, const FVector& Local)
    {
        const FVector Forward = Profile ? Profile->ForwardAxis.GetSafeNormal() : FVector::ForwardVector;
        const FVector Up = Profile ? Profile->UpAxis.GetSafeNormal() : FVector::UpVector;
        const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
        return Forward * Local.X + Right * Local.Y + Up * Local.Z;
    }

    FName ParentBone(const USkeletalMeshComponent* Mesh, FName Bone)
    {
        const int32 Index = Mesh ? Mesh->GetBoneIndex(Bone) : INDEX_NONE;
        const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
        if (Index == INDEX_NONE || !Asset) return NAME_None;
        const int32 Parent = Asset->GetRefSkeleton().GetParentIndex(Index);
        return Parent == INDEX_NONE ? NAME_None : Asset->GetRefSkeleton().GetBoneName(Parent);
    }

    /** Critically damped follow of a point (frame-rate independent). */
    void FollowPoint(FVector& Value, FVector& Velocity, const FVector& Target, float Hz, float Delta)
    {
        const float Dt = GratiaPlay::SafeDelta(Delta);
        const double Omega = 2.0 * PI * FMath::Max(0.1f, Hz);
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(Dt * 240.0f));
        const double H = Dt / FMath::Max(1, Steps);
        for (int32 I = 0; I < Steps && Dt > 0.0f; ++I)
        {
            Velocity += (Omega * Omega * (Target - Value) - 2.0 * Omega * Velocity) * H;
            Value += Velocity * H;
        }
        if (Value.ContainsNaN() || Velocity.ContainsNaN()) { Value = Target; Velocity = FVector::ZeroVector; }
    }
}

UGratiaPlayBody::UGratiaPlayBody()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGratiaPlayBody::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    // The pose input must be ready before the mesh evaluates this frame's animation.
    if (Character.IsValid() && Character->CharacterMesh) Character->CharacterMesh->AddTickPrerequisiteComponent(this);
}

void UGratiaPlayBody::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseAll();
    RestorePress();
    if (Character.IsValid() && Character->CharacterMesh)
        if (UGratiaAnimInstance* Anim = Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance())) Anim->PlayPose.Reset();
    Super::EndPlay(Reason);
}

FName UGratiaPlayBody::GetHeldLimb(bool bLeft) const
{
    const UGratiaPlaySettings* Settings = CachedSettings.Get();
    for (int32 I = 0; I < Limbs.Num(); ++I)
        if (Limbs[I].Hand == (bLeft ? 0 : 1) && Settings && Settings->LimbGrabs.IsValidIndex(I)) return Settings->LimbGrabs[I].Name;
    return NAME_None;
}

void UGratiaPlayBody::ReleaseAll()
{
    UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this);
    for (FLimb& Limb : Limbs)
    {
        if (Limb.Hand != INDEX_NONE && Play) Play->ReleaseHand(Limb.Hand == 0, this);
        Limb.Hand = INDEX_NONE;
    }
}

void UGratiaPlayBody::Resolve(const UGratiaPlaySettings& Settings)
{
    ReleaseAll();
    RestorePress();
    Presses.Reset();
    CachedSettings = &Settings;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    CachedProfile = Profile;
    const USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    auto Bone = [Profile, Mesh](FName Semantic) -> FName
    {
        const FName Name = Profile && !Semantic.IsNone() ? Profile->ResolveBone(Semantic) : NAME_None;
        return Mesh && !Name.IsNone() && Mesh->GetBoneIndex(Name) != INDEX_NONE ? Name : NAME_None;
    };
    Limbs.SetNum(Settings.LimbGrabs.Num());
    for (int32 I = 0; I < Limbs.Num(); ++I)
    {
        const FGratiaLimbGrabDefinition& Definition = Settings.LimbGrabs[I];
        FLimb& Limb = Limbs[I];
        Limb = FLimb();
        Limb.End = Bone(Definition.EndSemantic);
        // Missing semantics fall back to the skeleton: the end's parent and grandparent.
        Limb.Mid = Bone(Definition.MidSemantic);
        if (Limb.Mid.IsNone()) Limb.Mid = ParentBone(Mesh, Limb.End);
        Limb.Root = Bone(Definition.RootSemantic);
        if (Limb.Root.IsNone()) Limb.Root = ParentBone(Mesh, Limb.Mid);
        Limb.Pull = Bone(Definition.PullSemantic);
        Limb.bResolved = !Limb.End.IsNone() && !Limb.Mid.IsNone() && !Limb.Root.IsNone();
        if (!Limb.bResolved)
            UE_LOG(LogGratiaPlayBody, Warning, TEXT("PLAY limb %s disabled: bones not mapped (%s/%s/%s)."), *Definition.Name.ToString(),
                *Definition.RootSemantic.ToString(), *Definition.MidSemantic.ToString(), *Definition.EndSemantic.ToString());
    }
    Grounds.SetNum(Settings.GroundContacts.Num());
    for (int32 I = 0; I < Grounds.Num(); ++I)
    {
        const FGratiaGroundContactDefinition& Definition = Settings.GroundContacts[I];
        FGround& Ground = Grounds[I];
        Ground = FGround();
        Ground.Bone = Bone(Definition.Semantic);
        Ground.Root = Bone(Definition.RootSemantic);
        Ground.Mid = Bone(Definition.MidSemantic);
        if (!Definition.RootSemantic.IsNone() && (Ground.Root.IsNone() || Ground.Mid.IsNone())) Ground.Root = Ground.Mid = NAME_None;
        Ground.bResolved = !Ground.Bone.IsNone() && (Definition.RootSemantic.IsNone() || !Ground.Root.IsNone());
    }
    bPelvisValid = false;
    UE_LOG(LogGratiaPlayBody, Display, TEXT("PLAY body resolved limbs=%d grounds=%d"),
        Limbs.FilterByPredicate([](const FLimb& L) { return L.bResolved; }).Num(),
        Grounds.FilterByPredicate([](const FGround& G) { return G.bResolved; }).Num());
}

void UGratiaPlayBody::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Character.Get());
    USkeletalMeshComponent* Mesh = Character.IsValid() ? Character->CharacterMesh.Get() : nullptr;
    UGratiaAnimInstance* Anim = Mesh ? Cast<UGratiaAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
    if (!Anim)
    {
        if (!bAnimReported && Mesh && Mesh->GetAnimInstance())
            UE_LOG(LogGratiaPlayBody, Warning, TEXT("PLAY body off: AnimationClass %s is not UGratiaAnimInstance (no IK/soft tuning hook)."), *Mesh->GetAnimInstance()->GetClass()->GetName());
        bAnimReported = true;
        return;
    }
    if (!Settings || !UGratiaPlaySubsystem::IsLayerEnabled() || !UGratiaPlaySubsystem::Get(this))
    {
        ReleaseAll();
        RestorePress();
        Anim->PlayPose.Reset();
        return;
    }
    if (CachedSettings.Get() != Settings || CachedProfile.Get() != Character->CharacterProfile.Get()
        || Limbs.Num() != Settings->LimbGrabs.Num() || Grounds.Num() != Settings->GroundContacts.Num()) Resolve(*Settings);
    Pose.Reset();
    LastBodyBones = 0;
    UpdateSoft(*Settings, Delta);
    UpdatePress(*Settings, Delta);
    UpdateLimbs(*Settings, Delta);
    UpdateGround(*Settings, Delta);
    if (const UGratiaGarments* Garments = GetOwner()->FindComponentByClass<UGratiaGarments>()) Garments->AppendLateOffsets(Pose.LateOffsets);
    Anim->PlayPose = Pose;
}

void UGratiaPlayBody::UpdateLimbs(const UGratiaPlaySettings& Settings, float Delta)
{
    UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this);
    USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    const FTransform ToWorld = Mesh->GetComponentTransform();
    const double Scale = FMath::Max(0.01, double(ToWorld.GetMaximumAxisScale()));

    // Take or let go with the grip.
    for (int32 HandIndex = 0; HandIndex < 2; ++HandIndex)
    {
        const bool bLeft = HandIndex == 0;
        const UGratiaPlaySubsystem::FHand& Hand = Play->GetHand(bLeft);
        const bool bPressed = Hand.Grip >= Settings.GrabThreshold && PrevGrip[HandIndex] < Settings.GrabThreshold;
        PrevGrip[HandIndex] = Hand.Grip;
        const int32 Held = Limbs.IndexOfByPredicate([HandIndex](const FLimb& L) { return L.Hand == HandIndex; });
        if (Held != INDEX_NONE)
        {
            if (!Hand.bAllowed || Hand.Grip < Settings.GrabThreshold * 0.6f || !Play->IsHandOwnedBy(bLeft, this) || !bLimbGrabs)
            {
                Limbs[Held].Hand = INDEX_NONE;
                Play->ReleaseHand(bLeft, this);
                UE_LOG(LogGratiaPlayBody, Display, TEXT("PLAY limb release %s hand=%s"), *Settings.LimbGrabs[Held].Name.ToString(), bLeft ? TEXT("L") : TEXT("R"));
            }
            continue;
        }
        if (!bPressed || !Hand.bAllowed || !bLimbGrabs || Play->GetHandUse(bLeft) != EGratiaHandUse::None) continue;
        int32 Best = INDEX_NONE;
        double BestDistance = TNumericLimits<double>::Max();
        for (int32 I = 0; I < Limbs.Num(); ++I)
        {
            if (!Limbs[I].bResolved || Limbs[I].Hand != INDEX_NONE) continue;
            const double Distance = FVector::Distance(Mesh->GetSocketLocation(Limbs[I].End), Hand.World.GetLocation());
            if (Distance <= Settings.LimbGrabs[I].GrabRadiusCm * Scale && Distance < BestDistance) { Best = I; BestDistance = Distance; }
        }
        if (Best == INDEX_NONE || !Play->ClaimHand(bLeft, EGratiaHandUse::Limb, this)) continue;
        FLimb& Limb = Limbs[Best];
        const FVector End = Mesh->GetSocketLocation(Limb.End);
        Limb.Hand = HandIndex;
        Limb.HandLocal = Hand.World.InverseTransformPosition(End);
        if (Limb.Weight <= 0.001f) { Limb.Goal = End; Limb.GoalVelocity = FVector::ZeroVector; }
        UE_LOG(LogGratiaPlayBody, Display, TEXT("PLAY limb grab %s hand=%s distance=%.1f"), *Settings.LimbGrabs[Best].Name.ToString(), bLeft ? TEXT("L") : TEXT("R"), BestDistance);
    }

    // Follow, pull the body, plant the other feet.
    TMap<FName, FVector> Pulls;
    TSet<FName> GrabbedEnds;
    for (int32 I = 0; I < Limbs.Num(); ++I)
    {
        FLimb& Limb = Limbs[I];
        const FGratiaLimbGrabDefinition& Definition = Settings.LimbGrabs[I];
        if (!Limb.bResolved) continue;
        const bool bHeld = Limb.Hand != INDEX_NONE;
        if (bHeld)
        {
            FVector Target = Play->GetHand(Limb.Hand == 0).World.TransformPosition(Limb.HandLocal);
            double Surface = 0.0;
            if (bGround && TraceSurface(Target, 10.0f, 40.0f, Surface)) Target.Z = FMath::Max(Target.Z, Surface + 3.0 * Scale);
            FollowPoint(Limb.Goal, Limb.GoalVelocity, Target, Definition.FollowHz, Delta);
            GrabbedEnds.Add(Limb.End);
        }
        Limb.Weight = GratiaPlay::Envelope(Limb.Weight, bHeld ? 1.0f : 0.0f, Definition.BlendInSeconds / 3.0f, Definition.BlendOutSeconds / 3.0f, Delta);
        if (Limb.Weight <= 0.001f) { Limb.Weight = 0.0f; continue; }
        FGratiaPlayLimbGoal Goal;
        Goal.Root = Limb.Root; Goal.Mid = Limb.Mid; Goal.End = Limb.End;
        Goal.GoalCS = ToWorld.InverseTransformPosition(Limb.Goal);
        Goal.PoleCS = ProfileAxis(Profile, Definition.PoleHint);
        Goal.Weight = Limb.Weight;
        Pose.Limbs.Add(Goal);

        if (Limb.Pull.IsNone() || Definition.PullFollow <= 0.0f) continue;
        // Reach from the animated pose: last frame's pull is taken out so the pull does not chase itself.
        const FVector* LastPull = LastPulls.Find(Limb.Pull);
        const FVector Shift = LastPull ? *LastPull : FVector::ZeroVector;
        const FVector Root = Mesh->GetSocketLocation(Limb.Root) - Shift;
        const double Reach = FVector::Distance(Mesh->GetSocketLocation(Limb.Root), Mesh->GetSocketLocation(Limb.Mid))
            + FVector::Distance(Mesh->GetSocketLocation(Limb.Mid), Mesh->GetSocketLocation(Limb.End));
        const FVector Pull = GratiaPlay::BodyPull(Root, Limb.Goal, Reach * 0.97, Definition.PullFollow, Definition.PullMaxCm * Scale) * Limb.Weight;
        FVector& Sum = Pulls.FindOrAdd(Limb.Pull);
        Sum = (Sum + Pull).GetClampedToMaxSize(Definition.PullMaxCm * Scale);
    }

    // Smooth the pulls (no pops when a limb snaps out of reach) and emit them as body offsets.
    TMap<FName, FVector> Applied;
    for (TPair<FName, FVector>& Last : LastPulls)
        if (!Pulls.Contains(Last.Key)) Pulls.Add(Last.Key, FVector::ZeroVector);
    for (const TPair<FName, FVector>& Pull : Pulls)
    {
        const FVector* Last = LastPulls.Find(Pull.Key);
        FVector Value = Last ? *Last : FVector::ZeroVector;
        const float Alpha = 1.0f - FMath::Exp(-GratiaPlay::SafeDelta(Delta) / 0.06f);
        Value = FMath::Lerp(Value, Pull.Value, double(Alpha));
        if (Value.Size() < 0.02) continue;
        Applied.Add(Pull.Key, Value);
        AddBodyOffset(Pull.Key, ToWorld.InverseTransformVector(Value));
        // Feet stay where they were while the pelvis moves.
        for (int32 I = 0; I < Limbs.Num(); ++I)
        {
            const FLimb& Leg = Limbs[I];
            if (!Leg.bResolved || Leg.Pull != Pull.Key || GrabbedEnds.Contains(Leg.End) || !Settings.LimbGrabs[I].bPlantFeet) continue;
            if (Pose.Limbs.ContainsByPredicate([&Leg](const FGratiaPlayLimbGoal& G) { return G.End == Leg.End; })) continue;
            FGratiaPlayLimbGoal Plant;
            Plant.Root = Leg.Root; Plant.Mid = Leg.Mid; Plant.End = Leg.End;
            Plant.PoleCS = ProfileAxis(Profile, Settings.LimbGrabs[I].PoleHint);
            Plant.bPlant = true;
            Plant.Weight = FMath::Clamp(float(Value.Size()) / 0.5f, 0.0f, 1.0f);
            Pose.Limbs.Add(Plant);
        }
    }
    LastPulls = MoveTemp(Applied);
}

void UGratiaPlayBody::AddBodyOffset(FName Bone, const FVector& OffsetCS)
{
    for (FGratiaPlayBoneOffset& Existing : Pose.BodyOffsets)
        if (Existing.Bone == Bone) { Existing.OffsetCS += OffsetCS; return; }
    FGratiaPlayBoneOffset Offset;
    Offset.Bone = Bone;
    Offset.OffsetCS = OffsetCS;
    Pose.BodyOffsets.Add(Offset);
}

bool UGratiaPlayBody::TraceSurface(const FVector& Point, float Above, float Below, double& OutZ) const
{
    UWorld* World = GetWorld();
    if (!World || Point.ContainsNaN()) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(GratiaPlayGround), false, GetOwner());
    if (const UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this))
    {
        if (AGratiaStage1Runtime* Stage = Play->GetRuntime())
        {
            Params.AddIgnoredActor(Stage);
            if (APawn* Pawn = Stage->GetPlayerPawn()) Params.AddIgnoredActor(Pawn);
        }
        for (const TWeakObjectPtr<AGratiaPlayProp>& Prop : Play->GetProps())
            if (Prop.IsValid()) Params.AddIgnoredActor(Prop.Get());
    }
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FHitResult Hit;
    if (!World->LineTraceSingleByObjectType(Hit, Point + FVector(0, 0, Above), Point - FVector(0, 0, Below), Objects, Params)) return false;
    if (Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.5) return false;
    OutZ = Hit.ImpactPoint.Z;
    return true;
}

void UGratiaPlayBody::UpdateGround(const UGratiaPlaySettings& Settings, float Delta)
{
    USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    const FTransform ToWorld = Mesh->GetComponentTransform();
    const double Scale = FMath::Max(0.01, double(ToWorld.GetMaximumAxisScale()));
    for (int32 I = 0; I < Grounds.Num(); ++I)
    {
        FGround& Ground = Grounds[I];
        if (!Ground.bResolved) continue;
        const float Radius = Settings.GroundContacts[I].RadiusCm * float(Scale);
        float Wanted = 0.0f;
        const bool bGrabbed = Limbs.ContainsByPredicate([&Ground](const FLimb& L) { return L.Hand != INDEX_NONE && L.End == Ground.Bone; });
        if (bGround && !bGrabbed)
        {
            // The animated position: last frame's own lift and any pull of this bone are taken out first.
            FVector Point = Mesh->GetSocketLocation(Ground.Bone) - FVector(0, 0, Ground.Lift);
            if (const FVector* Pull = LastPulls.Find(Ground.Bone)) Point -= *Pull;
            double Surface = 0.0;
            // Only surfaces at most a radius (+6 cm) above the point count: a hand under a table is not lifted onto it.
            if (TraceSurface(Point, Radius + 6.0f, Settings.GroundTraceCm * float(Scale), Surface))
                Wanted = float(GratiaPlay::GroundLift(Point.Z, Surface, Radius, Settings.GroundMaxLiftCm * Scale));
        }
        Ground.Lift = GratiaPlay::Envelope(Ground.Lift, Wanted, Settings.GroundBlendSeconds, Settings.GroundBlendSeconds * 3.0f, Delta);
        if (Ground.Lift < 0.01f) { Ground.Lift = 0.0f; continue; }
        const FVector LiftCS = ToWorld.InverseTransformVector(FVector(0, 0, Ground.Lift));
        if (Ground.Root.IsNone()) { AddBodyOffset(Ground.Bone, LiftCS); continue; }
        if (FGratiaPlayLimbGoal* Existing = Pose.Limbs.FindByPredicate([&Ground](const FGratiaPlayLimbGoal& G) { return G.End == Ground.Bone; }))
        {
            if (Existing->bPlant) Existing->ExtraCS += LiftCS;
            continue;
        }
        FGratiaPlayLimbGoal Goal;
        Goal.Root = Ground.Root; Goal.Mid = Ground.Mid; Goal.End = Ground.Bone;
        Goal.bPlant = true;
        Goal.ExtraCS = LiftCS;
        Goal.Weight = 1.0f;
        Pose.Limbs.Add(Goal);
    }
}

void UGratiaPlayBody::UpdatePress(const UGratiaPlaySettings& Settings, float Delta)
{
    UGratiaSecondaryMotion* Secondary = Character->SecondaryMotion;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    if (!Driver.IsValid()) Driver = GetOwner()->FindComponentByClass<UPhysicalAnimationComponent>();
    UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this);
    if (!Driver.IsValid() || !Secondary || !Profile || !Play) return;
    if (!bPressSoftening) { RestorePress(); return; }
    const float Range = Settings.PressRangeCm * Mesh->GetComponentTransform().GetMaximumAxisScale();
    for (const FName Bone : Secondary->GetActiveBones())
    {
        FBodyInstance* Body = Mesh->GetBodyInstance(Bone);
        const FGratiaSecondaryBoneDefinition* Definition = Profile->FindSecondaryBone(Bone);
        if (!Body || !Definition) continue;
        float Near = 0.0f;
        for (const bool bLeft : { true, false })
        {
            const UGratiaPlaySubsystem::FHand& Hand = Play->GetHand(bLeft);
            if (!Hand.bAllowed) continue;
            float DistanceSquared = 0.0f;
            FVector Closest;
            if (Body->GetSquaredDistanceToBody(Hand.World.GetLocation(), DistanceSquared, Closest))
                Near = FMath::Max(Near, 1.0f - FMath::Sqrt(DistanceSquared) / FMath::Max(0.5f, Range));
        }
        FPress& Press = Presses.FindOrAdd(Bone);
        Press.Amount = GratiaPlay::Envelope(Press.Amount, FMath::Clamp(Near, 0.0f, 1.0f), Settings.PressAttackSeconds, Settings.ReturnSeconds / 3.0f, Delta);
        const bool bSettled = Press.Amount < 0.005f;
        if (bSettled) Press.Amount = 0.0f;
        if (FMath::Abs(Press.Amount - Press.Written) < 0.02f && !(bSettled && Press.Written > 0.0f)) continue;
        const FGratiaSecondaryGroupSettings Group = Profile->GetSecondaryGroupSettings(Definition->Group);
        const float* Override = Settings.PressSoftnessOverrides.Find(Bone);
        const float Drive = GratiaPlay::DriveScale(Press.Amount, Override ? *Override : Settings.PressSoftness);
        FPhysicalAnimationData Data;
        Data.bIsLocalSimulation = true;
        Data.OrientationStrength = Group.OrientationStrength * Drive;
        // Damping follows the square root of the stiffness: the return stays as damped as the profile tuned it.
        Data.AngularVelocityStrength = Group.AngularVelocityStrength * FMath::Sqrt(Drive);
        Data.MaxAngularForce = Group.MaxAngularForce;
        Driver->ApplyPhysicalAnimationSettings(Bone, Data);
        Body->PhysicsBlendWeight = FMath::Clamp(Group.BlendWeight + Settings.PressBlendBoost * Press.Amount, 0.0f, 1.0f);
        Press.Written = Press.Amount;
    }
}

void UGratiaPlayBody::RestorePress()
{
    UPhysicalAnimationComponent* PhysicalAnimation = Driver.Get();
    const UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    USkeletalMeshComponent* Mesh = Character.IsValid() ? Character->CharacterMesh.Get() : nullptr;
    for (TPair<FName, FPress>& Press : Presses)
    {
        if (Press.Value.Written <= 0.0f) { Press.Value.Amount = 0.0f; continue; }
        Press.Value = FPress();
        const FGratiaSecondaryBoneDefinition* Definition = Profile ? Profile->FindSecondaryBone(Press.Key) : nullptr;
        if (!PhysicalAnimation || !Definition || !Mesh) continue;
        const FGratiaSecondaryGroupSettings Group = Profile->GetSecondaryGroupSettings(Definition->Group);
        FPhysicalAnimationData Data;
        Data.bIsLocalSimulation = true;
        Data.OrientationStrength = Group.OrientationStrength;
        Data.AngularVelocityStrength = Group.AngularVelocityStrength;
        Data.MaxAngularForce = Group.MaxAngularForce;
        PhysicalAnimation->ApplyPhysicalAnimationSettings(Press.Key, Data);
        if (FBodyInstance* Body = Mesh->GetBodyInstance(Press.Key)) Body->PhysicsBlendWeight = Group.BlendWeight;
    }
}

void UGratiaPlayBody::UpdateSoft(const UGratiaPlaySettings& Settings, float Delta)
{
    Pose.Soft.bEnabled = bAnimeSoft && Settings.bAnimeSoft;
    if (!Pose.Soft.bEnabled) return;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    const FName Pelvis = Profile ? Profile->ResolveBone(TEXT("Pelvis")) : NAME_None;
    const FVector Position = !Pelvis.IsNone() && Mesh->GetBoneIndex(Pelvis) != INDEX_NONE ? Mesh->GetSocketLocation(Pelvis) : Mesh->GetComponentLocation();
    const float Dt = GratiaPlay::SafeDelta(Delta);
    if (Dt > 0.0f)
    {
        const FVector Velocity = (Position - PelvisPrev) / Dt;
        // Teleports, scene changes and the first frame do not kick the hair.
        if (!bPelvisValid || FVector::Distance(Position, PelvisPrev) > 30.0) { PelvisVelocity = FVector::ZeroVector; PelvisAccel = FVector::ZeroVector; }
        else
        {
            // Smoothed over 0.1 s: a raw per-frame second difference of the pelvis is mostly frame-time noise and
            // animation steps, and kicked the hair and soft parts by up to 3000 cm/s (wild jumping, soft parts off pose).
            const float Blend = 1.0f - FMath::Exp(-Dt / 0.1f);
            const FVector Smoothed = FMath::Lerp(PelvisVelocity, Velocity.ContainsNaN() ? PelvisVelocity : Velocity, double(Blend));
            const FVector Accel = (Smoothed - PelvisVelocity) / Dt;
            PelvisAccel = FMath::Lerp(PelvisAccel, Accel.ContainsNaN() ? FVector::ZeroVector : Accel, double(Blend));
            PelvisVelocity = Smoothed;
        }
        PelvisPrev = Position;
        bPelvisValid = true;
    }
    for (uint8 Group = 0; Group < 5; ++Group)
    {
        const FGratiaSoftGroupTuning& Tuning = Settings.GetSoftTuning(Group);
        Pose.Soft.WorldDamping[Group] = Tuning.WorldDamping;
        Pose.Soft.Damping[Group] = Tuning.Damping;
        Pose.Soft.Stiffness[Group] = Tuning.Stiffness;
        // An opposite push for a moment when the body jolts: the soft parts lag and swing back (cm/s).
        // Small accelerations (idle sway, breathing) do not kick.
        const FVector Felt = PelvisAccel.GetSafeNormal() * FMath::Max(0.0, PelvisAccel.Size() - double(Settings.InertiaKickDeadzone));
        Pose.Soft.KickWS[Group] = (-Felt * Tuning.InertiaKick * 0.03).GetClampedToMaxSize(Settings.MaxInertiaKick);
    }
}

FString UGratiaPlayBody::GetDiagnostics() const
{
    int32 Held = 0, Active = 0, Pressed = 0;
    for (const FLimb& Limb : Limbs) { Held += Limb.Hand != INDEX_NONE; Active += Limb.Weight > 0.0f; }
    for (const TPair<FName, FPress>& Press : Presses) Pressed += Press.Value.Amount > 0.05f;
    float MaxLift = 0.0f;
    for (const FGround& Ground : Grounds) MaxLift = FMath::Max(MaxLift, Ground.Lift);
    return FString::Printf(TEXT("limbs held=%d active=%d pulls=%d pressed=%d max_lift=%.1fcm soft=%d"),
        Held, Active, LastPulls.Num(), Pressed, MaxLift, Pose.Soft.bEnabled);
}
