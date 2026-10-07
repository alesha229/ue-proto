#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaAnimInstance.h"
#include "GratiaBodySurface.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaContact, Log, All);

namespace
{
    float Nonnegative(float Value)
    {
        return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
    }

    bool ValidBounds(const FVector& Center, const FVector& Extent)
    {
        return !Center.ContainsNaN() && !Extent.ContainsNaN()
            && Extent.X > UE_SMALL_NUMBER && Extent.Y > UE_SMALL_NUMBER && Extent.Z > UE_SMALL_NUMBER;
    }

    const TCHAR* StateName(EGratiaContactState State)
    {
        switch (State)
        {
        case EGratiaContactState::Hovered: return TEXT("hover");
        case EGratiaContactState::Touched: return TEXT("touch");
        case EGratiaContactState::Held: return TEXT("held");
        case EGratiaContactState::Cooldown: return TEXT("cooldown");
        default: return TEXT("idle");
        }
    }
}

void FGratiaContactZone::Step(bool bLeft, bool bRight, bool bHover, float Delta)
{
    // This bound limits stalled-frame state jumps; the actual behaviour durations belong to the profile.
    const float StepTime = FMath::IsFinite(Delta) ? FMath::Clamp(Delta, 0.0f, 0.05f) : 0.0f;
    const int32 Owner = Hand == 0 && bLeft ? 0 : Hand == 1 && bRight ? 1 : bLeft ? 0 : bRight ? 1 : INDEX_NONE;
    Elapsed += StepTime;
    if (State == EGratiaContactState::Cooldown)
    {
        Hand = INDEX_NONE;
        if (Elapsed >= Nonnegative(Settings.CooldownSeconds) && Owner == INDEX_NONE)
        {
            State = bHover ? EGratiaContactState::Hovered : EGratiaContactState::Idle;
            Elapsed = 0.0f;
        }
        return;
    }
    if (State == EGratiaContactState::Touched || State == EGratiaContactState::Held)
    {
        if (Owner == INDEX_NONE || (!bCanHold && Elapsed >= Nonnegative(Settings.SingleTouchSeconds)))
        {
            State = EGratiaContactState::Cooldown; Hand = INDEX_NONE; Elapsed = 0.0f;
        }
        else
        {
            Hand = Owner;
            if (bCanHold && Elapsed >= Nonnegative(Settings.HoldSeconds)) State = EGratiaContactState::Held;
        }
        return;
    }
    if (Owner != INDEX_NONE)
    {
        State = EGratiaContactState::Touched; Hand = Owner; Elapsed = 0.0f; ++Reactions;
    }
    else
    {
        State = bHover ? EGratiaContactState::Hovered : EGratiaContactState::Idle;
        Hand = INDEX_NONE;
    }
}

