#include "GratiaLocomotion.h"
#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaMovement, Log, All);

UGratiaLocomotion::UGratiaLocomotion()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    static ConstructorHelpers::FObjectFinder<UInputAction> WalkAsset(TEXT("/Game/Gratia/Input/IA_Walk.IA_Walk"));
    static ConstructorHelpers::FObjectFinder<UInputAction> TurnAsset(TEXT("/Game/Gratia/Input/IA_SnapTurn.IA_SnapTurn"));
    static ConstructorHelpers::FObjectFinder<UInputMappingContext> ContextAsset(TEXT("/Game/Gratia/Input/IMC_GratiaLocomotion.IMC_GratiaLocomotion"));
    WalkAction = WalkAsset.Object;
    TurnAction = TurnAsset.Object;
    Mapping = ContextAsset.Object;
}

void UGratiaLocomotion::BeginPlay()
{
    Super::BeginPlay();
    BindPlayer();
}

void UGratiaLocomotion::BindPlayer()
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    APawn* NewPawn = PC ? PC->GetPawn() : nullptr;
    if (NewPawn != Pawn.Get())
    {
        Pawn = NewPawn;
        Camera = NewPawn ? NewPawn->FindComponentByClass<UCameraComponent>() : nullptr;
        Controller = PC;
        bTurnArmed = true;
    }
    if (PC && PC->GetLocalPlayer() && Mapping)
    {
        UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
        if (Input && !Input->HasMappingContext(Mapping)) Input->AddMappingContext(Mapping, 50);
    }
}

bool UGratiaLocomotion::IsReady() const
{
    return Pawn.IsValid() && Camera.IsValid() && Controller.IsValid() && WalkAction && TurnAction && Mapping
        && Cast<UEnhancedPlayerInput>(Controller->PlayerInput);
}

FVector2D UGratiaLocomotion::FilterStick(FVector2D Stick)
{
    if (Stick.ContainsNaN()) return FVector2D::ZeroVector;
    const double Length = Stick.Size();
    constexpr double DeadZone = 0.18;
    if (Length <= DeadZone) return FVector2D::ZeroVector;
    return Stick / Length * FMath::Clamp((Length - DeadZone) / (1.0 - DeadZone), 0.0, 1.0);
}

FVector UGratiaLocomotion::SweepBody(FVector Delta) const
{
    if (!Pawn.IsValid() || !Camera.IsValid() || !GetWorld() || Delta.ContainsNaN()) return FVector::ZeroVector;
    // A pawn with a scene root cannot sweep its children. Sweep an upright body capsule explicitly.
    constexpr float Radius = 22.0f;
    constexpr float HalfHeight = 70.0f;
    FVector Start = Camera->GetComponentLocation();
    Start.Z = Pawn->GetActorLocation().Z + HalfHeight + 2.0;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(GratiaWalking), false, Pawn.Get());
    Params.AddIgnoredActor(GetOwner());
    const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
    FHitResult Hit;
    if (!GetWorld()->SweepSingleByChannel(Hit, Start, Start + Delta, FQuat::Identity, ECC_Pawn, Shape, Params)) return Delta;
    if (Hit.bStartPenetrating) return FVector::ZeroVector;
    const float SafeTime = FMath::Max(0.0f, Hit.Time - 0.5f / FMath::Max(1.0, Delta.Size()));
    FVector Travel = Delta * SafeTime;
    FVector Slide = FVector::VectorPlaneProject(Delta * (1.0f - SafeTime), Hit.Normal);
    Slide.Z = 0.0;
    FHitResult SlideHit;
    if (GetWorld()->SweepSingleByChannel(SlideHit, Start + Travel, Start + Travel + Slide, FQuat::Identity, ECC_Pawn, Shape, Params))
    {
        Slide *= SlideHit.bStartPenetrating ? 0.0f : FMath::Max(0.0f, SlideHit.Time - 0.5f / FMath::Max(1.0, Slide.Size()));
    }
    return Travel + Slide;
}

void UGratiaLocomotion::Walk(FVector2D Stick, float Seconds)
{
    if (!IsReady() || !FMath::IsFinite(Seconds) || Seconds <= 0.0f) return;
    const FVector2D Input = FilterStick(Stick);
    if (Input.IsNearlyZero()) return;
    const FRotator Heading(0.0, Camera->GetComponentRotation().Yaw, 0.0);
    const FVector Forward = Heading.Vector();
    const FVector Right = FRotationMatrix(Heading).GetUnitAxis(EAxis::Y);
    const FVector Delta = (Forward * Input.Y + Right * Input.X) * SpeedCmPerSecond * FMath::Min(Seconds, 0.05f);
    Pawn->AddActorWorldOffset(SweepBody(Delta), false, nullptr, ETeleportType::TeleportPhysics);
}

