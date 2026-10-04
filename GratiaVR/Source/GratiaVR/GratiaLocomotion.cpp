#include "GratiaLocomotion.h"
#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedInputComponent.h"
#include "InputCoreTypes.h"
#include "HeadMountedDisplayFunctionLibrary.h"
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
    if (PC != Controller.Get())
    {
        if (ActionInput)
        {
            if (Controller.IsValid()) Controller->PopInputComponent(ActionInput);
            ActionInput->DestroyComponent(); ActionInput = nullptr;
        }
        Controller = PC;
        if (PC && WalkAction && TurnAction)
        {
            ActionInput = NewObject<UEnhancedInputComponent>(PC, TEXT("GratiaLocomotionActions"));
            ActionInput->Priority = 60; ActionInput->bBlockInput = false;
            ActionInput->BindActionValue(WalkAction); ActionInput->BindActionValue(TurnAction);
            ActionInput->RegisterComponent(); PC->PushInputComponent(ActionInput);
        }
    }
    if (NewPawn != Pawn.Get())
    {
        Pawn = NewPawn;
        Camera = NewPawn ? NewPawn->FindComponentByClass<UCameraComponent>() : nullptr;
        bTurnArmed = true;
    }
    bMappingReady = false;
    if (PC && PC->GetLocalPlayer() && Mapping)
    {
        UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
        if (Input && !Input->HasMappingContext(Mapping)) Input->AddMappingContext(Mapping, 50);
        bMappingReady = Input && Input->HasMappingContext(Mapping);
    }
}

void UGratiaLocomotion::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ActionInput && Controller.IsValid()) Controller->PopInputComponent(ActionInput);
    if (ActionInput) ActionInput->DestroyComponent();
    ActionInput = nullptr;
    Super::EndPlay(Reason);
}

bool UGratiaLocomotion::IsReady() const
{
    return Pawn.IsValid() && Camera.IsValid() && Controller.IsValid() && WalkAction && TurnAction && Mapping
        && ActionInput && bMappingReady && Cast<UEnhancedPlayerInput>(Controller->PlayerInput);
}

FVector2D UGratiaLocomotion::FilterStick(FVector2D Stick, float DeadZone)
{
    if (Stick.ContainsNaN()) return FVector2D::ZeroVector;
    const double Length = Stick.Size();
    DeadZone = FMath::Clamp(DeadZone, 0.0f, 0.9f);
    if (Length <= DeadZone) return FVector2D::ZeroVector;
    return Stick / Length * FMath::Clamp((Length - DeadZone) / (1.0 - DeadZone), 0.0, 1.0);
}