UGratiaInteraction::UGratiaInteraction()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGratiaInteraction::RebuildProfileZones()
{
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    ActiveProfile = Profile;
    ContactSettings = Profile ? Profile->ContactSettings : FGratiaContactSettings();
    Zones.Reset();
    ActiveZone = INDEX_NONE;
    Reaction = Impulse = ReactionSeconds = DemoSeconds = 0.0f;
    ReactionSerial = 0;
    for (int32 Hand = 0; Hand < 2; ++Hand)
    {
        Hands[Hand] = FHandSample();
        LastConstraint[Hand] = FGratiaContactSolveResult();
        ContactRecoveryUntil[Hand] = 0.0;
    }
    OnContactReset.Broadcast();
    if (!Profile || !Character->CharacterMesh)
    {
        UE_LOG(LogGratiaContact, Warning, TEXT("Interaction waiting for an assigned CharacterProfile and mesh."));
        return;
    }
    if (!Profile->Capabilities.bContacts)
    {
        UE_LOG(LogGratiaContact, Display, TEXT("Interaction disabled by profile %s capability."), *Profile->ProfileId.ToString());
        return;
    }
    for (const FGratiaContactZoneDefinition& Definition : Profile->ContactZones)
    {
        const FName Bone = Profile->ResolveBone(Definition.BoneSemantic);
        if (Bone.IsNone() || Character->CharacterMesh->GetBoneIndex(Bone) == INDEX_NONE
            || !FMath::IsFinite(Definition.Radius) || Definition.Radius <= 0.0f || Definition.Offset.ContainsNaN())
        {
            UE_LOG(LogGratiaContact, Warning, TEXT("Skipped invalid contact zone %s: semantic bone=%s resolved=%s."),
                *Definition.Name.ToString(), *Definition.BoneSemantic.ToString(), *Bone.ToString());
            continue;
        }
        FGratiaContactZone Zone;
        Zone.Name = Definition.Name; Zone.Bone = Bone; Zone.Offset = Definition.Offset;
        Zone.Radius = Definition.Radius; Zone.bCanHold = Definition.bCanHold; Zone.Priority = Definition.Priority;
        Zone.Settings = ContactSettings;
        Zones.Add(Zone);
    }
    FVector Center, Extent;
    if (SceneBounds(Center, Extent))
    {
        FGratiaContactZone Zone;
        Zone.Name = SceneContactActor->GetFName();
        Zone.bSceneActor = true; Zone.Priority = MAX_int32; Zone.Radius = Extent.GetMax();
        Zone.Settings = ContactSettings;
        Zones.Add(Zone);
    }
    UE_LOG(LogGratiaContact, Display,
        TEXT("Interaction profile=%s zones=%d collision proxies=%d scene actor=%s hold=%.3fs cooldown=%.3fs."),
        *Profile->ProfileId.ToString(), Zones.Num(), Profile->CollisionProxies.Num(),
        IsValid(SceneContactActor.Get()) ? *SceneContactActor->GetName() : TEXT("unassigned"),
        ContactSettings.HoldSeconds, ContactSettings.CooldownSeconds);
}

void UGratiaInteraction::SetSceneContactActor(AActor* Actor)
{
    if (SceneContactActor.Get() == Actor) return;
    SceneContactActor = Actor;
    RebuildProfileZones();
}

void UGratiaInteraction::BeginPlay()
{
    Super::BeginPlay();
    RebuildProfileZones();
}

double UGratiaInteraction::CharacterScale() const
{
    return Character.IsValid() ? FMath::Max(double(UE_SMALL_NUMBER), Character->GetActorScale3D().GetAbs().GetMax()) : 1.0;
}

bool UGratiaInteraction::SceneBounds(FVector& Center, FVector& Extent) const
{
    if (!IsValid(SceneContactActor.Get())) return false;
    SceneContactActor->GetActorBounds(false, Center, Extent);
    return ValidBounds(Center, Extent);
}

bool UGratiaInteraction::ResolveProxyPoint(FName Semantic, const FVector& Offset, FVector& Point) const
{
    if (!Character.IsValid() || !Character->CharacterMesh || !Character->CharacterProfile || Offset.ContainsNaN()) return false;
    const FName Bone = Character->CharacterProfile->ResolveBone(Semantic);
    if (Bone.IsNone() || Character->CharacterMesh->GetBoneIndex(Bone) == INDEX_NONE) return false;
    Point = Character->CharacterMesh->GetSocketLocation(Bone) + Character->GetActorTransform().TransformVector(Offset);
    return !Point.ContainsNaN();
}

FVector UGratiaInteraction::ZonePosition(const FGratiaContactZone& Zone) const
{
    FVector Center, Extent;
    if (Zone.bSceneActor) return SceneBounds(Center, Extent) ? Center : GetOwner()->GetActorLocation();
    if (!Character.IsValid() || !Character->CharacterMesh) return GetOwner()->GetActorLocation();
    return Character->CharacterMesh->GetSocketLocation(Zone.Bone) + Character->GetActorTransform().TransformVector(Zone.Offset);
}

FVector UGratiaInteraction::GetZoneWorldPosition(int32 ZoneIndex) const
{
    return Zones.IsValidIndex(ZoneIndex) ? ZonePosition(Zones[ZoneIndex]) : GetOwner()->GetActorLocation();
}

