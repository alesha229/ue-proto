#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaContact, Log, All);

void FGratiaContactZone::Step(bool bLeft, bool bRight, bool bHover, float Delta)
{
    const float StepTime = FMath::IsFinite(Delta) ? FMath::Clamp(Delta, 0.0f, 0.05f) : 0.0f;
    const int32 Owner = Hand == 0 && bLeft ? 0 : Hand == 1 && bRight ? 1 : bLeft ? 0 : bRight ? 1 : INDEX_NONE;
    Elapsed += StepTime;
    if (State == EGratiaContactState::Cooldown)
    {
        Hand = INDEX_NONE;
        // Require release before rearming. Holding cannot retrigger every cooldown.
        if (Elapsed >= 0.7f && Owner == INDEX_NONE) { State = bHover ? EGratiaContactState::Hovered : EGratiaContactState::Idle; Elapsed = 0.0f; }
        return;
    }
    if (State == EGratiaContactState::Touched || State == EGratiaContactState::Held)
    {
        if (Owner == INDEX_NONE || (!bCanHold && Elapsed >= 0.18f)) { State = EGratiaContactState::Cooldown; Hand = INDEX_NONE; Elapsed = 0.0f; }
        else { Hand = Owner; if (bCanHold && Elapsed >= 0.4f) State = EGratiaContactState::Held; }
        return;
    }
    if (Owner != INDEX_NONE) { State = EGratiaContactState::Touched; Hand = Owner; Elapsed = 0.0f; ++Reactions; }
    else { State = bHover ? EGratiaContactState::Hovered : EGratiaContactState::Idle; Hand = INDEX_NONE; }
}

UGratiaInteraction::UGratiaInteraction()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGratiaInteraction::MakeZones()
{
    Zones.Reset();
    auto Add = [this](const TCHAR* Name, const TCHAR* Bone, float Radius, bool Hold, int32 Priority, FVector Offset = FVector::ZeroVector)
    {
        FGratiaContactZone Zone; Zone.Name = Name; Zone.Bone = Bone; Zone.Radius = Radius; Zone.bCanHold = Hold; Zone.Priority = Priority; Zone.Offset = Offset; Zones.Add(Zone);
    };
    Add(TEXT("Left hand"), TEXT("DEF-hand_L"), 7.0f, true, 1);
    Add(TEXT("Right hand"), TEXT("DEF-hand_R"), 7.0f, true, 1);
    Add(TEXT("Left forearm"), TEXT("DEF-forearm_L"), 9.0f, true, 2);
    Add(TEXT("Right forearm"), TEXT("DEF-forearm_R"), 9.0f, true, 2);
    Add(TEXT("Left shoulder"), TEXT("DEF-upper_arm_L"), 11.0f, true, 3);
    Add(TEXT("Right shoulder"), TEXT("DEF-upper_arm_R"), 11.0f, true, 3);
    Add(TEXT("Face"), TEXT("DEF-spine_006"), 12.0f, false, 0, FVector(0, 0, 11));
    Add(TEXT("Hair"), TEXT("DEF-spine_006"), 16.0f, true, 4, FVector(0, 0, 27));
    Add(TEXT("Upper costume"), TEXT("DEF-spine_003"), 23.0f, true, 5);
    Add(TEXT("Clothed torso"), TEXT("DEF-spine_002"), 21.0f, true, 6);
    Add(TEXT("Waist fabric"), TEXT("DEF-spine"), 23.0f, true, 7);
    Add(TEXT("Left fabric"), TEXT("DEF-thigh_L"), 15.0f, true, 8);
    Add(TEXT("Right fabric"), TEXT("DEF-thigh_R"), 15.0f, true, 8);
    Add(TEXT("Scene cube"), TEXT("None"), 32.0f, true, 9);
}

void UGratiaInteraction::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    MakeZones();
    Caption = NewObject<UTextRenderComponent>(GetOwner(), TEXT("ReactionCaption"));
    GetOwner()->AddInstanceComponent(Caption);
    Caption->SetupAttachment(GetOwner()->GetRootComponent());
    Caption->SetWorldSize(3.5f);
    Caption->SetHorizontalAlignment(EHTA_Center);
    Caption->SetTextRenderColor(FColor(230, 240, 255));
    Caption->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Caption->RegisterComponent();
    Caption->SetVisibility(false);
    UE_LOG(LogGratiaContact, Display, TEXT("Interaction ready: %d zones; hold=0.4s cooldown=0.7s face hold disabled, existing owner then left-hand tie priority."), Zones.Num());
}