void UGratiaLocomotion::SnapTurn(float Degrees)
{
    if (!IsReady() || !FMath::IsFinite(Degrees)) return;
    const FVector Before = Camera->GetComponentLocation();
    Pawn->AddActorWorldRotation(FRotator(0.0, Degrees, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
    const FVector After = Camera->GetComponentLocation();
    Pawn->AddActorWorldOffset(FVector(Before.X - After.X, Before.Y - After.Y, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
}

void UGratiaLocomotion::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    BindPlayer();
    if (!IsReady() || !bEnabled) return;
    UEnhancedPlayerInput* Input = Cast<UEnhancedPlayerInput>(Controller->PlayerInput);
    const FVector2D Stick = Input->GetActionValue(WalkAction).Get<FVector2D>();
    const float Turn = Input->GetActionValue(TurnAction).Get<float>();
    Walk(Stick, DeltaTime);
    if (FMath::Abs(Turn) < 0.25f) bTurnArmed = true;
    else if (bTurnArmed && FMath::Abs(Turn) >= 0.7f)
    {
        SnapTurn(Turn > 0.0f ? 30.0f : -30.0f);
        bTurnArmed = false;
    }
    if (!bReportedInput && (!FilterStick(Stick).IsNearlyZero() || FMath::Abs(Turn) > 0.1f))
    {
        bReportedInput = true;
        UE_LOG(LogGratiaMovement, Display, TEXT("Live enhanced locomotion input received: walk=(%.2f,%.2f) turn=%.2f"), Stick.X, Stick.Y, Turn);
    }
}

bool UGratiaLocomotion::RunChecks(FString& Failure)
{
    if (!IsReady()) { Failure = TEXT("Cooked enhanced locomotion assets or player are missing"); return false; }
    const FTransform Original = Pawn->GetActorTransform();
    bool Passed = FilterStick(FVector2D(0.1, 0.05)).IsNearlyZero()
        && FMath::IsNearlyEqual(FilterStick(FVector2D(1.0, 1.0)).Size(), 1.0, 0.001);
    Pawn->SetActorLocation(FVector(0.0, 0.0, Original.GetLocation().Z));
    Pawn->SetActorRotation(FRotator::ZeroRotator);
    const FVector Start = Pawn->GetActorLocation();
    for (int32 Index = 0; Index < 20; ++Index) Walk(FVector2D(0.0, 1.0), 0.05f);
    const FVector End = Pawn->GetActorLocation();
    Passed &= FMath::IsNearlyEqual((End - Start).Size2D(), 120.0, 0.1) && FMath::IsNearlyEqual(End.Z, Start.Z, 0.001);
    Walk(FVector2D::ZeroVector, 0.05f);
    Passed &= Pawn->GetActorLocation().Equals(End, 0.001);
    for (int32 Index = 0; Index < 100; ++Index) Walk(FVector2D(0.0, 1.0), 0.05f);
    Passed &= Pawn->GetActorLocation().X < 274.0 && Pawn->GetActorLocation().X > 250.0;
    const FVector Pivot = Camera->GetComponentLocation();
    SnapTurn(30.0f);
    Passed &= Camera->GetComponentLocation().Equals(Pivot, 0.01);
    UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
    Passed &= Subsystem && Subsystem->HasMappingContext(Mapping);
    // The lower-priority template keys must not retain teleport or left-stick turning.
    UEnhancedPlayerInput* Input = Cast<UEnhancedPlayerInput>(Controller->PlayerInput);
    const UInputAction* Teleport = LoadObject<UInputAction>(nullptr, TEXT("/Game/XRFramework/Input/Actions/IA_Move.IA_Move"));
    const bool Blocked = Subsystem && Teleport && Subsystem->QueryKeysMappedToAction(Teleport).IsEmpty();
    Passed &= Blocked;
    Pawn->SetActorTransform(Original, false, nullptr, ETeleportType::TeleportPhysics);
    if (!Passed) Failure = TEXT("Walk distance, collision, pivot, dead zone or teleport suppression failed");
    UE_LOG(LogGratiaMovement, Display, TEXT("Locomotion check: walk_distance=%.2fcm wall_stop<274cm teleport_blocked=%s result=%s"),
        (End - Start).Size2D(), Blocked ? TEXT("yes") : TEXT("no"), Passed ? TEXT("PASS") : TEXT("FAIL"));
    return Passed;
}