bool UGratiaInteraction::ZoneGap(const FGratiaContactZone& Zone, const FVector& Point, double& Gap) const
{
    if (Point.ContainsNaN()) return false;
    if (Zone.bSceneActor)
    {
        FVector Center, Extent;
        if (!SceneBounds(Center, Extent)) return false;
        const FVector Relative = Point - Center;
        const FVector Nearest(
            FMath::Clamp(Relative.X, -Extent.X, Extent.X),
            FMath::Clamp(Relative.Y, -Extent.Y, Extent.Y),
            FMath::Clamp(Relative.Z, -Extent.Z, Extent.Z));
        Gap = FVector::Distance(Relative, Nearest);
        return FMath::IsFinite(Gap);
    }
    if (!Character.IsValid() || !Character->CharacterMesh || Character->CharacterMesh->GetBoneIndex(Zone.Bone) == INDEX_NONE) return false;
    Gap = FVector::Distance(Point, ZonePosition(Zone)) - Zone.Radius * CharacterScale();
    return FMath::IsFinite(Gap);
}

void UGratiaInteraction::GatherCollisionShapes(TArray<FGratiaContactShape>& Shapes) const
{
    Shapes.Reset();
    if (!Character.IsValid() || !Character->CharacterProfile) return;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    const double Scale = CharacterScale();
    // Skin-fitted surface: the palm (small radius) rests on the skin instead of a wrist sphere
    // stopping at the conservative proxies.
    const bool bSurface = Profile->HandSurface.bEnabled && Character->BodySurface && Character->BodySurface->HasSurface();
    const double HandRadius = bSurface ? Nonnegative(Profile->HandSurface.PalmContactRadiusCm) * Scale : Nonnegative(Profile->ContactSettings.HandRadiusCm);
    if (bSurface) Character->BodySurface->GatherContactShapes(float(HandRadius), Shapes);
    for (const FGratiaCollisionProxyDefinition& Proxy : bSurface ? TArray<FGratiaCollisionProxyDefinition>() : Profile->CollisionProxies)
    {
        FVector Start, End;
        if (!FMath::IsFinite(Proxy.Radius) || Proxy.Radius <= 0.0f
            || !ResolveProxyPoint(Proxy.StartBoneSemantic, Proxy.StartOffset, Start)) continue;
        const double Radius = Proxy.Radius * Scale + HandRadius;
        if (Proxy.Shape == EGratiaCollisionProxyShape::Capsule)
        {
            if (!ResolveProxyPoint(Proxy.EndBoneSemantic, Proxy.EndOffset, End)) continue;
            Shapes.Add(FGratiaContactShape::Capsule(Start, End, Radius));
        }
        else Shapes.Add(FGratiaContactShape::Sphere(Start, Radius));
    }
    FVector Center, Extent;
    if (SceneBounds(Center, Extent)) Shapes.Add(FGratiaContactShape::Box(Center, Extent + FVector(HandRadius)));
}

FTransform UGratiaInteraction::ConstrainHand(const FTransform& From, const FTransform& Target, bool bLeft, const FVector& PalmLocal) const
{
    const int32 HandIndex = bLeft ? 0 : 1;
    TArray<FGratiaContactShape> Shapes;
    GatherCollisionShapes(Shapes);
    const FVector FromPalm = From.TransformPositionNoScale(PalmLocal), TargetPalm = Target.TransformPositionNoScale(PalmLocal);
    const FGratiaContactSolveResult Palm = GratiaContactSolver::Solve(FromPalm, TargetPalm, Shapes);
    FGratiaContactSolveResult Solved = Palm;
    Solved.Position = Palm.Position - (TargetPalm - Target.GetLocation());
    LastConstraint[HandIndex] = Solved;
    const bool bUnsafe = !Solved.bInputValid || !Solved.bConverged || Solved.bUsedFallback
        || Solved.StartCorrectionDistance > Nonnegative(ContactSettings.MaxHandCorrectionCm)
        || Solved.TargetCorrectionDistance > Nonnegative(ContactSettings.MaxHandCorrectionCm);
    if (bUnsafe)
    {
        const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
        ContactRecoveryUntil[HandIndex] = Now + Nonnegative(ContactSettings.ContactRecoverySeconds);
    }
    FTransform Result = !Target.ContainsNaN() ? Target : !From.ContainsNaN() ? From : FTransform::Identity;
    Result.SetLocation(Solved.Position);
    return Result;
}