FVector UGratiaInteraction::ZonePosition(const FGratiaContactZone& Zone) const
{
    if (Zone.Bone.IsNone()) return FVector(155, 150, 108);
    if (!Character.IsValid()) return FVector::ZeroVector;
    return Character->CharacterMesh->GetSocketLocation(Zone.Bone) + Character->GetActorTransform().TransformVectorNoScale(Zone.Offset);
}

void UGratiaInteraction::SetHandSample(bool bLeft, const FTransform& Raw, const FTransform& Visual, bool bAllowed)
{
    FHandSample& Hand = Hands[bLeft ? 0 : 1];
    Hand.Raw = Raw; Hand.Visual = Visual;
    Hand.bAllowed = bAllowed && !Raw.ContainsNaN() && !Visual.ContainsNaN();
}

FTransform UGratiaInteraction::ConstrainHand(const FTransform& From, const FTransform& Target) const
{
    if (Target.ContainsNaN() || !Character.IsValid()) return Target;
    FVector Point = Target.GetLocation();
    // Conservative bone-following sphere proxies; a hand centre stops at radius + its own 6cm.
    // These remain active when secondary animation is disabled.
    for (int32 Pass = 0; Pass < 4; ++Pass)
    {
        for (const FGratiaContactZone& Zone : Zones)
        {
            if (Zone.Bone.IsNone()) continue; // the room cube is constrained as a box below
            const FVector Center = ZonePosition(Zone);
            const float Radius = Zone.Radius + 6.0f;
            FVector Offset = Point - Center;
            if (Offset.SizeSquared() < FMath::Square(Radius))
            {
                if (Offset.IsNearlyZero()) Offset = FVector(-1, 0, 0);
                Point = Center + Offset.GetSafeNormal() * Radius;
            }
            // Catch fast movement through a proxy, rather than only the final overlap.
            const FVector Start = From.GetLocation();
            const FVector Travel = Point - Start;
            const FVector Relative = Start - Center;
            const double A = Travel.SizeSquared(), B = FVector::DotProduct(Relative, Travel), C = Relative.SizeSquared() - Radius * Radius;
            const double Discriminant = B * B - A * C;
            if (A > UE_SMALL_NUMBER && C > 0.0 && Discriminant >= 0.0)
            {
                const double T = (-B - FMath::Sqrt(Discriminant)) / A;
                if (T >= 0.0 && T < 1.0) Point = Start + Travel * FMath::Max(0.0, T - 0.002);
            }
        }
    }
    // The one-metre scene prop has a box surface, rather than a spherical stand-in.
    const FVector Cube(155, 150, 50), Extent(56, 56, 56);
    FVector P = Point - Cube;
    if (FMath::Abs(P.X) < Extent.X && FMath::Abs(P.Y) < Extent.Y && FMath::Abs(P.Z) < Extent.Z)
    {
        const FVector Depth = Extent - P.GetAbs();
        if (Depth.X <= Depth.Y && Depth.X <= Depth.Z) P.X = P.X >= 0.0 ? Extent.X : -Extent.X;
        else if (Depth.Y <= Depth.Z) P.Y = P.Y >= 0.0 ? Extent.Y : -Extent.Y;
        else P.Z = P.Z >= 0.0 ? Extent.Z : -Extent.Z;
        Point = Cube + P;
    }
    FTransform Result = Target; Result.SetLocation(Point); return Result;
}

