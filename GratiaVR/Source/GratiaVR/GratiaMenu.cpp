#include "GratiaMenu.h"
#include "GratiaMenuWidget.h"
#include "GratiaSceneDirector.h"
#include "GratiaSceneLibrary.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaStage1Runtime.h"
#include "GratiaLocomotion.h"
#include "GratiaCharacterProfile.h"
#include "GratiaPerformanceStage.h"
#include "Camera/CameraComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"
#include "StereoRendering.h"
#include "MotionControllerComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaMenu, Log, All);
namespace
{
float GratiaMenuValue(UEnhancedInputComponent* Input, UInputAction* Action)
{
    if (!Input || !Action) return 0.0f;
    const FInputActionValue Value = Input->GetBoundActionValue(Action);
    const float Scalar = Value.GetValueType() == EInputActionValueType::Boolean ? (Value.Get<bool>() ? 1.0f : 0.0f) : Value.Get<float>();
    return FMath::IsFinite(Scalar) ? Scalar : 0.0f;
}
const TCHAR* GratiaOnOff(bool Value) { return Value ? TEXT("вкл.") : TEXT("выкл."); }
}
UGratiaMenu::UGratiaMenu()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    static ConstructorHelpers::FObjectFinder<UInputAction> A(TEXT("/Game/Gratia/Input/IA_MenuToggle.IA_MenuToggle"));
    static ConstructorHelpers::FObjectFinder<UInputAction> B(TEXT("/Game/Gratia/Input/IA_MenuNext.IA_MenuNext"));
    static ConstructorHelpers::FObjectFinder<UInputAction> C(TEXT("/Game/Gratia/Input/IA_MenuApply.IA_MenuApply"));
    static ConstructorHelpers::FObjectFinder<UInputMappingContext> D(TEXT("/Game/Gratia/Input/IMC_GratiaMenu.IMC_GratiaMenu"));
    ToggleAction = A.Object; NextAction = B.Object; ApplyAction = C.Object; MenuMapping = D.Object;
    // Interface sounds (copies of the engine's VR editor UI sounds, setup_menu_sounds.py).
    static ConstructorHelpers::FObjectFinder<USoundBase> Click(TEXT("/Game/Gratia/Audio/UI/S_MenuClick.S_MenuClick"));
    static ConstructorHelpers::FObjectFinder<USoundBase> Opened(TEXT("/Game/Gratia/Audio/UI/S_MenuOpen.S_MenuOpen"));
    static ConstructorHelpers::FObjectFinder<USoundBase> Closed(TEXT("/Game/Gratia/Audio/UI/S_MenuClose.S_MenuClose"));
    ClickSound = Click.Object; OpenSound = Opened.Object; CloseSound = Closed.Object;
}
void UGratiaMenu::BeginPlay()
{
    Super::BeginPlay();
    if (FParse::Param(FCommandLine::Get(), TEXT("GratiaMenuShots")))
    {
        ShotStep = 0;
        // Close enough for the panel to fill a desktop view; the placement itself is checked by RunChecks.
        PanelDistanceCm = 105.0f;
        PanelBelowEyeCm = 0.0f;
    }
    // The menu context is applied as the registered asset itself: OpenXR activates the action
    // set of a registered mapping context only, so a runtime copy never received the trigger
    // in the headset (desktop keys/mouse still worked). Stick navigation lives in the asset
    // (setup_gratia_locomotion.py).
    Panel = NewObject<UWidgetComponent>(GetOwner(), TEXT("GratiaSettingsMenu"));
    GetOwner()->AddInstanceComponent(Panel);
    Panel->SetupAttachment(GetOwner()->GetRootComponent());
    Panel->SetWidgetSpace(EWidgetSpace::World);
    Panel->SetDrawSize(PanelResolution);
    Panel->SetPivot(FVector2D(0.5, 0.5));
    Panel->SetWorldScale3D(FVector(PanelScale));
    Panel->SetTwoSided(false);
    Panel->SetWindowFocusable(false);
    Panel->SetCollisionResponseToAllChannels(ECR_Ignore);
    Panel->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Panel->RegisterComponent();
    Panel->SetVisibility(false);
    Pointer = NewObject<UWidgetInteractionComponent>(GetOwner(), TEXT("GratiaMenuPointer"));
    GetOwner()->AddInstanceComponent(Pointer);
    Pointer->SetupAttachment(GetOwner()->GetRootComponent());
    Pointer->InteractionDistance = PointerDistanceCm;
    Pointer->VirtualUserIndex = 7;
    Pointer->PointerIndex = 0;
    Pointer->bEnableHitTesting = false;
    Pointer->RegisterComponent();
    // Pose -> hit test -> press/release happen together, after the runtime's tracking gate.
    Pointer->SetComponentTickEnabled(false);
    UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto MakePointerMesh = [this](const TCHAR* Name, UStaticMesh* Shape)
    {
        UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(GetOwner(), Name);
        GetOwner()->AddInstanceComponent(Mesh);
        Mesh->SetupAttachment(GetOwner()->GetRootComponent());
        Mesh->SetStaticMesh(Shape);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false);
        Mesh->RegisterComponent();
        Mesh->SetVisibility(false);
        return Mesh;
    };
    Laser = MakePointerMesh(TEXT("MenuLaser"), Cylinder);
    Dot = MakePointerMesh(TEXT("MenuPointerDot"), Sphere);
    UE_LOG(LogGratiaMenu, Display, TEXT("MENU ready: F4/Y/B open; arrows or A/X navigate; mouse/right trigger/Enter apply. Hardware VR pointer acceptance pending."));
}
UGratiaSceneDirector* UGratiaMenu::GetDirector() const
{
    const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    return Runtime ? Runtime->SceneDirector.Get() : nullptr;
}
bool UGratiaMenu::IsInScene() const { const auto* Director = GetDirector(); return Director && Director->IsInScene(); }
int32 UGratiaMenu::GetCurrentScene() const { const auto* Director = GetDirector(); return Director ? Director->GetCurrentScene() : INDEX_NONE; }
void UGratiaMenu::SetCharacter(AGratiaPreviewCharacter* Value)
{
    Character = Value;
    if (Character.IsValid() && Character->Interaction) ApplyQuality(Character->Interaction->Quality, false);
    Refresh();
}
void UGratiaMenu::BindInput(APlayerController* Controller)
{
    if (Controller == BoundController.Get()) return;
    if (bOpen) Close();
    if (ActionInput && BoundController.IsValid()) BoundController->PopInputComponent(ActionInput);
    if (ActionInput) ActionInput->DestroyComponent();
    ActionInput = nullptr; BoundController = Controller;
    if (!Controller) return;
    ActionInput = NewObject<UEnhancedInputComponent>(Controller, TEXT("GratiaMenuActions"));
    ActionInput->Priority = 110; ActionInput->bBlockInput = false;
    for (UInputAction* Action : {ToggleAction.Get(), NextAction.Get(), ApplyAction.Get()})
        if (Action) ActionInput->BindActionValue(Action);
    ActionInput->RegisterComponent(); Controller->PushInputComponent(ActionInput);
    if (!ToggleAction || !NextAction || !ApplyAction || !MenuMapping)
        UE_LOG(LogGratiaMenu, Warning, TEXT("MENU missing action/context assets; desktop F4/arrows/Enter remain available."));
}
bool UGratiaMenu::EnsureWidget(APlayerController* Controller)
{
    if (!Panel || !Controller) return false;
    const auto* Director = GetDirector();
    UGratiaSceneLibrary* Library = Director ? Director->Library.Get() : nullptr;
    if (Widget && BuiltLibrary.Get() == Library) return true;
    ReleasePointer();
    Widget = CreateWidget<UGratiaMenuWidget>(Controller, UGratiaMenuWidget::StaticClass());
    if (!Widget) return false;
    // Assign ownership before UWidgetComponent asks for the first Slate widget tree.
    Widget->Menu = this;
    BuiltLibrary = Library;
    Panel->SetOwnerPlayer(Controller->GetLocalPlayer());
    Panel->SetWidget(Widget);
    return true;
}
bool UGratiaMenu::Open(bool bLobby)
{
    auto* PC = UGameplayStatics::GetPlayerController(this, 0);
    BindInput(PC);
    auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    UCameraComponent* Camera = Runtime ? Runtime->GetPlayerCamera() : nullptr;
    if (!Camera || !EnsureWidget(PC)) return false;
    Panel->SetWorldTransform(ComputePanelTransform(Camera));
    bFollowing = false;
    SetOpen(true);
    if (bLobby) Widget->ShowPage(UGratiaMenuWidget::Scenes);
    Refresh();
    return true;
}
FTransform UGratiaMenu::ComputePanelTransform(const UCameraComponent* Camera) const
{
    if (!Camera) return Panel ? Panel->GetComponentTransform() : FTransform::Identity;
    const FVector Eye = Camera->GetComponentLocation();
    const FRotator Yaw(0.0, Camera->GetComponentRotation().Yaw, 0.0);
    FVector Position = Eye + Yaw.Vector() * PanelDistanceCm - FVector(0.0, 0.0, PanelBelowEyeCm);
    // Lowest allowed centre: half the panel above the floor (the player's floor, or furniture under the panel).
    const double HalfHeight = 0.5 * PanelResolution.Y * PanelScale;
    double Floor = -UE_BIG_NUMBER;
    const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    const APawn* Pawn = Runtime ? Runtime->GetPlayerPawn() : nullptr;
    if (Pawn) Floor = Pawn->GetActorLocation().Z;
    if (UWorld* World = GetWorld())
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(GratiaMenuFloor), false, GetOwner());
        if (Pawn) Query.AddIgnoredActor(Pawn);
        FHitResult Hit;
        // Static geometry only: the character standing in front must not lift the panel.
        if (World->LineTraceSingleByObjectType(Hit, FVector(Position.X, Position.Y, Eye.Z + 20.0), FVector(Position.X, Position.Y, Eye.Z - 400.0),
            FCollisionObjectQueryParams(ECC_WorldStatic), Query))
            Floor = FMath::Max(Floor, double(Hit.ImpactPoint.Z));
    }
    if (Floor > -UE_BIG_NUMBER) Position.Z = FMath::Max(Position.Z, Floor + HalfHeight + PanelFloorClearanceCm);
    FRotator Facing = (Eye - Position).Rotation();
    Facing.Roll = 0.0;
    return FTransform(Facing, Position, FVector(PanelScale));
}
void UGratiaMenu::UpdateFollow(float Delta)
{
    const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    const UCameraComponent* Camera = Runtime ? Runtime->GetPlayerCamera() : nullptr;
    if (!bOpen || !Panel || !Camera) { bFollowing = false; return; }
    // The panel stays where it opened until the head turns well away from it, then glides back in front.
    const FVector ToPanel = (Panel->GetComponentLocation() - Camera->GetComponentLocation()).GetSafeNormal2D();
    const FVector Ahead = FRotator(0.0, Camera->GetComponentRotation().Yaw, 0.0).Vector();
    const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ToPanel, Ahead), -1.0, 1.0)));
    if (!bFollowing && Angle > FollowAngleDegrees) bFollowing = true;
    if (!bFollowing) return;
    FollowTarget = ComputePanelTransform(Camera);
    const float Alpha = FMath::Clamp(Delta * 6.0f, 0.0f, 1.0f);
    const FVector Location = FMath::Lerp(Panel->GetComponentLocation(), FollowTarget.GetLocation(), double(Alpha));
    const FQuat Rotation = FQuat::Slerp(Panel->GetComponentQuat(), FollowTarget.GetRotation(), double(Alpha));
    Panel->SetWorldLocationAndRotation(Location, Rotation);
    if (FVector::Dist(Location, FollowTarget.GetLocation()) < 1.0 && Rotation.AngularDistance(FollowTarget.GetRotation()) < 0.01) bFollowing = false;
}
void UGratiaMenu::TickShots(float Delta)
{
    ShotSeconds += Delta;
    const UGratiaSceneDirector* Director = GetDirector();
    if (ShotStep == 0)
    {
        // Wait until the lobby or the scene has settled, then open the menu in front of the head.
        if (!Director || (Director->GetState() != EGratiaFlowState::Lobby && Director->GetState() != EGratiaFlowState::Playing) || ShotSeconds < 8.0f) return;
        if (bOpen) Close();
        if (!Open(false) || !Widget) { ShotStep = INDEX_NONE; return; }
        Widget->ShowPage(0);
        ShotStep = 1; ShotSeconds = 0.0f;
        return;
    }
    const int32 Page = (ShotStep - 1) / 2;
    if (Page >= UGratiaMenuWidget::PageCount)
    {
        if (ShotSeconds > 1.5f) UKismetSystemLibrary::QuitGame(this, BoundController.Get(), EQuitPreference::Quit, false);
        return;
    }
    if ((ShotStep - 1) % 2 == 0)
    {
        if (ShotSeconds < 1.0f) return;
        const FGratiaSceneEntry* Entry = Director ? Director->GetCurrentEntry() : nullptr;
        const FString Name = FString::Printf(TEXT("MenuShot_%s_%d.png"), Entry ? *Entry->Id.ToString() : TEXT("Lobby"), Page);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/MenuShots"), Name), false, false);
        UE_LOG(LogGratiaMenu, Display, TEXT("MENU SHOT %s empty='%s' visible=%d"), *Name, *Widget->GetEmptyState(), Widget->CountVisibleItems());
    }
    else
    {
        if (ShotSeconds < 0.6f) return;
        if (Page + 1 < UGratiaMenuWidget::PageCount) Widget->ShowPage(Page + 1);
    }
    ++ShotStep;
    ShotSeconds = 0.0f;
}
void UGratiaMenu::PointerFeedback(bool bClick)
{
    if (bClick && ClickSound) UGameplayStatics::PlaySound2D(this, ClickSound, 0.5f);
    const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    APlayerController* PC = BoundController.Get();
    if (!PC || !Runtime || !Runtime->bXRActive) return;
    const auto* Director = GetDirector();
    const float Scale = Director && Director->GetUserSettings() ? Director->GetUserSettings()->HapticsScale : 1.0f;
    if (Scale <= 0.0f) return;
    PC->SetHapticsByValue(bClick ? 0.6f : 0.3f, (bClick ? 0.45f : 0.18f) * Scale, EControllerHand::Right);
    HapticSeconds = bClick ? 0.05f : 0.025f;
}
void UGratiaMenu::Toggle() { if (bOpen) Close(); else Open(false); }
void UGratiaMenu::OpenLobby() { Open(true); }
void UGratiaMenu::Close() { SetOpen(false); }
void UGratiaMenu::SetOpen(bool bValue)
{
    const bool WasOpen = bOpen;
    if (bValue != WasOpen && (bValue ? OpenSound : CloseSound)) UGameplayStatics::PlaySound2D(this, bValue ? OpenSound : CloseSound, 0.45f);
    if (!bValue) ReleasePointer();
    bOpen = bValue;
    if (Panel)
    {
        Panel->SetVisibility(bOpen);
        Panel->SetCollisionEnabled(bOpen ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    }
    if (!bOpen) { if (Laser) Laser->SetVisibility(false); if (Dot) Dot->SetVisibility(false); }
    APlayerController* PC = BoundController.Get();
    if (PC && PC->GetLocalPlayer())
    {
        auto* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
        if (Input && MenuMapping) { if (bOpen) Input->AddMappingContext(MenuMapping, 100); else Input->RemoveMappingContext(MenuMapping); }
        const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
        if (!Runtime || !Runtime->bXRActive)
        {
            if (bOpen && !WasOpen) bMouseCursorBefore = PC->bShowMouseCursor;
            PC->bShowMouseCursor = bOpen ? true : bMouseCursorBefore;
            if (bOpen)
            {
                FInputModeGameAndUI Mode;
                Mode.SetHideCursorDuringCapture(false);
                Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
                PC->SetInputMode(Mode);
            }
            else PC->SetInputMode(FInputModeGameOnly());
        }
    }
    if (auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner()))
    {
        const auto* Director = GetDirector();
        if (Runtime->Locomotion) Runtime->Locomotion->bEnabled = !bOpen && (!Director || !Director->IsSceneInputBlocked());
    }
    bApplyArmed = false; bNextArmed = false;
    Refresh();
}
void UGratiaMenu::EndPlay(const EEndPlayReason::Type Reason)
{
    Close();
    BindInput(nullptr);
    if (Pointer) Pointer->Deactivate();
    Super::EndPlay(Reason);
}
void UGratiaMenu::ReleasePointer()
{
    const bool WasDown = bPointerDown;
    bPointerDown = false;
    if (!Pointer) return;
    if (WasDown)
    {
        // Move off every widget before cancelling a captured press. Tracking loss/close must
        // release Slate capture without clicking the button that happened to be under the ray.
        const EWidgetInteractionSource PreviousSource = Pointer->InteractionSource;
        Pointer->InteractionSource = EWidgetInteractionSource::Custom;
        Pointer->SetCustomHitResult(FHitResult());
        Pointer->bEnableHitTesting = true;
        Pointer->TickComponent(0.0f, LEVELTICK_All, &Pointer->PrimaryComponentTick);
        Pointer->ReleasePointerKey(EKeys::LeftMouseButton);
        Pointer->InteractionSource = PreviousSource;
    }
    Pointer->bEnableHitTesting = false;
}
void UGratiaMenu::UpdatePointer(float Delta, APlayerController* PC)
{
    auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    const bool XR = Runtime && Runtime->bXRActive;
    UMotionControllerComponent* Source = nullptr;
    if (XR && Runtime->GetPlayerPawn())
    {
        TArray<UMotionControllerComponent*> Controllers;
        Runtime->GetPlayerPawn()->GetComponents(Controllers);
        for (auto* Controller : Controllers)
            if (Controller->IsTracked() && Controller->GetTrackingMotionSource() == FName(TEXT("RightAim"))) { Source = Controller; break; }
        if (!Source) for (auto* Controller : Controllers)
            if (Controller->IsTracked() && (Controller->GetTrackingMotionSource() == FName(TEXT("RightGrip")) || Controller->GetTrackingMotionSource() == FName(TEXT("Right")))) { Source = Controller; break; }
    }
    const bool Allowed = bOpen && Pointer && PC && (!XR || (Source && Runtime->IsHandInteractionAllowed(false)));
    if (!Allowed)
    {
        ReleasePointer();
        if (Laser) Laser->SetVisibility(false);
        if (Dot) Dot->SetVisibility(false);
        return;
    }
    Pointer->InteractionDistance = PointerDistanceCm;
    Pointer->InteractionSource = XR ? EWidgetInteractionSource::World : EWidgetInteractionSource::Mouse;
    if (XR)
    {
        const FQuat Offset = Source->GetTrackingMotionSource() == FName(TEXT("RightAim")) ? FQuat::Identity : GripPointerRotation.Quaternion();
        Pointer->SetWorldLocationAndRotation(Source->GetComponentLocation(), Source->GetComponentQuat() * Offset);
    }
    Pointer->bEnableHitTesting = true;
    Pointer->TickComponent(Delta, LEVELTICK_All, &Pointer->PrimaryComponentTick);
    if (Laser)
    {
        Laser->SetVisibility(XR);
        const FVector Start = Pointer->GetComponentLocation();
        const FVector End = Pointer->GetHoveredWidgetComponent() == Panel ? FVector(Pointer->GetLastHitResult().ImpactPoint) : Start + Pointer->GetForwardVector() * PointerDistanceCm;
        const FVector Segment = End - Start;
        Laser->SetWorldLocation((Start + End) * 0.5);
        Laser->SetWorldRotation(FRotationMatrix::MakeFromZ(Segment).Rotator());
        Laser->SetWorldScale3D(FVector(0.002, 0.002, Segment.Size() / 100.0));
        if (Dot)
        {
            Dot->SetVisibility(XR && Pointer->GetHoveredWidgetComponent() == Panel);
            Dot->SetWorldLocation(End); Dot->SetWorldScale3D(FVector(0.012));
        }
    }
    const float Apply = GratiaMenuValue(ActionInput, ApplyAction);
    const bool MouseDown = !XR && PC->IsInputKeyDown(EKeys::LeftMouseButton);
    const bool Pressed = XR ? Apply > 0.65f : MouseDown;
    if (!Pressed)
    {
        if (bPointerDown) { bPointerDown = false; Pointer->ReleasePointerKey(EKeys::LeftMouseButton); }
        bApplyArmed = true;
    }
    else if (bApplyArmed && !bPointerDown)
    {
        bApplyArmed = false;
        if (Pointer->GetHoveredWidgetComponent() == Panel && Pointer->IsOverInteractableWidget())
        {
            bPointerDown = true;
            Pointer->PressPointerKey(EKeys::LeftMouseButton);
        }
        else if (XR && Widget) Widget->Activate();
    }
    if (!XR && bOpen)
    {
        const float Wheel = PC->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
        if (FMath::Abs(Wheel) > UE_SMALL_NUMBER) Pointer->ScrollWheel(Wheel);
    }
}
void UGratiaMenu::ApplyQuality(int32 Profile, bool bResetMotion)
{
    if (!Character.IsValid() || !Character->Interaction) return;
    Profile = FMath::Clamp(Profile, 0, 2);
    auto* I = Character->Interaction.Get(); I->Quality = Profile;
    if (bResetMotion)
    {
        I->bBodyMotion = IsActionAvailable(EGratiaMenuAction::Body);
        I->bEarMotion = IsActionAvailable(EGratiaMenuAction::Ears);
        I->bHairMotion = Profile > 0 && IsActionAvailable(EGratiaMenuAction::Hair);
        I->bClothMotion = Profile > 0 && IsActionAvailable(EGratiaMenuAction::Cloth);
        I->bLocalSpring = IsActionAvailable(EGratiaMenuAction::Springs);
    }
    auto* PC = UGameplayStatics::GetPlayerController(this, 0);
    // One resolution scale per profile. In the headset it sizes the runtime's render target (vr.PixelDensity: less
    // to render and to composite, no upscale pass); r.ScreenPercentage would scale the views inside it once more
    // (Medium was 0.85 x 0.85 = 72 % per axis). On a monitor (and in the stereo emulation) it is the screen percentage.
    const float Scale = Profile == 0 ? 0.70f : Profile == 1 ? 0.85f : 1.0f;
    IHeadMountedDisplay* HMD = GEngine && GEngine->XRSystem.IsValid() && GEngine->StereoRenderingDevice.IsValid()
        && GEngine->StereoRenderingDevice->IsStereoEnabled() ? GEngine->XRSystem->GetHMDDevice() : nullptr;
    if (HMD) HMD->SetPixelDensity(Scale);
    if (PC)
    {
        PC->ConsoleCommand(FString::Printf(TEXT("r.ScreenPercentage %d"), HMD ? 100 : FMath::RoundToInt(Scale * 100.0f)), false);
        PC->ConsoleCommand(FString::Printf(TEXT("sg.EffectsQuality %d"), Profile), false);
        // Forward MSAA: 4x costs ~1.6 ms of the VR frame in the guesthouse (RTX 3060, 2 x 2572^2 at 85 %),
        // so Low and Medium use 2x and High keeps 4x for stronger GPUs. -GratiaMSAA=N overrides it for measurements.
        int32 Samples = Profile == 2 ? 4 : 2;
        FParse::Value(FCommandLine::Get(), TEXT("GratiaMSAA="), Samples);
        PC->ConsoleCommand(FString::Printf(TEXT("r.MSAACount %d"), Samples), false);
    }
    const FIntPoint Target = HMD ? HMD->GetIdealRenderTargetSize() : FIntPoint::ZeroValue;
    UE_LOG(LogGratiaMenu, Display, TEXT("Quality=%s scale=%.2f via=%s target=%dx%d; hardware performance acceptance pending"), *QualityLabel(), Scale,
        HMD ? TEXT("pixel_density") : TEXT("screen_percentage"), Target.X, Target.Y);
}
FString UGratiaMenu::QualityLabel() const
{
    const int32 P = Character.IsValid() && Character->Interaction ? Character->Interaction->Quality : 1;
    return P == 0 ? TEXT("Низкое") : P == 2 ? TEXT("Высокое") : TEXT("Среднее");
}
const TCHAR* UGratiaMenu::ViewLabel() const
{
    const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    if (Runtime && Runtime->IsPartnerView()) return TEXT("глазами партнёра");
    FTransform Eye;
    return Character.IsValid() && Character->PerformanceStage && Character->PerformanceStage->GetViewpoint(Eye) ? TEXT("свободный вид") : TEXT("недоступен в этой позе");
}
bool UGratiaMenu::IsActionAvailable(EGratiaMenuAction Action, int32 Param) const
{
    const auto* Director = GetDirector();
    const auto* I = Character.IsValid() ? Character->Interaction.Get() : nullptr;
    const auto* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    const bool CharacterScene = !Director || !Director->IsSceneInputBlocked();
    switch (Action)
    {
    case EGratiaMenuAction::Tab: case EGratiaMenuAction::Close: case EGratiaMenuAction::Quit: return true;
    case EGratiaMenuAction::TrackPrev: case EGratiaMenuAction::TrackNext: return Director && Director->Library && !Director->Library->Playlist.IsEmpty();
    case EGratiaMenuAction::StartScene: return Director && Director->Library && Director->Library->Scenes.IsValidIndex(Param) && (Director->GetState() == EGratiaFlowState::Lobby || Director->GetState() == EGratiaFlowState::Playing);
    case EGratiaMenuAction::Lobby: return IsInScene();
    case EGratiaMenuAction::Pose: case EGratiaMenuAction::Demo: return I && CharacterScene && (!Director || !Director->IsPerformanceScene());
    case EGratiaMenuAction::Mood: case EGratiaMenuAction::Reset: return I && CharacterScene;
    case EGratiaMenuAction::Pause: case EGratiaMenuAction::Restart: case EGratiaMenuAction::PrevPart: case EGratiaMenuAction::NextPart: case EGratiaMenuAction::SpeedDown: case EGratiaMenuAction::SpeedUp: return Director && Director->IsPerformanceScene();
    case EGratiaMenuAction::PartnerView: { FTransform Eye; return Character.IsValid() && Character->PerformanceStage && Character->PerformanceStage->GetViewpoint(Eye); }
    case EGratiaMenuAction::MusicDown: case EGratiaMenuAction::MusicUp: case EGratiaMenuAction::HapticsDown: case EGratiaMenuAction::HapticsUp:
    case EGratiaMenuAction::VoiceDown: case EGratiaMenuAction::VoiceUp: case EGratiaMenuAction::Captions: case EGratiaMenuAction::ResetSettings:
        return Director && Director->GetUserSettings();
    case EGratiaMenuAction::TurnMode: case EGratiaMenuAction::WalkSpeed:
    {
        const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
        return Runtime && Runtime->Locomotion && Director && Director->GetUserSettings();
    }
    case EGratiaMenuAction::HeightDown: case EGratiaMenuAction::HeightUp: case EGratiaMenuAction::Recenter: return Cast<AGratiaStage1Runtime>(GetOwner()) != nullptr;
    case EGratiaMenuAction::Hair: case EGratiaMenuAction::Cloth: case EGratiaMenuAction::Body: case EGratiaMenuAction::Ears:
    {
        if (!I || !Profile || !(Profile->Capabilities.bSecondaryPhysics || Profile->Capabilities.bLocalSprings)) return false;
        const int32 Group = Action == EGratiaMenuAction::Hair ? 1 : Action == EGratiaMenuAction::Cloth ? 2 : Action == EGratiaMenuAction::Body ? 3 : 4;
        return Profile->SecondaryBones.ContainsByPredicate([Group](const auto& Bone) { return Bone.Group == Group; });
    }
    case EGratiaMenuAction::Primitive:
    {
        const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
        return Runtime && I && Profile && Profile->Penetration.bEnabled && CharacterScene;
    }
    case EGratiaMenuAction::PrimitiveSize:
    {
        const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
        return Runtime && Runtime->IsPrimitiveShown();
    }
    case EGratiaMenuAction::Physics: return I && Profile && Profile->Capabilities.bSecondaryPhysics;
    case EGratiaMenuAction::Springs: return I && Profile && Profile->Capabilities.bLocalSprings;
    case EGratiaMenuAction::Sound: return I && Profile && Profile->Capabilities.bSound;
    default: return I != nullptr;
    }
}
void UGratiaMenu::Execute(EGratiaMenuAction Action, int32 Param)
{
    if (!IsActionAvailable(Action, Param)) { UE_LOG(LogGratiaMenu, Display, TEXT("MENU action=%d unavailable for current state/profile"), int32(Action)); return; }
    auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    auto* Director = GetDirector();
    auto* I = Character.IsValid() ? Character->Interaction.Get() : nullptr;
    switch (Action)
    {
    case EGratiaMenuAction::Tab: if (Widget) Widget->ShowPage(Param); break;
    case EGratiaMenuAction::StartScene: if (Director && !Director->StartScene(Param)) UE_LOG(LogGratiaMenu, Warning, TEXT("MENU scene index=%d could not start"), Param); break;
    case EGratiaMenuAction::Lobby: if (Director) Director->ExitToLobby(); break;
    case EGratiaMenuAction::Close: Close(); break;
    case EGratiaMenuAction::Quit: if (Director) Director->SaveUserSettings(); UKismetSystemLibrary::QuitGame(this, BoundController.Get(), EQuitPreference::Quit, false); return;
    case EGratiaMenuAction::Pause: Director->TogglePause(); break;
    case EGratiaMenuAction::Restart: Director->Restart(); break;
    case EGratiaMenuAction::PrevPart: Director->StepPart(-1); break;
    case EGratiaMenuAction::NextPart: Director->StepPart(1); break;
    case EGratiaMenuAction::SpeedDown: Director->StepSpeed(-1); break;
    case EGratiaMenuAction::SpeedUp: Director->StepSpeed(1); break;
    case EGratiaMenuAction::PartnerView: if (Runtime) Runtime->SetPartnerView(!Runtime->IsPartnerView()); break;
    case EGratiaMenuAction::Pose:
        // Free play switches between stances; the diagnostic poses and performances stay on F2 outside the scenes.
        if (Director && Director->IsActive()) Character->CycleStance();
        else Character->CyclePreviewPose();
        break;
    case EGratiaMenuAction::Mood: I->Mood = FMath::Clamp(Param, 0, 2); break;
    case EGratiaMenuAction::Demo: I->bDemo = !I->bDemo; break;
    case EGratiaMenuAction::Reset:
        if (Director && Director->IsPerformanceScene()) { I->ResetState(); Director->Restart(); }
        else Character->ResetToIdle();
        if (Runtime) Runtime->ResetHeight();
        break;
    case EGratiaMenuAction::Hair: I->bHairMotion = !I->bHairMotion; break;
    case EGratiaMenuAction::Cloth: I->bClothMotion = !I->bClothMotion; break;
    case EGratiaMenuAction::Body: I->bBodyMotion = !I->bBodyMotion; break;
    case EGratiaMenuAction::Ears: I->bEarMotion = !I->bEarMotion; break;
    case EGratiaMenuAction::Physics: I->bPhysicalMotion = !I->bPhysicalMotion; break;
    case EGratiaMenuAction::Springs: I->bLocalSpring = !I->bLocalSpring; break;
    case EGratiaMenuAction::Quality: ApplyQuality(Param); break;
    case EGratiaMenuAction::Sound: I->bSound = !I->bSound; break;
    case EGratiaMenuAction::MusicDown: case EGratiaMenuAction::MusicUp: Director->SetMusicVolume(Director->GetUserSettings()->MusicVolume + (Action == EGratiaMenuAction::MusicUp ? 0.1f : -0.1f)); break;
    case EGratiaMenuAction::HapticsDown: case EGratiaMenuAction::HapticsUp: Director->SetHapticsScale(Director->GetUserSettings()->HapticsScale + (Action == EGratiaMenuAction::HapticsUp ? 0.1f : -0.1f)); break;
    case EGratiaMenuAction::VoiceDown: case EGratiaMenuAction::VoiceUp: Director->SetVoiceVolume(Director->GetUserSettings()->VoiceVolume + (Action == EGratiaMenuAction::VoiceUp ? 0.1f : -0.1f)); break;
    case EGratiaMenuAction::Captions: Director->SetCaptions(!Director->GetUserSettings()->bCaptions); break;
    case EGratiaMenuAction::TurnMode: Director->SetComfort(Param, Director->GetUserSettings()->WalkSpeed); break;
    case EGratiaMenuAction::WalkSpeed: Director->SetComfort(Director->GetUserSettings()->TurnMode, Param); break;
    case EGratiaMenuAction::ResetSettings: Director->ResetUserSettings(); break;
    case EGratiaMenuAction::HeightDown: case EGratiaMenuAction::HeightUp: if (Runtime) Runtime->AdjustHeight(Action == EGratiaMenuAction::HeightUp ? Runtime->HeightStepCm : -Runtime->HeightStepCm); break;
    case EGratiaMenuAction::Recenter: if (Runtime) Runtime->Recenter(); break;
    case EGratiaMenuAction::TrackPrev: if (Director) Director->PreviousTrack(); break;
    case EGratiaMenuAction::TrackNext: if (Director) Director->NextTrack(); break;
    case EGratiaMenuAction::Primitive: if (Runtime) Runtime->SetPrimitiveShown(!Runtime->IsPrimitiveShown()); break;
    case EGratiaMenuAction::PrimitiveSize: if (Runtime) Runtime->CyclePrimitiveSize(); break;
    }
    if (Director) Director->SaveUserSettings();
    Refresh();
}
bool UGratiaMenu::IsSelected(EGratiaMenuAction Action, int32 Param) const
{
    const auto* I = Character.IsValid() ? Character->Interaction.Get() : nullptr;
    const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    const auto* Director = GetDirector();
    if (const auto* Settings = Director ? Director->GetUserSettings() : nullptr)
    {
        if (Action == EGratiaMenuAction::Captions) return Settings->bCaptions;
        if (Action == EGratiaMenuAction::TurnMode) return Settings->TurnMode == Param;
        if (Action == EGratiaMenuAction::WalkSpeed) return Settings->WalkSpeed == Param;
    }
    if (!I) return false;
    switch (Action)
    {
    case EGratiaMenuAction::Mood: return I->Mood == Param;
    case EGratiaMenuAction::Quality: return I->Quality == Param;
    case EGratiaMenuAction::Demo: return I->bDemo;
    case EGratiaMenuAction::Hair: return I->bHairMotion;
    case EGratiaMenuAction::Cloth: return I->bClothMotion;
    case EGratiaMenuAction::Body: return I->bBodyMotion;
    case EGratiaMenuAction::Ears: return I->bEarMotion;
    case EGratiaMenuAction::Physics: return I->bPhysicalMotion;
    case EGratiaMenuAction::Springs: return I->bLocalSpring;
    case EGratiaMenuAction::Sound: return I->bSound;
    case EGratiaMenuAction::PartnerView: return Runtime && Runtime->IsPartnerView();
    case EGratiaMenuAction::Primitive: return Runtime && Runtime->IsPrimitiveShown();
    default: return false;
    }
}
FString UGratiaMenu::ValueFor(EGratiaMenuAction Action) const
{
    const auto* Director = GetDirector();
    const auto* Settings = Director ? Director->GetUserSettings() : nullptr;
    const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    auto Percent = [](float Value) { return FString::Printf(TEXT("%d %%"), FMath::RoundToInt(Value * 100.0f)); };
    switch (Action)
    {
    case EGratiaMenuAction::MusicUp: return Settings ? Percent(Settings->MusicVolume) : TEXT("—");
    case EGratiaMenuAction::VoiceUp: return Settings ? Percent(Settings->VoiceVolume) : TEXT("—");
    case EGratiaMenuAction::HapticsUp: return Settings ? Percent(Settings->HapticsScale) : TEXT("—");
    case EGratiaMenuAction::HeightUp: return Runtime ? FString::Printf(TEXT("%+.0f см"), Runtime->HeightOffsetCm) : TEXT("—");
    case EGratiaMenuAction::SpeedUp: return Character.IsValid() && Director && Director->IsPerformanceScene()
        ? FString::Printf(TEXT("%.2g×"), Character->GetPerformanceRate()) : TEXT("—");
    case EGratiaMenuAction::Pose: return Character.IsValid() && IsActionAvailable(Action) ? Character->GetPoseMenuLabel() : TEXT("—");
    case EGratiaMenuAction::PrimitiveSize: return Runtime && Runtime->IsPrimitiveShown() ? Runtime->GetPrimitiveLabel() : TEXT("—");
    case EGratiaMenuAction::PartnerView: return ViewLabel();
    default: return TEXT("");
    }
}
FString UGratiaMenu::LabelFor(EGratiaMenuAction Action, int32 Param) const
{
    const auto* I = Character.IsValid() ? Character->Interaction.Get() : nullptr;
    const auto* Director = GetDirector();
    const auto* Settings = Director ? Director->GetUserSettings() : nullptr;
    const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    const TCHAR* Name = TEXT("");
    switch (Action)
    {
    case EGratiaMenuAction::Pose: return TEXT("Поза: ") + (Character.IsValid() ? Character->GetPoseMenuLabel() : TEXT("недоступна"));
    case EGratiaMenuAction::Pause: return Character.IsValid() && Character->IsPerformancePaused() ? TEXT("Продолжить") : TEXT("Пауза");
    case EGratiaMenuAction::PartnerView: return FString(TEXT("Вид: ")) + ViewLabel();
    case EGratiaMenuAction::MusicUp: return FString::Printf(TEXT("Музыка: %d%%   +"), FMath::RoundToInt((Settings ? Settings->MusicVolume : 0) * 100));
    case EGratiaMenuAction::HapticsUp: return FString::Printf(TEXT("Вибрация: %d%%   +"), FMath::RoundToInt((Settings ? Settings->HapticsScale : 0) * 100));
    case EGratiaMenuAction::HeightUp: return FString::Printf(TEXT("Высота глаз: %+.0f см   +"), Runtime ? Runtime->HeightOffsetCm : 0.0f);
    case EGratiaMenuAction::PrimitiveSize: return TEXT("Размер: ") + (Runtime && Runtime->IsPrimitiveShown() ? Runtime->GetPrimitiveLabel() : FString(TEXT("включите примитив")));
    case EGratiaMenuAction::Primitive: Name = TEXT("Примитив"); break;
    case EGratiaMenuAction::Demo: Name = TEXT("Демо реакций"); break;
    case EGratiaMenuAction::Hair: Name = TEXT("Волосы"); break;
    case EGratiaMenuAction::Cloth: Name = TEXT("Одежда"); break;
    case EGratiaMenuAction::Body: Name = TEXT("Тело"); break;
    case EGratiaMenuAction::Ears: Name = TEXT("Уши / хвост"); break;
    case EGratiaMenuAction::Physics: Name = TEXT("Вторичная физика"); break;
    case EGratiaMenuAction::Springs: Name = TEXT("Локальные пружины"); break;
    case EGratiaMenuAction::Sound: Name = TEXT("Голос реакций"); break;
    case EGratiaMenuAction::Captions: Name = TEXT("Реплики в облачке"); break;
    default: return TEXT("");
    }
    return FString::Printf(TEXT("%s: %s"), Name, IsActionAvailable(Action, Param) && I ? GratiaOnOff(IsSelected(Action, Param)) : TEXT("недоступно"));
}
void UGratiaMenu::ApplySelected()
{
    const EGratiaMenuAction Rows[] = {EGratiaMenuAction::Pose, EGratiaMenuAction::Mood, EGratiaMenuAction::Demo, EGratiaMenuAction::Recenter,
        EGratiaMenuAction::HeightUp, EGratiaMenuAction::HeightDown, EGratiaMenuAction::Reset, EGratiaMenuAction::Quality,
        EGratiaMenuAction::Hair, EGratiaMenuAction::Cloth, EGratiaMenuAction::Body, EGratiaMenuAction::Springs,
        EGratiaMenuAction::Physics, EGratiaMenuAction::Sound, EGratiaMenuAction::Ears, EGratiaMenuAction::PartnerView};
    if (Selected < 0 || Selected >= UE_ARRAY_COUNT(Rows)) return;
    const auto* I = Character.IsValid() ? Character->Interaction.Get() : nullptr;
    const int32 Param = I && Selected == 1 ? (I->Mood + 1) % 3 : I && Selected == 7 ? (I->Quality + 1) % 3 : 0;
    Execute(Rows[Selected], Param);
}
void UGratiaMenu::Refresh() { if (Widget) Widget->RefreshState(); }
FString UGratiaMenu::GetDiagnostics() const
{
    const auto* PC = BoundController.Get();
    const auto* Input = PC && PC->GetLocalPlayer() ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
    return FString::Printf(TEXT("open=%d widget=%d player=%d actions=%d context=%d pointer_down=%d"), bOpen, Widget != nullptr, PC != nullptr,
        ToggleAction && NextAction && ApplyAction, Input && MenuMapping && Input->HasMappingContext(MenuMapping), bPointerDown);
}
void UGratiaMenu::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    auto* PC = UGameplayStatics::GetPlayerController(this, 0);
    BindInput(PC);
    if (HapticSeconds > 0.0f && (HapticSeconds -= Delta) <= 0.0f && PC) PC->SetHapticsByValue(0.0f, 0.0f, EControllerHand::Right);
    UpdateFollow(Delta);
    if (ShotStep != INDEX_NONE) TickShots(Delta);
    const auto* Director = GetDirector();
    if (auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner()))
        if (Runtime->Locomotion) Runtime->Locomotion->bEnabled = !bOpen && (!Director || !Director->IsSceneInputBlocked());
    if (!PC) return;
    const float ToggleInput = GratiaMenuValue(ActionInput, ToggleAction);
    if (ToggleInput < 0.2f) bToggleArmed = true;
    if ((ToggleInput > 0.5f && bToggleArmed) || PC->WasInputKeyJustPressed(EKeys::F4)) { Toggle(); bToggleArmed = false; }
    if (bOpen && PC->WasInputKeyJustPressed(EKeys::Escape)) Close();
    if (!bOpen) return;
    if (!EnsureWidget(PC)) return;
    RefreshSeconds += FMath::Max(0.0f, Delta);
    if (RefreshSeconds >= 0.2f) { RefreshSeconds = 0.0f; Refresh(); }
    const float Next = GratiaMenuValue(ActionInput, NextAction);
    if (FMath::Abs(Next) < 0.2f) bNextArmed = true;
    if ((bNextArmed && FMath::Abs(Next) >= 0.5f) || PC->WasInputKeyJustPressed(EKeys::Down) || PC->WasInputKeyJustPressed(EKeys::Up))
    {
        Widget->Navigate(Next < 0 || PC->WasInputKeyJustPressed(EKeys::Up) ? -1 : 1);
        bNextArmed = false;
    }
    if (PC->WasInputKeyJustPressed(EKeys::Enter)) Widget->Activate();
    if (bOpen) UpdatePointer(Delta, PC);
}
bool UGratiaMenu::RunChecks()
{
    if (!Character.IsValid() || !Character->Interaction || !ToggleAction || !NextAction || !ApplyAction || !MenuMapping) return false;
    for (int32 I = 0; I < 10; ++I) { Selected = 0; ApplySelected(); Selected = 1; ApplySelected(); }
    Selected = 6; ApplySelected();
    bool Pass = Character->IsIdlePreview() && Character->Interaction->ActiveZone == INDEX_NONE;
    for (int32 Profile = 0; Profile < 3; ++Profile) { ApplyQuality(Profile); Pass &= Character->Interaction->Quality == Profile; }
    ApplyQuality(1); Character->Interaction->Mood = 0;
    Close();
    Toggle();
    const FVector Position = Panel ? Panel->GetComponentLocation() : FVector::ZeroVector;
    const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    Pass &= bOpen && Widget && Runtime && Runtime->Locomotion && !Runtime->Locomotion->bEnabled;
    Toggle();
    Pass &= !bOpen && Panel && Panel->GetComponentLocation().Equals(Position, 0.001);
    // Every page builds and offers something to click; the switcher follows the tabs.
    int32 PagesWithItems = 0;
    if (Widget)
    {
        for (int32 Page = 0; Page < UGratiaMenuWidget::PageCount; ++Page)
        {
            Widget->ShowPage(Page);
            PagesWithItems += Widget->GetPage() == Page && Widget->CountVisibleItems() > 0;
        }
        Widget->ShowPage(UGratiaMenuWidget::Scenes);
    }
    Pass &= PagesWithItems == UGratiaMenuWidget::PageCount;
    // Placement: with the head 40 cm above the floor the whole panel still opens above the floor, facing the eyes;
    // standing, it opens just below eye level.
    bool bPlacement = false;
    UCameraComponent* View = Runtime ? Runtime->GetPlayerCamera() : nullptr;
    if (View && Runtime->GetPlayerPawn())
    {
        const FVector Saved = View->GetComponentLocation();
        const double Floor = Runtime->GetPlayerPawn()->GetActorLocation().Z;
        const double HalfHeight = 0.5 * PanelResolution.Y * PanelScale;
        View->SetWorldLocation(FVector(Saved.X, Saved.Y, Floor + 40.0));
        const FTransform Low = ComputePanelTransform(View);
        View->SetWorldLocation(FVector(Saved.X, Saved.Y, Floor + 165.0));
        const FTransform High = ComputePanelTransform(View);
        View->SetWorldLocation(Saved);
        const FVector LowToEye = FVector(Saved.X, Saved.Y, Floor + 40.0) - Low.GetLocation();
        bPlacement = Low.GetLocation().Z - HalfHeight >= Floor + PanelFloorClearanceCm - 0.5
            && FVector::DotProduct(Low.GetRotation().GetForwardVector(), LowToEye.GetSafeNormal()) > 0.99
            && FMath::IsNearlyEqual(High.GetLocation().Z, Floor + 165.0 - PanelBelowEyeCm, 1.0);
        UE_LOG(LogGratiaMenu, Display, TEXT("MENU PLACEMENT: low head bottom=%.1fcm above floor, standing centre=%.1fcm below eyes"),
            Low.GetLocation().Z - HalfHeight - Floor, Floor + 165.0 - High.GetLocation().Z);
    }
    Pass &= bPlacement;
    Selected = 0;
    UE_LOG(LogGratiaMenu, Display, TEXT("MENU SELFTEST: pose/mood/reset/quality/world widget anchor, %d/%d pages, placement=%s: %s"),
        PagesWithItems, int32(UGratiaMenuWidget::PageCount), bPlacement ? TEXT("yes") : TEXT("no"), Pass ? TEXT("PASS") : TEXT("FAIL"));
    return Pass;
}