void UGratiaInteraction::SetHandSample(bool bLeft, const FTransform& Raw, const FTransform& Visual, bool bAllowed)
{
    const int32 Index = bLeft ? 0 : 1;
    FHandSample& Hand = Hands[Index];
    Hand.Raw = Raw; Hand.Visual = Visual;
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const bool bValidSample = !Raw.ContainsNaN() && !Visual.ContainsNaN();
    const UGratiaCharacterProfile* Profile = ActiveProfile.Get();
    const double Correction = bValidSample ? FVector::Distance(Raw.GetLocation(), Visual.GetLocation()) : 0.0;
    const double CorrectionLimit = Nonnegative(ContactSettings.MaxHandCorrectionCm);
    const FGratiaContactSolveResult& Constraint = LastConstraint[Index];
    const bool bSafeConstraint = Constraint.bInputValid && Constraint.bConverged && !Constraint.bUsedFallback
        && Constraint.StartCorrectionDistance <= CorrectionLimit && Constraint.TargetCorrectionDistance <= CorrectionLimit;
    if (bValidSample && Correction > CorrectionLimit)
        ContactRecoveryUntil[Index] = Now + Nonnegative(ContactSettings.ContactRecoverySeconds);
    Hand.bAllowed = bAllowed && bValidSample && Profile && Profile->Capabilities.bContacts
        && bSafeConstraint && Correction <= CorrectionLimit && Now >= ContactRecoveryUntil[Index];
    Hand.GateReason = !bValidSample ? TEXT("invalid sample")
        : !bAllowed ? TEXT("tracking unavailable/recovering")
        : !Profile ? TEXT("missing profile")
        : !Profile->Capabilities.bContacts ? TEXT("contacts disabled")
        : !bSafeConstraint ? TEXT("proxy fallback/excess correction")
        : Correction > CorrectionLimit ? TEXT("excess hand correction")
        : Now < ContactRecoveryUntil[Index] ? TEXT("proxy correction recovery")
        : TEXT("ready");
    if (!Hand.bAllowed)
    {
        // Cancel/transfer ownership in the same update; tracking loss never leaves a held contact.
        const int32 Other = 1 - Index;
        for (FGratiaContactZone& Zone : Zones)
        {
            if (Zone.Hand != Index) continue;
            double OtherGap = 0.0;
            if (Hands[Other].bAllowed && ZoneGap(Zone, Hands[Other].Visual.GetLocation(), OtherGap)
                && OtherGap <= Nonnegative(ContactSettings.TouchPaddingCm))
                Zone.Hand = Other;
            else
            {
                Zone.State = EGratiaContactState::Cooldown; Zone.Hand = INDEX_NONE; Zone.Elapsed = 0.0f;
            }
        }
    }
}

FString UGratiaInteraction::GetContactDiagnostics() const
{
    const UGratiaCharacterProfile* Profile = ActiveProfile.Get();
    FString Text = FString::Printf(TEXT("Contact profile=%s zones=%d proxies=%d\n"),
        Profile ? *Profile->ProfileId.ToString() : TEXT("MISSING"), Zones.Num(), Profile ? Profile->CollisionProxies.Num() : 0);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const FHandSample& Hand = Hands[Index];
        const FGratiaContactSolveResult& Solve = LastConstraint[Index];
        Text += FString::Printf(TEXT("%s: %s; near=%s gap=%.1fcm; blocked=%d fallback=%d pen=%.4fcm correction=%.1fcm\n"),
            Index == 0 ? TEXT("L") : TEXT("R"), *Hand.GateReason.ToString(),
            Hand.NearestZone.IsNone() ? TEXT("none") : *Hand.NearestZone.ToString(), Hand.NearestZone.IsNone() ? 0.0 : Hand.NearestGapCm,
            Solve.bBlocked ? 1 : 0, Solve.bUsedFallback ? 1 : 0, Solve.MaxPenetration, Solve.TargetCorrectionDistance);
    }
    if (Zones.IsValidIndex(ActiveZone))
        Text += FString::Printf(TEXT("Zone=%s state=%s owner=%d"), *Zones[ActiveZone].Name.ToString(),
            StateName(Zones[ActiveZone].State), Zones[ActiveZone].Hand);
    else Text += TEXT("Zone=none");
    return Text;
}