FVector UGratiaLocomotion::SweepBody(FVector Delta) const
{
    if (!Pawn.IsValid() || !Camera.IsValid() || !GetWorld() || Delta.ContainsNaN()) return FVector::ZeroVector;
    // A pawn with a scene root cannot sweep its children. Sweep an upright body capsule explicitly.
    const float Radius = FMath::Max(1.0f, BodyRadiusCm);
    const float HalfHeight = FMath::Max(Radius, BodyHalfHeightCm);
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
    const FVector2D Input = FilterStick(Stick, StickDeadZone);
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
    MappedStick = ActionInput && WalkAction ? ActionInput->GetBoundActionValue(WalkAction).Get<FVector2D>() : FVector2D::ZeroVector;
    const float Turn = ActionInput && TurnAction ? ActionInput->GetBoundActionValue(TurnAction).Get<float>() : 0.0f;
    RawStick = FVector2D::ZeroVector;
    // OpenXR's Enhanced Input path injects action values directly, without legacy key state.
    bRawKeyChannelAvailable = !UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
    if (Controller.IsValid())
    {
        for (const TCHAR* Prefix : {TEXT("OculusTouch_Left_Thumbstick"), TEXT("ValveIndex_Left_Thumbstick"), TEXT("MixedReality_Left_Thumbstick"), TEXT("Vive_Left_Trackpad"), TEXT("Gamepad_Left")})
        {
            const bool Gamepad = FString(Prefix).StartsWith(TEXT("Gamepad"));
            const FString KeyPrefix(Prefix);
            const FVector2D Candidate(Controller->GetInputAnalogKeyState(FKey(*(KeyPrefix + (Gamepad ? TEXT("X") : TEXT("_X"))))),
                Controller->GetInputAnalogKeyState(FKey(*(KeyPrefix + (Gamepad ? TEXT("Y") : TEXT("_Y"))))));
            if (Candidate.SizeSquared() > RawStick.SizeSquared()) RawStick = Candidate;
        }
    }
    LastPawnDelta = FVector::ZeroVector;
    if (!Controller.IsValid() || !Pawn.IsValid() || !Camera.IsValid()) MovementReason = TEXT("missing player/camera");
    else if (!WalkAction || !TurnAction || !Mapping || !ActionInput) MovementReason = TEXT("missing action/binding");
    else if (!bMappingReady) MovementReason = TEXT("missing mapping context");
    else if (!Cast<UEnhancedPlayerInput>(Controller->PlayerInput)) MovementReason = TEXT("wrong PlayerInput class");
    else if (!bEnabled) MovementReason = TEXT("menu blocked");
    else if (FilterStick(MappedStick, StickDeadZone).IsNearlyZero())
        MovementReason = bRawKeyChannelAvailable && RawStick.Size() > StickDeadZone ? TEXT("raw keys present, mapped zero") : TEXT("zero action input / dead zone");
    else
    {
        const FVector Before = Pawn->GetActorLocation();
        Walk(MappedStick, DeltaTime);
        LastPawnDelta = Pawn->GetActorLocation() - Before;
        MovementReason = LastPawnDelta.IsNearlyZero(0.001) ? TEXT("collision blocked") : TEXT("moving");
    }
    if (IsReady() && bEnabled)
    {
        if (FMath::Abs(Turn) < 0.25f) bTurnArmed = true;
        else if (bTurnArmed && FMath::Abs(Turn) >= 0.7f)
        {
            SnapTurn(Turn > 0.0f ? SnapDegrees : -SnapDegrees);
            bTurnArmed = false;
        }
    }
    DiagnosticSeconds += FMath::Clamp(DeltaTime, 0.0f, 0.1f);
    if (DiagnosticSeconds >= 1.0f && (MovementReason != LastReportedReason || !MappedStick.IsNearlyZero() || !RawStick.IsNearlyZero() || FMath::Abs(Turn) > 0.1f))
    {
        DiagnosticSeconds = 0.0f; LastReportedReason = MovementReason;
        UE_LOG(LogGratiaMovement, Display, TEXT("MOVEMENT raw_keys=(%.2f,%.2f) raw_key_channel=%s mapped=(%.2f,%.2f) turn=%.2f ready=%d context=%d enabled=%d pawn_delta_cm=%.3f reason=%s"),
            RawStick.X, RawStick.Y, bRawKeyChannelAvailable ? TEXT("available") : TEXT("unavailable_OpenXR_direct_actions"), MappedStick.X, MappedStick.Y, Turn, IsReady(), bMappingReady, bEnabled, LastPawnDelta.Size(), *MovementReason);
    }
}

FString UGratiaLocomotion::GetDiagnosticText() const
{
    return FString::Printf(TEXT("Walk %s mapped %.2f/%.2f delta %.2fcm: %s"),
        bRawKeyChannelAvailable ? *FString::Printf(TEXT("raw keys %.2f/%.2f"),RawStick.X,RawStick.Y) : TEXT("OpenXR direct actions"), MappedStick.X, MappedStick.Y, LastPawnDelta.Size(), *MovementReason);
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
    Passed &= FMath::IsNearlyEqual((End - Start).Size2D(), static_cast<double>(SpeedCmPerSecond), 0.1) && FMath::IsNearlyEqual(End.Z, Start.Z, 0.001);
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