void UGratiaInteraction::React(int32 ZoneIndex)
{
    ++ReactionSerial;
    ActiveZone = ZoneIndex; ReactionSeconds = 1.4f;
    const FGratiaContactZone& Zone = Zones[ZoneIndex];
    const float Speed = Zone.Hand != INDEX_NONE ? Hands[Zone.Hand].Speed : 0.0f;
    Impulse = FMath::Clamp(Speed / 150.0f, 0.15f, 1.0f);
    Caption->SetText(FText::FromString(FString::Printf(TEXT("%s%s"), Speed > 120.0f ? TEXT("Easy... ") : Mood == 2 ? TEXT("Hmm. ") : TEXT("Hey! "), *Zone.Name.ToString())));
    Caption->SetWorldLocation(Character->GetActorLocation() + FVector(0, 0, 225));
    Caption->SetWorldRotation(FRotator(0, 180, 0));
    Caption->SetVisibility(true); CaptionSeconds = 1.8f;
    const float Now = GetWorld()->GetTimeSeconds();
    if (bSound && Now - LastChime > 0.7f)
    {
        // A short soft acknowledgement tone, generated locally; no speech-service dependency.
        USoundWaveProcedural* Sound = NewObject<USoundWaveProcedural>(this);
        Sound->SetSampleRate(24000); Sound->NumChannels = 1; Sound->Duration = 0.12f;
        TArray<int16> Samples; Samples.SetNum(2880);
        for (int32 I = 0; I < Samples.Num(); ++I)
        {
            const double T = double(I) / 24000.0;
            const double Envelope = FMath::Sin(PI * I / Samples.Num());
            Samples[I] = int16(1100.0 * Envelope * FMath::Sin(2.0 * PI * (Speed > 120.0f ? 390.0 : 620.0) * T));
        }
        Sound->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
        UGameplayStatics::PlaySoundAtLocation(this, Sound, ZonePosition(Zone), 0.3f);
        LastChime = Now;
    }
    UE_LOG(LogGratiaContact, Display, TEXT("CONTACT REACTION: zone=%s hand=%d speed=%.2f mood=%d impulse=%.2f"), *Zone.Name.ToString(), Zone.Hand, Speed, Mood, Impulse);
}

void UGratiaInteraction::ResetState()
{
    MakeZones(); ActiveZone = INDEX_NONE; Reaction = ReactionSeconds = CaptionSeconds = Impulse = 0.0f; ReactionSerial = 0;
    bDemo = false;
    for (FHandSample& Hand : Hands) Hand.bAllowed = false;
    if (Caption) Caption->SetVisibility(false);
}

void UGratiaInteraction::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    if (!Character.IsValid() || !FMath::IsFinite(Delta) || Delta <= 0.0f) return;
    const float StepTime = FMath::Min(Delta, 0.05f);
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    UCameraComponent* View = Player ? Player->FindComponentByClass<UCameraComponent>() : nullptr;
    if (View) LookTarget = View->GetComponentLocation();
    for (FHandSample& Hand : Hands)
    {
        Hand.Speed = Hand.bAllowed ? FMath::Min(500.0, FVector::Distance(Hand.Raw.GetLocation(), Hand.Last) / FMath::Max(0.001f, Delta)) : 0.0f;
        Hand.Last = Hand.Raw.GetLocation();
    }
    for (int32 Index = 0; Index < Zones.Num(); ++Index)
    {
        FGratiaContactZone& Zone = Zones[Index]; const FVector Center = ZonePosition(Zone);
        const double L = FVector::Distance(Hands[0].Raw.GetLocation(), Center), R = FVector::Distance(Hands[1].Raw.GetLocation(), Center);
        const int32 Before = Zone.Reactions;
        Zone.Step(Hands[0].bAllowed && L <= Zone.Radius + 6.5f, Hands[1].bAllowed && R <= Zone.Radius + 6.5f,
            (Hands[0].bAllowed && L <= Zone.Radius + 20.0f) || (Hands[1].bAllowed && R <= Zone.Radius + 20.0f), StepTime);
        if (Zone.Reactions > Before && (ActiveZone == INDEX_NONE || ReactionSeconds <= 0.0f || Zone.Priority < Zones[ActiveZone].Priority)) React(Index);
    }
    bool Held = false;
    if (Zones.IsValidIndex(ActiveZone))
    {
        const FGratiaContactZone& Zone = Zones[ActiveZone];
        Held = Zone.State == EGratiaContactState::Held || Zone.State == EGratiaContactState::Touched;
        if (Held && Zone.Hand != INDEX_NONE) LookTarget = Hands[Zone.Hand].Visual.GetLocation();
    }
    ReactionSeconds = FMath::Max(0.0f, ReactionSeconds - StepTime);
    const float Desired = Held ? 0.6f : ReactionSeconds > 0.0f ? FMath::Clamp(ReactionSeconds / 1.4f, 0.0f, 1.0f) : 0.0f;
    Reaction = FMath::FInterpTo(Reaction, Desired, StepTime, 8.0f);
    Impulse = FMath::FInterpTo(Impulse, 0.0f, StepTime, 4.0f);
    if (!Held && ReactionSeconds == 0.0f && Reaction < 0.001f) ActiveZone = INDEX_NONE;
    CaptionSeconds -= StepTime;
    Caption->SetVisibility(CaptionSeconds > 0.0f);
    if (View && CaptionSeconds > 0.0f) Caption->SetWorldRotation((View->GetComponentLocation() - Caption->GetComponentLocation()).Rotation());
    DemoSeconds += StepTime;
    if (bDemo && DemoSeconds >= 4.0f) { DemoSeconds = 0.0f; React((ActiveZone + 1 + Zones.Num()) % Zones.Num()); }
    USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    Mesh->SetMorphTarget(TEXT("Mouth smile"), Reaction * (Mood == 2 ? 0.15f : 0.65f));
    Mesh->SetMorphTarget(TEXT("Brows up"), Reaction * (0.15f + Impulse * 0.45f));
    Mesh->SetMorphTarget(TEXT("Eyes surprised"), Reaction * Impulse * 0.5f);
    Mesh->SetMorphTarget(TEXT("Mouth o"), Reaction * Impulse * 0.12f);
}