void UGratiaInteraction::React(int32 ZoneIndex)
{
    if (!Zones.IsValidIndex(ZoneIndex) || !Character.IsValid()) return;
    const double ResponseNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if (ResponseNow - LastResponseTime < Nonnegative(ContactSettings.ReactionMinimumIntervalSeconds)) return;
    LastResponseTime = ResponseNow;
    ReactionSerial = ReactionSerial == MAX_int32 ? 1 : ReactionSerial + 1;
    ActiveZone = ZoneIndex; ReactionSeconds = Nonnegative(ContactSettings.ReactionSeconds);
    const FGratiaContactZone& Zone = Zones[ZoneIndex];
    LastReactionZoneName = Zone.Name;
    const float Speed = Zone.Hand != INDEX_NONE ? Hands[Zone.Hand].Speed : 0.0f;
    LastReactionSpeed = FMath::IsFinite(Speed) ? Speed : 0.0f;
    Impulse = FMath::Clamp(Speed / FMath::Max(UE_SMALL_NUMBER, ContactSettings.ImpulseSpeedCmPerSecond), 0.15f, 1.0f);
    OnContactReaction.Broadcast(Zone.Name, Zone.Hand, Speed, Mood);
    UE_LOG(LogGratiaContact, Display, TEXT("CONTACT REACTION: zone=%s hand=%d source=%s speed=%.2f mood=%d impulse=%.2f"),
        *Zone.Name.ToString(), Zone.Hand, Zone.Hand == INDEX_NONE ? TEXT("demo") : TEXT("hand sample"), Speed, Mood, Impulse);
}

void UGratiaInteraction::ResetState()
{
    RebuildProfileZones();
    LastReactionZoneName = NAME_None; LastReactionSpeed = 0.0f; LastResponseTime = -100.0;
    ExternalHold = 0.0f; ExternalHoldTime = -100.0;
    bDemo = false;
}

void UGratiaInteraction::ExternalReaction(FName ZoneName, int32 HandIndex, float Speed)
{
    const UGratiaCharacterProfile* Profile = ActiveProfile.Get();
    if (!Character.IsValid() || !Profile || !Profile->Capabilities.bContacts || ZoneName.IsNone()) return;
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if (Now - LastResponseTime < Nonnegative(ContactSettings.ReactionMinimumIntervalSeconds)) return;
    LastResponseTime = Now;
    ReactionSerial = ReactionSerial == MAX_int32 ? 1 : ReactionSerial + 1;
    ReactionSeconds = Nonnegative(ContactSettings.ReactionSeconds);
    LastReactionZoneName = ZoneName;
    LastReactionSpeed = FMath::IsFinite(Speed) ? FMath::Max(0.0f, Speed) : 0.0f;
    Impulse = FMath::Clamp(LastReactionSpeed / FMath::Max(UE_SMALL_NUMBER, ContactSettings.ImpulseSpeedCmPerSecond), 0.15f, 1.0f);
    OnContactReaction.Broadcast(ZoneName, HandIndex, LastReactionSpeed, Mood);
    UE_LOG(LogGratiaContact, Display, TEXT("CONTACT REACTION: zone=%s hand=%d source=channel speed=%.2f mood=%d impulse=%.2f"),
        *ZoneName.ToString(), HandIndex, LastReactionSpeed, Mood, Impulse);
}

void UGratiaInteraction::SetExternalHold(float Weight)
{
    ExternalHold = FMath::IsFinite(Weight) ? FMath::Clamp(Weight, 0.0f, 1.0f) : 0.0f;
    ExternalHoldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void UGratiaInteraction::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    if (!Character.IsValid()) Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    if (!Character.IsValid() || !FMath::IsFinite(Delta) || Delta <= 0.0f) return;
    if (ActiveProfile.Get() != Character->CharacterProfile.Get()) RebuildProfileZones();
    const UGratiaCharacterProfile* Profile = ActiveProfile.Get();
    if (!Profile || !Character->CharacterMesh) return;
    const float StepTime = FMath::Min(Delta, 0.05f);
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    UCameraComponent* View = Player ? Player->FindComponentByClass<UCameraComponent>() : nullptr;
    if (View) LookTarget = View->GetComponentLocation();
    for (FHandSample& Hand : Hands)
    {
        Hand.Speed = Hand.bAllowed && Hand.bWasAllowed
            ? FMath::Min(double(Nonnegative(ContactSettings.MaxHandSpeedCmPerSecond)),
                FVector::Distance(Hand.Raw.GetLocation(), Hand.Last) / FMath::Max(0.001f, Delta)) : 0.0f;
        if (!Hand.Raw.ContainsNaN()) Hand.Last = Hand.Raw.GetLocation();
        Hand.bWasAllowed = Hand.bAllowed;
        Hand.NearestZone = NAME_None; Hand.NearestGapCm = TNumericLimits<double>::Max();
    }
    int32 NewReaction = INDEX_NONE;
    for (int32 Index = 0; Index < Zones.Num(); ++Index)
    {
        FGratiaContactZone& Zone = Zones[Index];
        double Gap[2] = { 0.0, 0.0 };
        bool Near[2] = { false, false }, Hover[2] = { false, false };
        for (int32 HandIndex = 0; HandIndex < 2; ++HandIndex)
        {
            if (!ZoneGap(Zone, Hands[HandIndex].Visual.GetLocation(), Gap[HandIndex])) continue;
            Near[HandIndex] = Hands[HandIndex].bAllowed && Gap[HandIndex] <= Nonnegative(ContactSettings.TouchPaddingCm);
            Hover[HandIndex] = Hands[HandIndex].bAllowed && Gap[HandIndex] <= Nonnegative(ContactSettings.HoverPaddingCm);
            if (Gap[HandIndex] < Hands[HandIndex].NearestGapCm)
            {
                Hands[HandIndex].NearestZone = Zone.Name; Hands[HandIndex].NearestGapCm = Gap[HandIndex];
            }
        }
        const int32 Before = Zone.Reactions;
        Zone.Step(Near[0], Near[1], Hover[0] || Hover[1], StepTime);
        if (Zone.Reactions > Before && (NewReaction == INDEX_NONE || Zone.Priority < Zones[NewReaction].Priority))
            NewReaction = Index;
        if (bShowContactDebug)
        {
            const FColor Color = Zone.State == EGratiaContactState::Touched || Zone.State == EGratiaContactState::Held
                ? FColor::Green : Zone.State == EGratiaContactState::Hovered ? FColor::Yellow : FColor::Silver;
            if (Zone.bSceneActor)
            {
                FVector Center, Extent;
                if (SceneBounds(Center, Extent)) DrawDebugBox(GetWorld(), Center, Extent, Color, false, 0.0f, 0, 0.5f);
            }
            else DrawDebugSphere(GetWorld(), ZonePosition(Zone), Zone.Radius * CharacterScale(), 12, Color, false, 0.0f, 0, 0.5f);
        }
    }
    if (NewReaction != INDEX_NONE && (ActiveZone == INDEX_NONE || ReactionSeconds <= 0.0f
        || Zones[NewReaction].Priority < Zones[ActiveZone].Priority)) React(NewReaction);
    bool Held = false;
    if (Zones.IsValidIndex(ActiveZone))
    {
        const FGratiaContactZone& Zone = Zones[ActiveZone];
        Held = Zone.State == EGratiaContactState::Held || Zone.State == EGratiaContactState::Touched;
        if (Held && Zone.Hand != INDEX_NONE) LookTarget = Hands[Zone.Hand].Visual.GetLocation();
    }
    ReactionSeconds = FMath::Max(0.0f, ReactionSeconds - StepTime);
    // A penetration channel refreshes its hold every frame; a stale one lapses.
    const float External = GetWorld()->GetTimeSeconds() - ExternalHoldTime < 0.15 ? ExternalHold : 0.0f;
    const float Desired = FMath::Max(External, Held ? FMath::Clamp(ContactSettings.HoldReactionWeight, 0.0f, 1.0f)
        : ReactionSeconds > 0.0f ? FMath::Clamp(ReactionSeconds / FMath::Max(UE_SMALL_NUMBER, ContactSettings.ReactionSeconds), 0.0f, 1.0f) : 0.0f);
    Reaction = FMath::FInterpTo(Reaction, Desired, StepTime, Nonnegative(ContactSettings.ReactionInterpSpeed));
    Impulse = FMath::FInterpTo(Impulse, 0.0f, StepTime, Nonnegative(ContactSettings.ImpulseDecaySpeed));
    if (!Held && ReactionSeconds == 0.0f && Reaction < 0.001f) ActiveZone = INDEX_NONE;
    DemoSeconds += StepTime;
    if (bDemo && Zones.Num() > 0 && DemoSeconds >= FMath::Max(0.1f, ContactSettings.DemoIntervalSeconds))
    {
        DemoSeconds = 0.0f;
        const int32 DemoZone = (ActiveZone + 1 + Zones.Num()) % Zones.Num();
        // Synthetic demonstration never reports a real controller as its source.
        const int32 SavedHand = Zones[DemoZone].Hand;
        Zones[DemoZone].Hand = INDEX_NONE;
        React(DemoZone);
        Zones[DemoZone].Hand = SavedHand;
    }
    if (Profile->Capabilities.bFacialReactions)
    {
        const auto* Animation = Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance());
        const bool bAuthoredFace = Profile->bAuthoredReactionFacialCurves && Animation && Animation->IsReactionCuePlaying();
        auto SetMorph = [this, Profile](FName Semantic, float Weight)
        {
            const FName Morph = Profile->ResolveMorph(Semantic);
            if (!Morph.IsNone()) Character->CharacterMesh->SetMorphTarget(Morph, Weight);
        };
        SetMorph(TEXT("Smile"), bAuthoredFace ? 0.0f : Reaction * (Mood == 2 ? 0.15f : 0.65f));
        SetMorph(TEXT("BrowsUp"), bAuthoredFace ? 0.0f : Reaction * (0.15f + Impulse * 0.45f));
        SetMorph(TEXT("Surprise"), bAuthoredFace ? 0.0f : Reaction * Impulse * 0.5f);
        SetMorph(TEXT("MouthOpen"), bAuthoredFace ? 0.0f : Reaction * Impulse * 0.12f);
    }
    if (bShowContactDebug)
        for (const FHandSample& Hand : Hands)
            if (!Hand.Raw.ContainsNaN() && !Hand.Visual.ContainsNaN())
                DrawDebugLine(GetWorld(), Hand.Raw.GetLocation(), Hand.Visual.GetLocation(), FColor::Cyan, false, 0.0f, 0, 1.0f);
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now >= NextDiagnosticTime)
    {
        NextDiagnosticTime = Now + 0.5;
        const FString Diagnostic = GetContactDiagnostics();
        if (Diagnostic != LastDiagnostic)
        {
            LastDiagnostic = Diagnostic;
            UE_LOG(LogGratiaContact, Display, TEXT("CONTACT DIAGNOSTIC: %s"), *Diagnostic.Replace(TEXT("\n"), TEXT(" | ")));
        }
    }
}