bool UGratiaInteraction::RunChecks(FString& Failure)
{
    bool Pass = Zones.Num() == 14;
    int32 Cycles = 0;
    for (const FGratiaContactZone& Definition : Zones)
    {
        Pass &= Definition.Bone.IsNone() || (Character.IsValid() && Character->CharacterMesh->GetBoneIndex(Definition.Bone) != INDEX_NONE);
        for (int32 Hand = 0; Hand < 2; ++Hand) for (int32 Cycle = 0; Cycle < 20; ++Cycle)
        {
            FGratiaContactZone Zone = Definition; Zone.State = EGratiaContactState::Idle; Zone.Hand = INDEX_NONE; Zone.Reactions = 0; Zone.Elapsed = 0.0f;
            Zone.Step(false, false, true, 0.011f); Pass &= Zone.State == EGratiaContactState::Hovered && Zone.Reactions == 0;
            Zone.Step(Hand == 0, Hand == 1, true, 0.011f); Pass &= Zone.State == EGratiaContactState::Touched && Zone.Hand == Hand && Zone.Reactions == 1;
            for (int32 I = 0; I < 500; ++I) Zone.Step(Hand == 0, Hand == 1, true, 0.01f);
            Pass &= Zone.Reactions == 1 && (Zone.bCanHold ? Zone.State == EGratiaContactState::Held : Zone.State == EGratiaContactState::Cooldown);
            Zone.Step(false, false, false, 0.011f);
            Pass &= Zone.State == EGratiaContactState::Cooldown || (!Zone.bCanHold && Zone.State == EGratiaContactState::Idle);
            for (int32 I = 0; I < 72; ++I) Zone.Step(false, false, false, 0.01f);
            Pass &= Zone.State == EGratiaContactState::Idle;
            ++Cycles;
        }
        if (!Definition.Bone.IsNone())
        {
            const FVector Center = ZonePosition(Definition);
            const FTransform Goal(FQuat::Identity, Center);
            const FTransform Constrained = ConstrainHand(FTransform(FQuat::Identity, Center + FVector(-100,0,0)), Goal);
            Pass &= !Constrained.ContainsNaN() && FVector::Distance(Center, Constrained.GetLocation()) >= Definition.Radius + 5.0f;
        }
    }
    FGratiaContactZone Tie; Tie.Step(true, true, true, 0.011f); Pass &= Tie.Hand == 0;
    Tie.Step(false, true, true, 0.011f); Pass &= Tie.Hand == 1 && Tie.Reactions == 1;
    Tie.Step(false, false, false, 0.011f); Pass &= Tie.State == EGratiaContactState::Cooldown;
    UE_LOG(LogGratiaContact, Display, TEXT("CONTACT SELFTEST: %d zones, %d full cycles, loss/hold/tie/cooldown and proxy bounds=%s"), Zones.Num(), Cycles, Pass ? TEXT("PASS") : TEXT("FAIL"));
    if (!Pass) Failure = TEXT("Contact zone geometry, lifecycle, priority or proxy constraint test failed");
    return Pass;
}