bool UGratiaInteraction::RunChecks(FString& Failure)
{
    const UGratiaCharacterProfile* Profile = ActiveProfile.Get();
    if (!Profile || !Character.IsValid() || !Character->CharacterMesh)
    {
        Failure = TEXT("Contact integration needs a valid assigned profile and mesh");
        return false;
    }
    if (!Profile->Capabilities.bContacts)
    {
        UE_LOG(LogGratiaContact, Display, TEXT("CONTACT SELFTEST: SKIP profile disables contacts."));
        return true;
    }
    FVector SceneCenter, SceneExtent;
    const int32 ExpectedZones = Profile->ContactZones.Num() + (SceneBounds(SceneCenter, SceneExtent) ? 1 : 0);
    bool Pass = Zones.Num() == ExpectedZones;
    int32 Cycles = 0;
    for (const FGratiaContactZone& Definition : Zones)
    {
        Pass &= Definition.bSceneActor || Character->CharacterMesh->GetBoneIndex(Definition.Bone) != INDEX_NONE;
        for (int32 HandIndex = 0; HandIndex < 2; ++HandIndex) for (int32 Cycle = 0; Cycle < 20; ++Cycle)
        {
            FGratiaContactZone Zone = Definition;
            Zone.State = EGratiaContactState::Idle; Zone.Hand = INDEX_NONE; Zone.Reactions = 0; Zone.Elapsed = 0.0f;
            Zone.Step(false, false, true, 0.01f);
            Pass &= Zone.State == EGratiaContactState::Hovered && Zone.Reactions == 0;
            Zone.Step(HandIndex == 0, HandIndex == 1, true, 0.01f);
            Pass &= Zone.State == EGratiaContactState::Touched && Zone.Hand == HandIndex && Zone.Reactions == 1;
            Zone.Elapsed = Zone.bCanHold ? Nonnegative(Zone.Settings.HoldSeconds) : Nonnegative(Zone.Settings.SingleTouchSeconds);
            Zone.Step(HandIndex == 0, HandIndex == 1, true, 0.01f);
            Pass &= Zone.Reactions == 1 && (Zone.bCanHold ? Zone.State == EGratiaContactState::Held : Zone.State == EGratiaContactState::Cooldown);
            Zone.Elapsed += Nonnegative(Zone.Settings.CooldownSeconds);
            Zone.Step(HandIndex == 0, HandIndex == 1, true, 0.01f);
            Pass &= Zone.Reactions == 1;
            Zone.Step(false, false, false, 0.01f);
            Zone.Elapsed = Nonnegative(Zone.Settings.CooldownSeconds);
            Zone.Step(false, false, false, 0.01f);
            Pass &= Zone.State == EGratiaContactState::Idle;
            ++Cycles;
        }
    }
    FGratiaContactZone Tie;
    Tie.Settings = ContactSettings;
    Tie.Step(true, true, true, 0.01f); Pass &= Tie.Hand == 0;
    Tie.Step(false, true, true, 0.01f); Pass &= Tie.Hand == 1 && Tie.Reactions == 1;
    Tie.Step(false, false, false, 0.01f); Pass &= Tie.State == EGratiaContactState::Cooldown;
    TArray<FGratiaContactShape> Shapes;
    GatherCollisionShapes(Shapes);
    for (const FGratiaContactShape& Shape : Shapes)
    {
        const FVector Center = Shape.Type == EGratiaContactShapeType::Capsule ? (Shape.A + Shape.B) * 0.5 : Shape.A;
        const double Extent = Shape.Type == EGratiaContactShapeType::AxisAlignedBox ? Shape.HalfExtent.GetMax() : Shape.Radius;
        const auto Solved = GratiaContactSolver::Solve(Center + FVector(-2.0 * Extent - 100.0, 0, 0), Center, Shapes);
        Pass &= !Solved.Position.ContainsNaN() && GratiaContactSolver::MaxPenetration(Solved.Position, Shapes) <= 0.001;
    }
    const FHandSample SavedHands[2] = { Hands[0], Hands[1] };
    const double SavedRecovery[2] = { ContactRecoveryUntil[0], ContactRecoveryUntil[1] };
    const TArray<FGratiaContactZone> SavedZones = Zones;
    for (int32 HandIndex = 0; HandIndex < 2; ++HandIndex)
    {
        SetHandSample(HandIndex == 0, FTransform::Identity,
            FTransform(FQuat::Identity, FVector(ContactSettings.MaxHandCorrectionCm + 1.0f, 0, 0)), true);
        Pass &= !Hands[HandIndex].bAllowed;
    }
    for (int32 HandIndex = 0; HandIndex < 2; ++HandIndex)
    {
        Hands[HandIndex] = SavedHands[HandIndex];
        ContactRecoveryUntil[HandIndex] = SavedRecovery[HandIndex];
    }
    Zones = SavedZones;
    UE_LOG(LogGratiaContact, Display, TEXT("CONTACT SELFTEST: profile=%s %d zones, %d cycles, %d separate proxies, lifecycle/loss/tie and correction gates=%s"),
        *Profile->ProfileId.ToString(), Zones.Num(), Cycles, Shapes.Num(), Pass ? TEXT("PASS") : TEXT("FAIL"));
    if (!Pass) Failure = TEXT("Profile contact integration, lifecycle, separate proxy bounds or correction gate failed");
    return Pass;
}
