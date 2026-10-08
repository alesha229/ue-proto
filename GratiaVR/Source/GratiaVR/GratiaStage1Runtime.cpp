#include "GratiaStage1Runtime.h"
#include "GratiaPerformanceStage.h"
#include "GratiaRuntimeVerification.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaLocomotion.h"
#include "GratiaInteraction.h"
#include "GratiaMenu.h"
#include "GratiaMenuWidget.h"
#include "GratiaSceneDirector.h"
#include "GratiaSceneFlowVerification.h"
#include "GratiaSceneLibrary.h"
#include "GratiaSecondaryMotion.h"
#include "GratiaSoftBodyInteraction.h"
#include "GratiaBodySurface.h"
#include "GratiaCharacterProfile.h"
#include "GratiaHandAnimInstance.h"
#include "GratiaBuildInfo.h"
#include "GratiaHandInput.h"
#include "GratiaAnimInstance.h"
#include "GratiaPenetration.h"
#include "GratiaPenetrator.h"
#include "GratiaChannelShots.h"

#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Kismet/GameplayStatics.h"
#include "MotionControllerComponent.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "GratiaStage1HUD.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaStage1, Log, All);

namespace
{
    const TCHAR* HandStateLabel(EGratiaHandState State)
    {
        switch (State)
        {
        case EGratiaHandState::Acquiring: return TEXT("ACQUIRING (no contact)");
        case EGratiaHandState::Recovering: return TEXT("RECOVERING (no contact)");
        case EGratiaHandState::Tracked: return TEXT("TRACKED");
        default: return TEXT("UNTRACKED (no contact)");
        }
    }

    bool IsFiniteTransform(const FTransform& Transform)
    {
        return !Transform.ContainsNaN() && Transform.GetRotation().IsNormalized();
    }

    FTransform BlendTransform(const FTransform& From, const FTransform& To, float Alpha)
    {
        FQuat Rotation = FQuat::Slerp(From.GetRotation(), To.GetRotation(), Alpha);
        Rotation.Normalize();
        return FTransform(Rotation, FMath::Lerp(From.GetLocation(), To.GetLocation(), Alpha), To.GetScale3D());
    }

    float GratiaPercentile(TArray<float> Values, float Fraction)
    {
        if (Values.IsEmpty()) return 0.0f;
        Values.Sort();
        const float Rank = (Values.Num() - 1) * FMath::Clamp(Fraction, 0.0f, 1.0f);
        const int32 Low = FMath::FloorToInt(Rank);
        return FMath::Lerp(Values[Low], Values[FMath::Min(Low + 1, Values.Num() - 1)], Rank - Low);
    }
}

AGratiaStage1Runtime::AGratiaStage1Runtime()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CalibrationAnchor"));
    Locomotion = CreateDefaultSubobject<UGratiaLocomotion>(TEXT("SmoothLocomotion"));
    Menu = CreateDefaultSubobject<UGratiaMenu>(TEXT("WorldMenu"));
    SceneDirector = CreateDefaultSubobject<UGratiaSceneDirector>(TEXT("SceneDirector"));
    SceneFlowVerification = CreateDefaultSubobject<UGratiaSceneFlowVerification>(TEXT("SceneFlowVerification"));
    SceneDirector->AddTickPrerequisiteActor(this);
    Menu->AddTickPrerequisiteComponent(SceneDirector);
    SceneFlowVerification->AddTickPrerequisiteComponent(SceneDirector);
    HandInput = CreateDefaultSubobject<UGratiaHandInput>(TEXT("HandInput"));
    Verification = CreateDefaultSubobject<UGratiaRuntimeVerification>(TEXT("RuntimeVerification"));
    DebugPanel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DebugPanel"));
    DebugPanel->SetupAttachment(RootComponent);
    DebugPanel->SetRelativeLocation(FVector(250.0, 0.0, 140.0));
    DebugPanel->SetRelativeRotation(FRotator(0.0, 180.0, 0.0));
    DebugPanel->SetWorldSize(3.0f);
    DebugPanel->SetHorizontalAlignment(EHTA_Center);
    DebugPanel->SetVerticalAlignment(EVRTA_TextTop);
    DebugPanel->SetTextRenderColor(FColor(160, 240, 215));
    DebugPanel->SetText(FText::FromString(TEXT("GRATIA VR - STAGE 1\nWaiting for player pawn...")));
    DebugPanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AGratiaStage1Runtime::BeginPlay()
{
    Super::BeginPlay();
    Verification->ConfigureFromCommandLine();
    BindPlayer();
    SetTargetCharacter(TargetCharacter.Get());
    FocusTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &AGratiaStage1Runtime::TickFocus), 0.1f);
    if (FParse::Param(FCommandLine::Get(), TEXT("GratiaChannelShots")))
    {
        UGratiaChannelShots* Shots = NewObject<UGratiaChannelShots>(this, TEXT("GratiaChannelShots"));
        AddInstanceComponent(Shots);
        Shots->RegisterComponent();
    }
    UE_LOG(LogGratiaStage1, Display, TEXT("BUILD id=%s commit=%s"), TEXT(GRATIA_BUILD_ID), TEXT(GRATIA_BUILD_COMMIT));
    UE_LOG(LogGratiaStage1, Display, TEXT("Stage 1 runtime started. R=recenter, PgUp/PgDn=height, Home=reset height, F1=debug, F6=primitive on/off, F7=primitive size, F8/F9=toggle forced left/right tracking loss."));
}

void AGratiaStage1Runtime::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    FTSTicker::GetCoreTicker().RemoveTicker(FocusTicker);
    FocusTicker.Reset();
    FlushScenePerformance();
    ReleasePrimitive(LeftHand, true, TEXT("end play"));
    ReleasePrimitive(RightHand, false, TEXT("end play"));
    for (AGratiaPenetrator* Shaft : HandShafts) if (Shaft) Shaft->Destroy();
    HandShafts.Reset();
    UpdateHaptics(LeftHand, true, 0.0f, 0.0f);
    UpdateHaptics(RightHand, false, 0.0f, 0.0f);
    RestoreHand(LeftHand);
    RestoreHand(RightHand);
    if (Camera.IsValid() && bDesktopCameraApplied)
    {
        Camera->SetRelativeTransform(OriginalCameraRelative);
    }
    Super::EndPlay(EndPlayReason);
}

void AGratiaStage1Runtime::SetTargetCharacter(AGratiaPreviewCharacter* Character)
{
    if (TargetCharacter.IsValid() && TargetCharacter.Get() != Character)
    {
        if (TargetCharacter->Penetration)
        {
            TargetCharacter->Penetration->RemoveTickPrerequisiteActor(this);
            TargetCharacter->Penetration->SetPenetrator(nullptr);
        }
        TargetCharacter->Interaction->RemoveTickPrerequisiteActor(this);
        TargetCharacter->Interaction->SetSceneContactActor(nullptr);
        TargetCharacter->Interaction->ResetState();
        if (TargetCharacter->SecondaryMotion) TargetCharacter->SecondaryMotion->ClearHands();
        if (TargetCharacter->SoftBodyInteraction) TargetCharacter->SoftBodyInteraction->ClearHands();
    }
    TargetCharacter = Character;
    if (Menu) Menu->SetCharacter(Character);
    if (Character && Character->Interaction)
    {
        Character->Interaction->AddTickPrerequisiteActor(this);
        Character->Interaction->SetSceneContactActor(SceneContactActor);
        Verification->ConfigureCaptureView();
    }
    if (Character && Character->Penetration)
    {
        Character->Penetration->AddTickPrerequisiteActor(this);
        Character->Penetration->SetPenetrator(Primitive);
    }
}

bool AGratiaStage1Runtime::TickFocus(float Delta)
{
    const bool bLost = bXRActive && FApp::UseVRFocus() && !FApp::HasVRFocus();
    if (bLost != bFocusPaused) SetFocusPaused(bLost);
    return true;
}

void AGratiaStage1Runtime::SetFocusPaused(bool bPaused)
{
    if (bPaused == bFocusPaused) return;
    bFocusPaused = bPaused;
    if (bPaused)
    {
        UpdateHaptics(LeftHand, true, 0.0f, 0.0f);
        UpdateHaptics(RightHand, false, 0.0f, 0.0f);
    }
    // Non-UI sounds (music, voices, rain) pause with the game.
    UGameplayStatics::SetGamePaused(this, bPaused);
    UE_LOG(LogGratiaStage1, Display, TEXT("FOCUS %s: game %s"), bPaused ? TEXT("lost") : TEXT("back"),
        UGameplayStatics::IsGamePaused(this) ? TEXT("paused") : TEXT("running"));
}

bool AGratiaStage1Runtime::SetPrimitiveShown(bool bShown)
{
    if (!bShown)
    {
        ReleasePrimitive(LeftHand, true, TEXT("removed"));
        ReleasePrimitive(RightHand, false, TEXT("removed"));
        if (TargetCharacter.IsValid() && TargetCharacter->Penetration) TargetCharacter->Penetration->SetPenetrator(nullptr);
        if (Primitive) Primitive->Destroy();
        Primitive = nullptr;
        return true;
    }
    if (Primitive) return true;
    const UCameraComponent* View = Camera.Get();
    if (!View || !GetWorld()) return false;
    // In front of the chest, pointing ahead and slightly down, within reach of either hand.
    const FRotator Yaw(0.0, View->GetComponentRotation().Yaw, 0.0);
    const FVector Location = View->GetComponentLocation() + Yaw.Vector() * 32.0 - FVector(0, 0, 32.0);
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Primitive = GetWorld()->SpawnActor<AGratiaPenetrator>(PrimitiveClass ? PrimitiveClass.Get() : AGratiaPenetrator::StaticClass(),
        Location, Yaw + FRotator(-20.0, 0.0, 0.0), Parameters);
    if (!Primitive) return false;
    if (TargetCharacter.IsValid() && TargetCharacter->Penetration) TargetCharacter->Penetration->SetPenetrator(Primitive);
    UE_LOG(LogGratiaStage1, Display, TEXT("PRIMITIVE shown size=%s; grip near the handle picks it up"), *Primitive->GetSizeLabel());
    return true;
}

void AGratiaStage1Runtime::CyclePrimitiveSize()
{
    if (!Primitive) return;
    Primitive->CycleSize();
    UE_LOG(LogGratiaStage1, Display, TEXT("PRIMITIVE size=%s"), *Primitive->GetSizeLabel());
}

FString AGratiaStage1Runtime::GetPrimitiveLabel() const
{
    return Primitive ? Primitive->GetSizeLabel() : FString(TEXT("—"));
}

void AGratiaStage1Runtime::ReleasePrimitive(FHandProxy& Hand, bool bLeft, const TCHAR* Reason)
{
    if (!Hand.bHoldsPrimitive) return;
    Hand.bHoldsPrimitive = false;
    if (Primitive && Primitive->GetHeldHand() == (bLeft ? 0 : 1)) Primitive->SetHeld(INDEX_NONE);
    UE_LOG(LogGratiaStage1, Display, TEXT("PRIMITIVE_RELEASE hand=%s reason=%s"), bLeft ? TEXT("L") : TEXT("R"), Reason);
}

void AGratiaStage1Runtime::UpdatePrimitiveGrab(FHandProxy& Hand, bool bLeft, const FTransform& Target)
{
    const float Grip = HandInput ? HandInput->GetGrip(bLeft) : 0.0f;
    const FTransform Controller(Target.GetRotation(), Target.GetLocation());
    if (Hand.bHoldsPrimitive)
    {
        if (!Primitive) { Hand.bHoldsPrimitive = false; return; }
        if (Grip <= 0.3f) { ReleasePrimitive(Hand, bLeft, TEXT("grip released")); Hand.bPrimitiveArmed = true; return; }
        Primitive->SetBase(Hand.PrimitiveRelative * Controller);
        return;
    }
    if (Grip <= 0.3f) Hand.bPrimitiveArmed = true;
    if (!Primitive || !Hand.bPrimitiveArmed || Grip < 0.6f || !Hand.GripBone.IsNone()) return;
    FGratiaPalmFrame Palm;
    const FVector PalmLocal = Hand.HandAnim.IsValid() && Hand.HandAnim->GetPalmFrame(Palm) ? Palm.Point * Target.GetScale3D() : FVector::ZeroVector;
    if (Primitive->GrabGap(Target.TransformPositionNoScale(PalmLocal)) > Primitive->GrabReachCm) return;
    // Taking it from the other hand passes it over.
    FHandProxy& Other = bLeft ? RightHand : LeftHand;
    ReleasePrimitive(Other, !bLeft, TEXT("passed to the other hand"));
    Hand.bHoldsPrimitive = true;
    Hand.bPrimitiveArmed = false;
    Hand.PrimitiveRelative = Primitive->GetBase().GetRelativeTransform(Controller);
    Primitive->SetHeld(bLeft ? 0 : 1);
    Hand.GripPulse = 0.06f;
    UE_LOG(LogGratiaStage1, Display, TEXT("PRIMITIVE_GRAB hand=%s size=%s"), bLeft ? TEXT("L") : TEXT("R"), *Primitive->GetSizeLabel());
}

void AGratiaStage1Runtime::UpdateHandShaft(FHandProxy& Hand, bool bLeft, const FTransform& Target)
{
    using namespace GratiaPenetration;
    UGratiaPenetration* Penetration = TargetCharacter.IsValid() ? TargetCharacter->Penetration.Get() : nullptr;
    const int32 Side = bLeft ? 0 : 1;
    if (HandShafts.Num() < 2) HandShafts.SetNum(2);
    FGratiaPalmFrame Palm;
    const bool bReady = bHandPenetration && Penetration && Penetration->IsEnabled() && IsSceneInteractionAllowed() && GetWorld()
        && Hand.Gate.State == EGratiaHandState::Tracked && !Hand.bHoldsPrimitive && Hand.GripBone.IsNone()
        && Hand.HandAnim.IsValid() && Hand.HandAnim->GetPalmFrame(Palm);
    EHandShape Shape = EHandShape::None;
    if (bReady)
    {
        // Grip: three fingers. Grip and trigger: fist. Nothing pressed with the thumb resting on the stick: the whole
        // hand, fingers straight. A relaxed hand (thumb up, nothing pressed) does not enter.
        const float Grip = HandInput ? HandInput->GetGrip(bLeft) : 0.0f;
        const float Trigger = HandInput ? HandInput->GetTrigger(bLeft) : 0.0f;
        const bool bThumb = HandInput && HandInput->IsThumbDown(bLeft);
        if (Grip >= 0.6f && Trigger >= 0.6f) Shape = EHandShape::Fist;
        else if (Grip >= 0.6f && Trigger <= 0.35f) Shape = EHandShape::Fingers;
        else if (Grip <= 0.35f && Trigger <= 0.35f && bThumb) Shape = EHandShape::Hand;
    }
    Hand.PenetrationShape = uint8(Shape);
    TObjectPtr<AGratiaPenetrator>& Shaft = HandShafts[Side];
    if (Shape == EHandShape::None)
    {
        if (Shaft) Shaft->SetHeld(INDEX_NONE);
        return;
    }
    if (!Shaft)
    {
        FActorSpawnParameters Parameters;
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Shaft = GetWorld()->SpawnActor<AGratiaPenetrator>(AGratiaPenetrator::StaticClass(), Target, Parameters);
        if (!Shaft) return;
        // The hand itself is what the player sees; its shaft only carries the shape to the channels.
        Shaft->SetActorHiddenInGame(true);
        Shaft->bAnchorWhenReleased = false;
    }
    const double Scale = Target.GetScale3D().GetAbsMax();
    static const FName Labels[] = {NAME_None, TEXT("fingers"), TEXT("hand"), TEXT("fist")};
    Shaft->SetShape(Labels[uint8(Shape)], HandShaft(Shape, Scale));
    // +X along the fingers (palm frame of the hand mesh on the controller pose): the tip at the fingertips or the
    // fist's knuckles, the base back along the forearm.
    const FVector Finger = Target.TransformVectorNoScale(Palm.Finger).GetSafeNormal();
    const FVector Normal = Target.TransformVectorNoScale(Palm.Normal).GetSafeNormal();
    if (Finger.IsNearlyZero() || Normal.IsNearlyZero()) { Shaft->SetHeld(INDEX_NONE); return; }
    const FVector Tip = Target.TransformPosition(Palm.Point) + Finger * HandTipFromPalmCm(Shape) * Scale;
    Shaft->SetBase(FTransform(FRotationMatrix::MakeFromXZ(Finger, Normal).ToQuat(), Tip - Finger * Shaft->GetShaft().Length));
    Shaft->SetHeld(Side);
}

void AGratiaStage1Runtime::ApplyHandInChannel(FHandProxy& Hand, bool bLeft)
{
    UGratiaPenetration* Penetration = TargetCharacter.IsValid() ? TargetCharacter->Penetration.Get() : nullptr;
    AGratiaPenetrator* Shaft = HandShafts.IsValidIndex(bLeft ? 0 : 1) ? HandShafts[bLeft ? 0 : 1].Get() : nullptr;
    FVector Entrance, Inward;
    double Inserted = 0.0, Depth = 0.0;
    const bool bInside = Penetration && Shaft && Penetration->GetEngagedFrame(Shaft, Entrance, Inward, Inserted, Depth);
    if (bInside != Hand.bInChannel)
        UE_LOG(LogGratiaStage1, Display, TEXT("HAND_CHANNEL hand=%s shape=%s %s"), bLeft ? TEXT("L") : TEXT("R"),
            Shaft ? *Shaft->GetSizeLabel() : TEXT("-"), bInside ? TEXT("enter") : TEXT("exit"));
    Hand.bInChannel = bInside;
    FGratiaPalmFrame Palm;
    if (!bInside || !Hand.Visual.IsValid() || !Hand.HandAnim.IsValid() || !Hand.HandAnim->GetPalmFrame(Palm)) return;
    // The rigid hand turns onto the channel axis within the first 3 cm and its tip stops at the channel's depth
    // (the controller may go further; the visible hand does not).
    const FTransform Visual = Hand.Visual->GetComponentTransform();
    const FVector TipLocal = Palm.Point + Palm.Finger * GratiaPenetration::HandTipFromPalmCm(GratiaPenetration::EHandShape(Hand.PenetrationShape));
    const double Align = FMath::Clamp(Inserted / 3.0, 0.0, 1.0);
    const FVector Finger = Visual.TransformVectorNoScale(Palm.Finger).GetSafeNormal();
    const FQuat Rotation = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Finger, Inward), Align) * Visual.GetRotation();
    const FVector Tip = FMath::Lerp(Visual.TransformPosition(TipLocal), Entrance + Inward * FMath::Min(Inserted, Depth), Align);
    const FTransform Placed(Rotation, Tip - Rotation.RotateVector(TipLocal * Visual.GetScale3D()), Visual.GetScale3D());
    if (!IsFiniteTransform(Placed)) return;
    Hand.Visual->SetWorldTransform(Placed, false, nullptr, ETeleportType::TeleportPhysics);
    Hand.LastWorld = Placed;
    Hand.Smoothed = Placed;
    Hand.bSmoothedValid = true;
}

void AGratiaStage1Runtime::UpdateForearm(FHandProxy& Hand, bool bLeft)
{
    FGratiaPalmFrame Palm;
    const bool bShown = bShowForearms && Hand.Visual.IsValid() && Hand.Visual->IsVisible() && Hand.HandAnim.IsValid()
        && Hand.HandAnim->GetPalmFrame(Palm) && Camera.IsValid() && Hand.Gate.State != EGratiaHandState::Unavailable;
    if (!Hand.Forearm.IsValid() && bShown)
    {
        UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
        UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        // The hand's own material, so the forearm reads as the same skin.
        const UMeshComponent* HandMesh = Cast<UMeshComponent>(Hand.Visual.Get());
        UMaterialInterface* Skin = HandMesh ? HandMesh->GetMaterial(0) : nullptr;
        auto Make = [this, Skin](UStaticMesh* Shape, const TCHAR* Name)
        {
            UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, Name);
            AddInstanceComponent(Part);
            Part->SetupAttachment(GetRootComponent());
            Part->SetStaticMesh(Shape);
            if (Skin) Part->SetMaterial(0, Skin);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetCastShadow(false);
            Part->SetAbsolute(true, true, true);
            Part->RegisterComponent();
            return Part;
        };
        Hand.Forearm = Make(Cylinder, bLeft ? TEXT("ForearmLeft") : TEXT("ForearmRight"));
        Hand.WristJoint = Make(Sphere, bLeft ? TEXT("WristLeft") : TEXT("WristRight"));
    }
    if (!Hand.Forearm.IsValid() || !Hand.WristJoint.IsValid()) return;
    Hand.Forearm->SetVisibility(bShown);
    Hand.WristJoint->SetVisibility(bShown);
    if (!bShown) return;
    const FTransform Visual = Hand.Visual->GetComponentTransform();
    const double Scale = FMath::Max(0.1, double(Visual.GetScale3D().GetAbsMax()));
    const FVector Back = -Visual.TransformVectorNoScale(Palm.Finger).GetSafeNormal();
    const FVector Wrist = Visual.TransformPosition(Palm.Point) + Back * GratiaPenetration::HandWristBackCm * Scale;
    // Elbow: two bones from a shoulder estimated off the head (down, out to the side, a little back).
    const FRotator Yaw(0.0, Camera->GetComponentRotation().Yaw, 0.0);
    const FVector Ahead = Yaw.Vector(), Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
    const FVector Shoulder = Camera->GetComponentLocation() - FVector::UpVector * 24.0 + Right * (bLeft ? -17.0 : 17.0) - Ahead * 6.0;
    const double Upper = 30.0, Lower = ForearmLengthCm;
    FVector ToWrist = Wrist - Shoulder;
    const double Reach = FMath::Clamp(ToWrist.Size(), FMath::Abs(Upper - Lower) + 1.0, Upper + Lower - 0.5);
    ToWrist = ToWrist.GetSafeNormal();
    const double Along = (Upper * Upper + Reach * Reach - Lower * Lower) / (2.0 * Reach);
    const FVector Pole = (-FVector::UpVector + Right * (bLeft ? -0.6 : 0.6) - Ahead * 0.2);
    FVector Bend = Pole - ToWrist * FVector::DotProduct(Pole, ToWrist);
    if (!Bend.Normalize()) Bend = -FVector::UpVector;
    const FVector Elbow = Shoulder + ToWrist * Along + Bend * FMath::Sqrt(FMath::Max(0.0, Upper * Upper - Along * Along));
    FVector Direction = (Elbow - Wrist).GetSafeNormal();
    // The wrist bends at most 60 degrees; in a channel the forearm continues the hand along the channel.
    const double Bent = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, Back), -1.0, 1.0)));
    if (Hand.bInChannel || Direction.IsNearlyZero()) Direction = Back;
    else if (Bent > 60.0) Direction = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Back, Direction), 60.0 / Bent).RotateVector(Back);
    const double Radius = ForearmRadiusCm * Scale, Length = Lower * Scale;
    Hand.Forearm->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Direction).ToQuat(), Wrist + Direction * (0.5 * Length),
        FVector(Radius / 50.0, Radius / 50.0, Length / 100.0)));
    Hand.WristJoint->SetWorldTransform(FTransform(FQuat::Identity, Wrist, FVector(Radius * 0.95 / 50.0)));
}

void AGratiaStage1Runtime::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    BindPlayer();
    bXRActive = UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
    UpdateDesktopCamera();
    UpdatePartnerView();

    // Apply recenter on the next tick, after the runtime has updated the head pose.
    if (bPendingRecenter)
    {
        FinishRecenter();
    }

    if (PlayerController.IsValid())
    {
        APlayerController* PC = PlayerController.Get();
        if (IsSceneInteractionAllowed() && Menu)
        {
            if (PC->WasInputKeyJustPressed(EKeys::F2)) Menu->Execute(EGratiaMenuAction::Pose);
            if (PC->WasInputKeyJustPressed(EKeys::F3)) Menu->Execute(EGratiaMenuAction::Reset);
        }
        // R on the keyboard, either stick click on the controllers (edge, so holding it recenters once).
        const bool bStickRecenter = HandInput && HandInput->IsRecenterPressed();
        if (PC->WasInputKeyJustPressed(EKeys::R) || (bStickRecenter && !bStickRecenterHeld)) Recenter();
        bStickRecenterHeld = bStickRecenter;
        if (PC->WasInputKeyJustPressed(EKeys::PageUp)) AdjustHeight(HeightStepCm);
        if (PC->WasInputKeyJustPressed(EKeys::PageDown)) AdjustHeight(-HeightStepCm);
        if (PC->WasInputKeyJustPressed(EKeys::Home)) ResetHeight();
        if (PC->WasInputKeyJustPressed(EKeys::F1)) bShowDebug = !bShowDebug;
        if (IsSceneInteractionAllowed() && PC->WasInputKeyJustPressed(EKeys::F6)) SetPrimitiveShown(!IsPrimitiveShown());
        if (IsSceneInteractionAllowed() && PC->WasInputKeyJustPressed(EKeys::F7)) CyclePrimitiveSize();
        if (PC->WasInputKeyJustPressed(EKeys::F8)) SetForcedTrackingLoss(true, !LeftHand.bForceLoss);
        if (PC->WasInputKeyJustPressed(EKeys::F9)) SetForcedTrackingLoss(false, !RightHand.bForceLoss);
    }

    // Character and scene target are serialized on the runtime actor or set explicitly.
    HandInput->UpdateInput();
    UpdateHand(LeftHand, true, DeltaSeconds);
    UpdateHand(RightHand, false, DeltaSeconds);
    LeftHandState = LeftHand.Gate.State;
    RightHandState = RightHand.Gate.State;
    // The primitive leaves with the character scene (lobby, loading).
    if (Primitive && SceneDirector && SceneDirector->IsSceneInputBlocked()) SetPrimitiveShown(false);
    // The held primitive or a hand is laid into the body right after the hands moved (same frame).
    if (TargetCharacter.IsValid() && TargetCharacter->Penetration)
    {
        TArray<AGratiaPenetrator*> Candidates;
        for (AGratiaPenetrator* Shaft : HandShafts) Candidates.Add(Shaft);
        for (AGratiaPenetrator* Shaft : QAShafts) Candidates.Add(Shaft);
        TargetCharacter->Penetration->SetCandidates(Candidates);
        TargetCharacter->Penetration->Solve(DeltaSeconds);
    }
    ApplyHandInChannel(LeftHand, true);
    ApplyHandInChannel(RightHand, false);
    UpdateForearm(LeftHand, true);
    UpdateForearm(RightHand, false);

    if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds > UE_SMALL_NUMBER)
    {
        const float FrameMs = DeltaSeconds * 1000.0f;
        const float Alpha = 1.0f - FMath::Exp(-DeltaSeconds * 3.0f);
        EstimatedFrameMs = EstimatedFrameMs > 0.0f ? FMath::Lerp(EstimatedFrameMs, FrameMs, Alpha) : FrameMs;
        EstimatedFPS = 1000.0f / EstimatedFrameMs;
        GameThreadMs = FMath::Lerp(GameThreadMs, static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime)), Alpha);
        RenderThreadMs = FMath::Lerp(RenderThreadMs, static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime)), Alpha);
        GPUFrameMs = FMath::Lerp(GPUFrameMs, static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())), Alpha);
        DebugRefreshSeconds += DeltaSeconds;
        LogFramePerformance(DeltaSeconds);
    }
    DebugPanel->SetVisibility(bShowDebug);
    if (DebugRefreshSeconds >= 0.2f)
    {
        DebugRefreshSeconds = 0.0f;
        DebugPanel->SetText(FText::FromString(GetStatusText()));
    }
    RunRequestedTests(DeltaSeconds);
    RunSoakAndMetrics(DeltaSeconds);
}

FString AGratiaStage1Runtime::GetPerformanceContext() const
{
    const AGratiaPreviewCharacter* Character = TargetCharacter.Get();
    const auto* Anim = Character && Character->CharacterMesh ? Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance()) : nullptr;
    const auto* SoftBody = Character ? Character->SoftBodyInteraction.Get() : nullptr;
    auto HandText = [&](const FHandProxy& Hand, bool bLeft)
    {
        return FString::Printf(TEXT("%s:%s%s%s"), bLeft ? TEXT("L") : TEXT("R"),
            !Hand.CupBone.IsNone() ? TEXT("cup ") : TEXT(""), !Hand.GripBone.IsNone() ? TEXT("grip ") : TEXT(""),
            SoftBody && !SoftBody->GetGrabbedBone(bLeft).IsNone() ? TEXT("grab") : TEXT("-"));
    };
    const auto* Physics = Character ? Character->SecondaryMotion.Get() : nullptr;
    return FString::Printf(TEXT("reaction=%d pose=%s quality=%d %s %s hand_physics=%.2fms/%d"),
        Anim && Anim->IsReactionCuePlaying() ? 1 : 0, Character ? *Character->GetPreviewPoseLabel() : TEXT("none"),
        Character && Character->Interaction ? Character->Interaction->Quality : -1, *HandText(LeftHand, true), *HandText(RightHand, false),
        Physics ? Physics->GetHandPressureMs() : 0.0f, Physics ? Physics->GetHandPressureQueries() : 0);
}

FName AGratiaStage1Runtime::GetScenePerfKey() const
{
    if (!SceneDirector || !SceneDirector->IsActive()) return FName(TEXT("Stage"));
    if (SceneDirector->IsInScene())
    {
        const FGratiaSceneEntry* Entry = SceneDirector->GetCurrentEntry();
        return Entry ? Entry->Id : FName(TEXT("Scene"));
    }
    return SceneDirector->GetState() == EGratiaFlowState::Lobby ? FName(TEXT("Lobby")) : NAME_None;
}

void AGratiaStage1Runtime::FlushScenePerformance()
{
    // A visit counts from 3 s after it starts (streaming and first draws) and needs ~2 s of frames.
    if (!ScenePerfKey.IsNone() && ScenePerfFrame.Num() >= 120)
    {
        double Seconds = 0.0;
        int32 Late = 0;
        for (const float Ms : ScenePerfFrame)
        {
            Seconds += Ms / 1000.0;
            Late += Ms > 1000.0f / 85.0f ? 1 : 0;
        }
        UE_LOG(LogGratiaStage1, Display, TEXT("SCENE_PERF scene=%s seconds=%.0f fps=%.1f frame_p95=%.1f gpu_med=%.2f gpu_p95=%.2f gpu_p99=%.2f late=%.1f%% quality=%d"),
            *ScenePerfKey.ToString(), Seconds, ScenePerfFrame.Num() / FMath::Max(Seconds, 0.001), GratiaPercentile(ScenePerfFrame, 0.95f),
            GratiaPercentile(ScenePerfGPU, 0.5f), GratiaPercentile(ScenePerfGPU, 0.95f), GratiaPercentile(ScenePerfGPU, 0.99f), 100.0f * Late / ScenePerfFrame.Num(),
            TargetCharacter.IsValid() && TargetCharacter->Interaction ? TargetCharacter->Interaction->Quality : -1);
    }
    ScenePerfGPU.Reset();
    ScenePerfFrame.Reset();
}

void AGratiaStage1Runtime::LogFramePerformance(float DeltaSeconds)
{
    // Thread times are the previous frame's; the worst frame of each window is logged with context.
    const float FrameMs = DeltaSeconds * 1000.0f;
    const float SlowMs = 1000.0f / 72.0f;
    const float GPUMs = static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
    const double Now = FPlatformTime::Seconds();
    ++PerfFrames;
    PerfWindowSeconds += DeltaSeconds;
    PerfWindowGPU.Add(GPUMs);
    if (FrameMs > SlowMs) ++PerfSlowFrames;
    if (FrameMs > PerfWorstMs)
    {
        PerfWorstMs = FrameMs;
        PerfWorstGameMs = static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime));
        PerfWorstRenderMs = static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime));
        PerfWorstGPUMs = GPUMs;
    }
    const FName SceneKey = GetScenePerfKey();
    if (SceneKey != ScenePerfKey)
    {
        FlushScenePerformance();
        ScenePerfKey = SceneKey;
        ScenePerfSince = Now;
    }
    else if (!SceneKey.IsNone() && Now - ScenePerfSince > 3.0)
    {
        ScenePerfGPU.Add(GPUMs);
        ScenePerfFrame.Add(FrameMs);
    }
    if (FrameMs > 40.0f)
    {
        if (Now - PerfSpikeSecond >= 1.0) { PerfSpikeSecond = Now; PerfSpikesThisSecond = 0; }
        if (++PerfSpikesThisSecond <= 3)
            UE_LOG(LogGratiaStage1, Warning, TEXT("PERF_SPIKE frame=%.1fms game=%.1f render=%.1f gpu=%.1f %s"), FrameMs,
                FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
                FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()), *GetPerformanceContext());
    }
    if (PerfWindowSeconds < 2.0f) return;
    // Windows with slow frames are always logged; quiet ones every 10 s.
    if (PerfSlowFrames > 0 || ++PerfQuietWindows >= 5)
    {
        PerfQuietWindows = 0;
        UE_LOG(LogGratiaStage1, Display, TEXT("PERF fps=%.1f gpu_med=%.1f gpu_p95=%.1f worst=%.1fms (game=%.1f render=%.1f gpu=%.1f) slow=%d/%d %s"),
            PerfFrames / PerfWindowSeconds, GratiaPercentile(PerfWindowGPU, 0.5f), GratiaPercentile(PerfWindowGPU, 0.95f), PerfWorstMs, PerfWorstGameMs,
            PerfWorstRenderMs, PerfWorstGPUMs, PerfSlowFrames, PerfFrames, *GetPerformanceContext());
    }
    PerfWindowSeconds = PerfWorstMs = PerfWorstGameMs = PerfWorstRenderMs = PerfWorstGPUMs = 0.0f;
    PerfFrames = PerfSlowFrames = 0;
    PerfWindowGPU.Reset();
}

void AGratiaStage1Runtime::BindPlayer()
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (PC != PlayerController.Get())
    {
        PlayerController = PC;
        if (PC && PC->IsLocalController())
        {
            PC->ClientSetHUD(AGratiaStage1HUD::StaticClass());
        }
    }

    if (PC) if (auto* HUD = Cast<AGratiaStage1HUD>(PC->GetHUD())) HUD->SetRuntime(this);
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (Pawn == PlayerPawn.Get())
    {
        return;
    }
    RestoreHand(LeftHand);
    RestoreHand(RightHand);
    if (Camera.IsValid() && bDesktopCameraApplied)
    {
        Camera->SetRelativeTransform(OriginalCameraRelative);
    }
    LeftHand = FHandProxy();
    RightHand = FHandProxy();
    PlayerPawn = Pawn;
    Camera.Reset();
    TrackingOrigin.Reset();
    bPawnReady = false;
    bDesktopCameraApplied = false;
    if (!Pawn)
    {
        return;
    }

    Camera = Pawn->FindComponentByClass<UCameraComponent>();
    if (Camera.IsValid())
    {
        OriginalCameraRelative = Camera->GetRelativeTransform();
        AddTickPrerequisiteComponent(Camera.Get());
    }
    BindHand(LeftHand, TEXT("MotionControllerLeftGrip"), TEXT("HandLeft"));
    BindHand(RightHand, TEXT("MotionControllerRightGrip"), TEXT("HandRight"));

    // Prefer the common tracking-space parent, including differently named XR template origins.
    USceneComponent* Candidate = LeftHand.Controller.IsValid() ? LeftHand.Controller->GetAttachParent() : nullptr;
    for (; Candidate; Candidate = Candidate->GetAttachParent())
    {
        const bool bContainsCamera = Camera.IsValid() && (Camera.Get() == Candidate || Camera->IsAttachedTo(Candidate));
        const bool bContainsRight = RightHand.Controller.IsValid() && RightHand.Controller->IsAttachedTo(Candidate);
        if (bContainsCamera && bContainsRight)
        {
            TrackingOrigin = Candidate;
            break;
        }
    }
    if (TrackingOrigin.IsValid())
    {
        OriginalOriginZ = TrackingOrigin->GetRelativeLocation().Z;
        ApplyHeight();
    }
    bPawnReady = Camera.IsValid() && TrackingOrigin.IsValid() && LeftHand.Controller.IsValid()
        && RightHand.Controller.IsValid() && LeftHand.Visual.IsValid() && RightHand.Visual.IsValid();
    UE_LOG(LogGratiaStage1, Display, TEXT("Pawn=%s, camera=%s, origin=%s, left=%s, right=%s, ready=%s"),
        *Pawn->GetName(), *GetNameSafe(Camera.Get()), *GetNameSafe(TrackingOrigin.Get()),
        *GetNameSafe(LeftHand.Visual.Get()), *GetNameSafe(RightHand.Visual.Get()), bPawnReady ? TEXT("yes") : TEXT("no"));
}

void AGratiaStage1Runtime::BindHand(FHandProxy& Hand, FName ControllerName, FName VisualName)
{
    TInlineComponentArray<USceneComponent*> Components;
    PlayerPawn->GetComponents(Components);
    for (USceneComponent* Component : Components)
    {
        if (Component->GetFName() == ControllerName)
        {
            Hand.Controller = Cast<UMotionControllerComponent>(Component);
        }
        if (Component->GetFName() == VisualName)
        {
            Hand.Visual = Component;
        }
    }
    if (!Hand.Controller.IsValid() || !Hand.Visual.IsValid() || !Hand.Visual->IsAttachedTo(Hand.Controller.Get()))
    {
        UE_LOG(LogGratiaStage1, Warning, TEXT("Expected hand hierarchy not found: %s -> %s"), *ControllerName.ToString(), *VisualName.ToString());
        Hand.Visual.Reset();
        return;
    }

    AddTickPrerequisiteComponent(Hand.Controller.Get());
    // Replace the template hand graph with the native per-finger pose so fingers can
    // stop on and wrap around soft parts; keep the template graph if poses are missing.
    if (auto* HandMesh = Cast<USkeletalMeshComponent>(Hand.Visual.Get()))
    {
        UClass* PreviousClass = HandMesh->GetAnimClass();
        HandMesh->SetAnimInstanceClass(UGratiaHandAnimInstance::StaticClass());
        auto* HandAnim = Cast<UGratiaHandAnimInstance>(HandMesh->GetAnimInstance());
        if (HandAnim) HandAnim->bLeftHand = &Hand == &LeftHand;
        if (HandAnim && HandAnim->LoadDefaultPoses()) Hand.HandAnim = HandAnim;
        else
        {
            UE_LOG(LogGratiaStage1, Warning, TEXT("Hand poses unavailable for %s; template hand animation kept, no finger conform"), *VisualName.ToString());
            HandMesh->SetAnimInstanceClass(PreviousClass);
        }
    }
    Hand.OriginalParent = Hand.Visual->GetAttachParent();
    Hand.OriginalSocket = Hand.Visual->GetAttachSocketName();
    Hand.OriginalRelative = Hand.Visual->GetRelativeTransform();
    Hand.LastWorld = Hand.Visual->GetComponentTransform();
    if (!IsFiniteTransform(Hand.LastWorld)) Hand.LastWorld = FTransform::Identity;
    // XR mannequin meshes have no PhysicsAsset and default to NoCollision.
    // Overlap proxy is independent of swept, force-limited Chaos secondary-body interaction.
    USphereComponent* Contact = NewObject<USphereComponent>(PlayerPawn.Get(), NAME_None, RF_Transient);
    PlayerPawn->AddInstanceComponent(Contact);
    Contact->SetupAttachment(Hand.Visual.Get());
    Contact->SetSphereRadius(6.0f);
    Contact->SetCollisionObjectType(ECC_WorldDynamic);
    Contact->SetCollisionResponseToAllChannels(ECR_Overlap);
    Contact->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Contact->SetGenerateOverlapEvents(false);
    Contact->SetHiddenInGame(true);
    Contact->RegisterComponent();
    Hand.ContactCollider = Contact;
    TArray<USceneComponent*> Descendants;
    Hand.Visual->GetChildrenComponents(true, Descendants);
    Descendants.Insert(Hand.Visual.Get(), 0);
    for (USceneComponent* Component : Descendants)
    {
        if (UPrimitiveComponent* Shape = Cast<UPrimitiveComponent>(Component))
        {
            FCollisionSnapshot Snapshot;
            Snapshot.Component = Shape;
            Snapshot.Collision = Shape->GetCollisionEnabled();
            Snapshot.bGenerateOverlapEvents = Shape->GetGenerateOverlapEvents();
            if (Shape == Contact)
            {
                Snapshot.Collision = ECollisionEnabled::QueryOnly;
                Snapshot.bGenerateOverlapEvents = true;
            }
            Hand.Collisions.Add(Snapshot);
        }
    }
    Hand.bCollisionsEnabled = true;
    SetHandCollision(Hand, false);
}

void AGratiaStage1Runtime::SetHandCollision(FHandProxy& Hand, bool bEnabled)
{
    if (Hand.bCollisionsEnabled == bEnabled) return;
    for (const FCollisionSnapshot& Snapshot : Hand.Collisions)
    {
        if (Snapshot.Component.IsValid())
        {
            // Disable overlap events before changing collision, preventing stale interaction on loss.
            Snapshot.Component->SetGenerateOverlapEvents(bEnabled && Snapshot.bGenerateOverlapEvents);
            Snapshot.Component->SetCollisionEnabled(bEnabled ? Snapshot.Collision : ECollisionEnabled::NoCollision);
        }
    }
    Hand.bCollisionsEnabled = bEnabled;
}

void AGratiaStage1Runtime::RestoreHand(FHandProxy& Hand)
{
    if (Hand.ContactCollider.IsValid())
    {
        Hand.ContactCollider->DestroyComponent();
        Hand.ContactCollider.Reset();
    }
    if (Hand.Visual.IsValid() && Hand.OriginalParent.IsValid() && IsFiniteTransform(Hand.OriginalRelative))
    {
        Hand.Visual->AttachToComponent(Hand.OriginalParent.Get(), FAttachmentTransformRules::KeepWorldTransform, Hand.OriginalSocket);
        Hand.Visual->SetRelativeTransform(Hand.OriginalRelative, false, nullptr, ETeleportType::TeleportPhysics);
    }
    SetHandCollision(Hand, true);
}

FTransform AGratiaStage1Runtime::GetParkedTransform(bool bLeft, const FVector& Scale) const
{
    const FTransform View = Camera.IsValid() ? Camera->GetComponentTransform() : GetActorTransform();
    const FRotator YawOnly(0.0, View.Rotator().Yaw, 0.0);
    const FVector ViewLocation = View.GetLocation();
    const FVector ParkLocation = ViewLocation + YawOnly.RotateVector(FVector(25.0, bLeft ? -28.0 : 28.0, -65.0));
    return FTransform(YawOnly, ParkLocation, Scale);
}

void AGratiaStage1Runtime::UpdateHand(FHandProxy& Hand, bool bLeft, float DeltaSeconds)
{
    if (!Hand.Visual.IsValid() || !Hand.Controller.IsValid() || !Hand.OriginalParent.IsValid())
    {
        UpdateHaptics(Hand, bLeft, 0.0f, 0.0f);
        return;
    }
    const FTransform Target = Hand.OriginalRelative * Hand.OriginalParent->GetSocketTransform(Hand.OriginalSocket, RTS_World);
    const bool bReliable = bXRActive && !Hand.bForceLoss && Hand.Controller->IsTracked()
        && Hand.Controller->CurrentTrackingStatus == ETrackingStatus::Tracked && IsFiniteTransform(Target);
    const EGratiaHandState Before = Hand.Gate.State;
    Hand.Gate.Update(bReliable, DeltaSeconds, TrackingStableSeconds, RecoveryBlendSeconds);
    if (!Hand.Gate.CanInteract())
    {
        SetHandCollision(Hand, false);
        // A parked proxy must not inherit stale poses, invalid parent transforms or late XR updates.
        if (Hand.Visual->GetAttachParent())
        {
            Hand.Visual->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        }
    }

    FTransform VisualWorld = Hand.LastWorld;
    if ((Hand.Gate.State != EGratiaHandState::Tracked || !IsSceneInteractionAllowed()) && !Hand.GripBone.IsNone()) ReleaseBodyGrip(Hand, bLeft, TEXT("tracking / scene gate"));
    if (Hand.Gate.State != EGratiaHandState::Tracked || !IsSceneInteractionAllowed())
    {
        ReleasePrimitive(Hand, bLeft, TEXT("tracking / scene gate"));
        // An untracked hand leaves the channel (its shaft is no longer held).
        if (HandShafts.IsValidIndex(bLeft ? 0 : 1) && HandShafts[bLeft ? 0 : 1]) HandShafts[bLeft ? 0 : 1]->SetHeld(INDEX_NONE);
        Hand.PenetrationShape = 0;
    }
    if (Hand.Gate.State == EGratiaHandState::Tracked)
    {
        // The palm collides with the body surface (not a wide sphere around the wrist).
        FGratiaPalmFrame Palm;
        const FVector PalmLocal = Hand.HandAnim.IsValid() && Hand.HandAnim->GetPalmFrame(Palm) ? Palm.Point * Target.GetScale3D() : FVector::ZeroVector;
        const FTransform Constrained = IsSceneInteractionAllowed() && TargetCharacter.IsValid() && TargetCharacter->Interaction
            ? TargetCharacter->Interaction->ConstrainHand(Hand.LastWorld, Target, bLeft, PalmLocal) : Target;
        if (IsSceneInteractionAllowed()) UpdatePrimitiveGrab(Hand, bLeft, Target);
        UpdateHandShaft(Hand, bLeft, Target);
        // A hand at or in a channel entrance is entering, not cupping or wrapping the body around it.
        const UGratiaPenetration* Penetration = TargetCharacter.IsValid() ? TargetCharacter->Penetration.Get() : nullptr;
        const AGratiaPenetrator* Shaft = HandShafts.IsValidIndex(bLeft ? 0 : 1) ? HandShafts[bLeft ? 0 : 1].Get() : nullptr;
        const bool bAtChannel = Hand.bInChannel || (Hand.PenetrationShape != 0 && Shaft && Penetration
            && Penetration->GetEntranceGap(Shaft->GetBase().GetLocation() + Shaft->GetBase().GetRotation().GetForwardVector() * Shaft->GetShaft().Length) < 8.0);
        // A hand holding the primitive neither wraps, cups nor leans onto the body.
        if (Hand.bHoldsPrimitive || bAtChannel)
        {
            ReleaseBodyGrip(Hand, bLeft, Hand.bHoldsPrimitive ? TEXT("holding the primitive") : TEXT("at a channel"));
            Hand.CupBone = NAME_None; Hand.CupBlend = 0.0f; Hand.SurfaceWeight = 0.0f; Hand.LeanWeight = 0.0f;
        }
        const FTransform ContactTarget = Hand.bHoldsPrimitive || bAtChannel ? Constrained : ApplyBodySurface(Hand, bLeft, Target, Constrained, DeltaSeconds);
        VisualWorld = ContactTarget;
        SetHandCollision(Hand, IsSceneInteractionAllowed());
    }
    else if (Hand.Gate.State == EGratiaHandState::Recovering)
    {
        if (Before != EGratiaHandState::Recovering) Hand.RecoveryStart = Hand.LastWorld;
        VisualWorld = BlendTransform(Hand.RecoveryStart, Target, Hand.Gate.RecoveryAlpha(RecoveryBlendSeconds));
        Hand.Visual->SetWorldTransform(VisualWorld, false, nullptr, ETeleportType::TeleportPhysics);
        Hand.Smoothed = VisualWorld; Hand.bSmoothedValid = true; Hand.OffsetSmoother.Reset();
    }
    else
    {
        const FTransform Parked = GetParkedTransform(bLeft, Hand.LastWorld.GetScale3D());
        if (IsFiniteTransform(Parked))
        {
            const float SafeDelta = FMath::IsFinite(DeltaSeconds) ? FMath::Clamp(DeltaSeconds, 0.0f, 0.1f) : 0.0f;
            const float Alpha = 1.0f - FMath::Exp(-SafeDelta * FMath::Max(0.1f, ParkingInterpSpeed));
            VisualWorld = BlendTransform(Hand.LastWorld, Parked, Alpha);
            Hand.Visual->SetWorldTransform(VisualWorld, false, nullptr, ETeleportType::TeleportPhysics);
            Hand.Smoothed = VisualWorld; Hand.bSmoothedValid = true; Hand.OffsetSmoother.Reset();
        }
    }
    FTransform Desired = VisualWorld;
    float ContactHapticAmplitude = 0.0f, ContactHapticFrequency = 0.0f;
    // The opt-in soak supplies its own samples after this update. Parked desktop
    // hands must not keep resetting that scenario's correction-recovery timer.
    if (TargetCharacter.IsValid() && TargetCharacter->Interaction && !Verification->OwnsSyntheticContactSamples())
    {
        // Inside a channel the channel answers; the touch zones around it stay quiet.
        TargetCharacter->Interaction->SetHandSample(bLeft, Target, VisualWorld, Hand.Gate.CanInteract() && IsSceneInteractionAllowed() && !Hand.bInChannel);
        if (TargetCharacter->SecondaryMotion && !Verification->IsHandPhysicsQAActive())
            TargetCharacter->SecondaryMotion->SubmitHand(bLeft, VisualWorld.GetLocation(),
                TargetCharacter->Interaction->IsHandSampleReady(bLeft) && IsSceneInteractionAllowed(), DeltaSeconds,
                HandInput ? HandInput->GetTrigger(bLeft) : 0.0f);
        if (TargetCharacter->SoftBodyInteraction)
        {
            auto* SoftBody = TargetCharacter->SoftBodyInteraction.Get();
            TArray<FVector> Fingers;
            // Press, grab and the surface dent work from the palm centre, not the wrist (hand root).
            FVector PalmLocal = FVector::ZeroVector, Palm;
            if (Hand.HandAnim.IsValid())
            {
                Hand.HandAnim->GetFingerPoints(Fingers);
                if (Hand.HandAnim->GetPalmPoint(Palm)) PalmLocal = VisualWorld.InverseTransformPositionNoScale(Palm);
            }
            const FVector VisiblePalm = VisualWorld.TransformPositionNoScale(PalmLocal);
            const FVector RawPalm = Target.TransformPositionNoScale(PalmLocal);
            const bool bAllowed = TargetCharacter->Interaction->IsHandSampleReady(bLeft) && IsSceneInteractionAllowed() && !Hand.bInChannel;
            const float Grab = HandInput && !Hand.bHoldsPrimitive ? FMath::Max(HandInput->GetTrigger(bLeft), HandInput->GetGrip(bLeft)) : 0.0f;
            // Cupping/wrapping places the hand itself; the soft part then follows the controller.
            const bool bPoseOwned = !Hand.CupBone.IsNone() || !Hand.GripBone.IsNone();
            SoftBody->SubmitHand(bLeft, VisiblePalm, RawPalm, bAllowed, DeltaSeconds, Grab, Fingers, bPoseOwned,
                Hand.CupBone, Hand.CupBone.IsNone() ? 0.0f : Hand.CupSqueeze);
            // Inside a soft zone the visible hand sinks by the bounded press depth.
            if (SoftBody->HasPress(bLeft) && Hand.Gate.State == EGratiaHandState::Tracked)
                Desired.SetLocation(SoftBody->GetPressPoint(bLeft) - (VisiblePalm - VisualWorld.GetLocation()));
            // Grip onto a body part gives a short tap; soft-zone vibration follows depth.
            Hand.GripPulse = FMath::Max(0.0f, Hand.GripPulse - DeltaSeconds);
            ContactHapticAmplitude = bAllowed ? FMath::Max(SoftBody->GetHapticAmplitude(bLeft), Hand.GripPulse > 0 ? 0.45f : 0.0f) : 0.0f;
            ContactHapticFrequency = Hand.GripPulse > 0 ? 0.3f : SoftBody->GetHapticFrequency(bLeft);
        }
    }
    // The held primitive vibrates with its own insertion (computed by the previous solve).
    // So does a hand inside a channel.
    const AGratiaPenetrator* OwnShaft = HandShafts.IsValidIndex(bLeft ? 0 : 1) ? HandShafts[bLeft ? 0 : 1].Get() : nullptr;
    if (Hand.bInChannel && OwnShaft && OwnShaft->HapticAmplitude > ContactHapticAmplitude)
    {
        ContactHapticAmplitude = OwnShaft->HapticAmplitude;
        ContactHapticFrequency = OwnShaft->HapticFrequency;
    }
    if (Hand.bHoldsPrimitive && Primitive && Primitive->HapticAmplitude > ContactHapticAmplitude)
    {
        ContactHapticAmplitude = Primitive->HapticAmplitude;
        ContactHapticFrequency = Primitive->HapticFrequency;
    }
    UpdateHaptics(Hand, bLeft, ContactHapticAmplitude, ContactHapticFrequency);
    if (Hand.Gate.State == EGratiaHandState::Tracked) ApplyVisualHand(Hand, Target, Desired, DeltaSeconds);
    if (Hand.bSmoothedValid && IsFiniteTransform(Hand.Smoothed)) VisualWorld = Hand.Smoothed;
    if (IsFiniteTransform(VisualWorld)) Hand.LastWorld = VisualWorld;
    UpdateHandPose(Hand, bLeft, VisualWorld.GetLocation());
    if (Before != Hand.Gate.State)
    {
        UE_LOG(LogGratiaStage1, Display, TEXT("%s hand: %s (forced loss=%s)"), bLeft ? TEXT("Left") : TEXT("Right"),
            HandStateLabel(Hand.Gate.State), Hand.bForceLoss ? TEXT("yes") : TEXT("no"));
    }
}

void AGratiaStage1Runtime::UpdateHandPose(FHandProxy& Hand, bool bLeft, const FVector& Near)
{
    if (!Hand.HandAnim.IsValid()) return;
    auto* Anim = Hand.HandAnim.Get();
    const float Grasp = HandInput ? HandInput->GetGrasp(bLeft) : 0.0f;
    const float Index = HandInput ? HandInput->GetIndexCurl(bLeft) : 0.0f;
    // Wrapping grip and cupping close every finger onto the part; near a surface the fingers rest on it.
    const float Rest = !Hand.GripBone.IsNone() || !Hand.CupBone.IsNone() || Hand.bHoldsPrimitive ? 1.0f : 0.85f * Hand.SurfaceWeight;
    // Squeeze depth into soft parts follows the trigger/grip while cupping.
    const float Squeeze = Hand.CupBone.IsNone() ? Grasp : Hand.CupSqueeze;
    // Thumb: 1 is its relaxed pose; contact caps extend it out of the body.
    Anim->FingerInput[0] = Anim->ThumbOpenPose ? 1.0f : FMath::Max(Grasp, Rest);
    Anim->FingerInput[1] = FMath::Max(Index, Rest);
    Anim->FingerInput[2] = Anim->FingerInput[3] = Anim->FingerInput[4] = FMath::Max(Grasp, Rest);
    // Penetrating shapes: three fingers (index, middle, ring) straight with the thumb and little finger folded;
    // the flat hand keeps the four fingers straight (the thumb rests on the stick); the fist closes them (grip and
    // trigger already do).
    using GratiaPenetration::EHandShape;
    if (Hand.PenetrationShape == uint8(EHandShape::Fingers))
    {
        Anim->FingerInput[1] = Anim->FingerInput[2] = Anim->FingerInput[3] = 0.0f;
        Anim->FingerInput[4] = 1.0f;
        if (!Anim->ThumbOpenPose) Anim->FingerInput[0] = 1.0f;
    }
    else if (Hand.PenetrationShape == uint8(EHandShape::Hand))
        Anim->FingerInput[1] = Anim->FingerInput[2] = Anim->FingerInput[3] = Anim->FingerInput[4] = 0.0f;
    const auto* Profile = TargetCharacter.IsValid() ? TargetCharacter->CharacterProfile.Get() : nullptr;
    Anim->bConform = Profile && Profile->SoftBody.bFingerConform && Hand.Gate.CanInteract() && !Hand.bInChannel;
    if (Profile) { Anim->FingerRadiusCm = Profile->SoftBody.FingerRadiusCm; Anim->ConformMarginCm = Profile->SoftBody.FingerConformMarginCm; }
    // Squeezing (grip) lets the fingers sink into soft zones; the press dent opens under them.
    Anim->ConformCapsules.Reset();
    if (Anim->bConform && TargetCharacter->BodySurface && TargetCharacter->BodySurface->HasSurface())
        TargetCharacter->BodySurface->GatherConformShapes(Near, 30.0f, Anim->ConformSpheres, Anim->ConformCapsules, Profile->SoftBody.SquishDepthCm * Squeeze);
    else if (Anim->bConform && TargetCharacter->SoftBodyInteraction)
        TargetCharacter->SoftBodyInteraction->GetConformSpheres(Near, 30.0f, Anim->ConformSpheres, Profile->SoftBody.SquishDepthCm * Squeeze);
    else Anim->ConformSpheres.Reset();
}

void AGratiaStage1Runtime::ApplyVisualHand(FHandProxy& Hand, const FTransform& Target, const FTransform& Desired, float DeltaSeconds)
{
    if (!IsFiniteTransform(Desired)) return;
    auto Near = [](const FTransform& A, const FTransform& B, double Cm, double Degrees)
    {
        return FVector::Distance(A.GetLocation(), B.GetLocation()) <= Cm
            && FMath::RadiansToDegrees(A.GetRotation().AngularDistance(B.GetRotation())) <= Degrees;
    };
    // Contact, lean, grip, cup and press switch on and off: ease their jumps (~35 ms) while the
    // hand follows the controller's own motion exactly (FGratiaHandOffsetSmoother).
    const FTransform Visible = Hand.OffsetSmoother.Update(Target, Desired, Hand.bSmoothedValid ? Hand.Smoothed : Hand.LastWorld, DeltaSeconds);
    // A free hand without contact or residual rides the controller (exact late update).
    if (Hand.OffsetSmoother.IsSettled() && Near(Desired, Target, 0.05, 0.2))
    {
        Hand.OffsetSmoother.Settle();
        Hand.Smoothed = Target;
        Hand.bSmoothedValid = true;
        if (Hand.Visual->GetAttachParent() != Hand.OriginalParent.Get())
            Hand.Visual->AttachToComponent(Hand.OriginalParent.Get(), FAttachmentTransformRules::KeepWorldTransform, Hand.OriginalSocket);
        Hand.Visual->SetRelativeTransform(Hand.OriginalRelative, false, nullptr, ETeleportType::TeleportPhysics);
        return;
    }
    Hand.Smoothed = IsFiniteTransform(Visible) ? Visible : Desired;
    Hand.bSmoothedValid = true;
    if (Hand.Visual->GetAttachParent()) Hand.Visual->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    Hand.Visual->SetWorldTransform(Hand.Smoothed, false, nullptr, ETeleportType::TeleportPhysics);
}

void AGratiaStage1Runtime::ReleaseBodyGrip(FHandProxy& Hand, bool bLeft, const TCHAR* Reason)
{
    if (Hand.GripBone.IsNone()) return;
    UE_LOG(LogGratiaStage1, Display, TEXT("BODY_GRIP_RELEASE hand=%s part=%s reason=%s"), bLeft ? TEXT("L") : TEXT("R"), *Hand.GripBone.ToString(), Reason);
    Hand.GripBone = NAME_None;
    Hand.GripBlend = 1.0f;
}

FTransform AGratiaStage1Runtime::ApplyBodySurface(FHandProxy& Hand, bool bLeft, const FTransform& Target, const FTransform& Constrained, float DeltaSeconds)
{
    Hand.SurfaceWeight = 0.0f;
    const FName WasCupping = Hand.CupBone;
    Hand.CupBone = NAME_None;
    AGratiaPreviewCharacter* Character = TargetCharacter.Get();
    const UGratiaCharacterProfile* Profile = Character ? Character->CharacterProfile.Get() : nullptr;
    UGratiaBodySurface* Surface = Character ? Character->BodySurface.Get() : nullptr;
    FGratiaPalmFrame Palm;
    if (!Profile || !Profile->HandSurface.bEnabled || !Surface || !Hand.HandAnim.IsValid() || !Hand.HandAnim->GetPalmFrame(Palm)
        || !IsSceneInteractionAllowed() || !Character->CharacterMesh)
    {
        ReleaseBodyGrip(Hand, bLeft, TEXT("unavailable"));
        return Constrained;
    }
    const FGratiaHandSurfaceSettings& Settings = Profile->HandSurface;
    const float Grip = HandInput ? HandInput->GetGrip(bLeft) : 0.0f;
    const USkeletalMeshComponent* Mesh = Character->CharacterMesh.Get();
    auto BoneFrame = [Mesh](FName Bone, FTransform& Out)
    {
        const int32 Index = Mesh->GetBoneIndex(Bone);
        if (Index == INDEX_NONE) return false;
        const FTransform World = Mesh->GetBoneTransform(Index);
        Out = FTransform(World.GetRotation(), World.GetLocation());
        return !Out.ContainsNaN();
    };
    if (!Hand.GripBone.IsNone())
    {
        // Held: the hand rides on the body part; it lets go on release or when pulled away.
        FTransform Bone;
        if (!BoneFrame(Hand.GripBone, Bone)) { ReleaseBodyGrip(Hand, bLeft, TEXT("missing bone")); return Constrained; }
        const FTransform Held = Hand.GripRelative * Bone;
        const double Pull = FVector::Distance(Target.TransformPosition(Palm.Point), Held.TransformPosition(Palm.Point));
        if (Grip <= Settings.GripReleaseInput) ReleaseBodyGrip(Hand, bLeft, TEXT("grip released"));
        else if (Pull > Settings.GripBreakDistanceCm) ReleaseBodyGrip(Hand, bLeft, TEXT("pulled away"));
        else
        {
            Hand.GripBlend = FMath::Min(1.0f, Hand.GripBlend + DeltaSeconds / FMath::Max(0.01f, Settings.GripBlendSeconds));
            return BlendTransform(Hand.GripFrom, Held, FMath::SmoothStep(0.0f, 1.0f, Hand.GripBlend));
        }
        Hand.bGripArmed = false;
        return Constrained;
    }
    if (Grip <= Settings.GripReleaseInput) Hand.bGripArmed = true;
    FGratiaSurfaceHit Hit;
    const FVector PalmPoint = Constrained.TransformPosition(Palm.Point);
    // Trigger or grip on a breast/butt cups it: palm on the curve, fingers around it, and the
    // fingers sink deeper the harder the trigger is pressed. The hand follows the controller and
    // the soft part follows the hand (soft-body grab), so it is not pinned to the bone.
    const float Squeeze = FMath::Max(Grip, HandInput ? HandInput->GetTrigger(bLeft) : 0.0f);
    if (Squeeze >= Settings.CupStartInput
        && Surface->FindNearest(PalmPoint, Settings.GripReachCm + Settings.PalmThicknessCm, Hit, true) && Hit.bSoftZone)
    {
        TArray<FVector4> NearSpheres;
        TArray<FGratiaConformCapsule> NearCapsules;
        Surface->GatherConformShapes(PalmPoint, 25.0f, NearSpheres, NearCapsules);
        // The palm sinks into the soft surface by half of the finger squeeze depth.
        const FGratiaSurfaceHit CupHit = UGratiaBodySurface::Squeezed(Hit, 0.5f * Profile->SoftBody.SquishDepthCm * Squeeze);
        const FTransform Cup = UGratiaBodySurface::SolveWrap(Constrained, Palm, CupHit, Settings.PalmThicknessCm,
            Profile->SoftBody.FingerRadiusCm + Profile->SoftBody.FingerConformMarginCm, NearCapsules);
        if (WasCupping != Hit.Bone) { Hand.CupBlend = 0.0f; Hand.GripPulse = 0.05f; }
        Hand.CupBone = Hit.Bone;
        Hand.CupSqueeze = Squeeze;
        Hand.CupBlend = FMath::Min(1.0f, Hand.CupBlend + DeltaSeconds / FMath::Max(0.01f, Settings.GripBlendSeconds));
        Hand.SurfaceWeight = 1.0f;
        return BlendTransform(Constrained, Cup, FMath::SmoothStep(0.0f, 1.0f, Hand.CupBlend));
    }
    Hand.CupBlend = 0.0f;
    // Soft zones keep their own trigger/grip grab; limbs and torso are wrapped here.
    if (Hand.bGripArmed && Grip >= Settings.GripStartInput
        && Surface->FindNearest(PalmPoint, Settings.GripReachCm + Settings.PalmThicknessCm, Hit, false, true))
    {
        FTransform Bone;
        if (BoneFrame(Hit.Bone, Bone))
        {
            TArray<FVector4> NearSpheres;
            TArray<FGratiaConformCapsule> NearCapsules;
            Surface->GatherConformShapes(PalmPoint, 25.0f, NearSpheres, NearCapsules);
            const FTransform Wrap = UGratiaBodySurface::SolveWrap(Constrained, Palm, Hit, Settings.PalmThicknessCm,
                Profile->SoftBody.FingerRadiusCm + Profile->SoftBody.FingerConformMarginCm, NearCapsules);
            Hand.GripBone = Hit.Bone;
            Hand.GripRelative = Wrap.GetRelativeTransform(Bone);
            Hand.GripFrom = Hand.LastWorld;
            Hand.GripBlend = 0.0f;
            Hand.bGripArmed = false;
            Hand.GripPulse = 0.07f;
            UE_LOG(LogGratiaStage1, Display, TEXT("BODY_GRIP hand=%s part=%s gap=%.1fcm radius=%.1fcm"),
                bLeft ? TEXT("L") : TEXT("R"), *Hit.Bone.ToString(), Hit.Gap, Hit.Radius);
            return Hand.GripFrom;
        }
    }
    float LeanTarget = 0.0f;
    const bool bNear = Settings.AdaptDistanceCm > 0 && Surface->FindNearest(PalmPoint, Settings.AdaptDistanceCm + Settings.PalmThicknessCm, Hit, true);
    if (bNear) LeanTarget = 1.0f - FMath::Clamp((Hit.Gap - Settings.PalmThicknessCm) / Settings.AdaptDistanceCm, 0.0f, 1.0f);
    Hand.LeanWeight = FMath::FInterpTo(Hand.LeanWeight, LeanTarget, FMath::Clamp(DeltaSeconds, 0.0f, 0.1f), 12.0f);
    Hand.SurfaceWeight = Hand.LeanWeight;
    if (bNear && Hand.LeanWeight > 0.01f)
        return UGratiaBodySurface::LeanToSurface(Constrained, Palm, Hit, Hand.LeanWeight, Settings.AdaptMaxDegrees, Settings.PalmThicknessCm);
    return Constrained;
}

void AGratiaStage1Runtime::UpdateHaptics(FHandProxy& Hand, bool bLeft, float Amplitude, float Frequency)
{
    APlayerController* PC = PlayerController.Get();
    if (!PC) return;
    if (!bXRActive || !Hand.Gate.CanInteract() || !IsSceneInteractionAllowed()) Amplitude = 0.0f;
    const auto* Settings = SceneDirector ? SceneDirector->GetUserSettings() : nullptr;
    Amplitude *= Settings ? Settings->HapticsScale : 1.0f;
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    Amplitude = FMath::IsFinite(Amplitude) ? FMath::Clamp(Amplitude, 0.0f, 1.0f) : 0.0f;
    Frequency = FMath::IsFinite(Frequency) ? FMath::Clamp(Frequency, 0.0f, 1.0f) : 0.0f;
    // OpenXR haptics expire on their own; refresh while active, change only on a real difference.
    const bool bChanged = FMath::Abs(Amplitude - Hand.SentHapticAmplitude) > 0.02f || FMath::Abs(Frequency - Hand.SentHapticFrequency) > 0.05f;
    const bool bMustStop = Amplitude <= 0.0f && Hand.SentHapticAmplitude > 0.0f;
    const bool bKeepAlive = Amplitude > 0.0f && Now - Hand.SentHapticTime > 0.25;
    if (!bChanged && !bKeepAlive && !bMustStop) return;
    PC->SetHapticsByValue(Frequency, Amplitude, bLeft ? EControllerHand::Left : EControllerHand::Right);
    Hand.SentHapticAmplitude = Amplitude; Hand.SentHapticFrequency = Frequency; Hand.SentHapticTime = Now;
}

void AGratiaStage1Runtime::UpdateDesktopCamera()
{
    if (!Camera.IsValid()) return;
    if (!bXRActive)
    {
        FVector Location = OriginalCameraRelative.GetLocation();
        Location.Z += DesktopEyeHeightCm;
        Camera->SetRelativeLocation(Location);
        bDesktopCameraApplied = true;
    }
    else if (bDesktopCameraApplied)
    {
        Camera->SetRelativeTransform(OriginalCameraRelative);
        bDesktopCameraApplied = false;
    }
}

void AGratiaStage1Runtime::ApplyHeight()
{
    if (!TrackingOrigin.IsValid()) return;
    FVector Location = TrackingOrigin->GetRelativeLocation();
    Location.Z = OriginalOriginZ + HeightOffsetCm;
    TrackingOrigin->SetRelativeLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
}

bool AGratiaStage1Runtime::IsSceneInteractionAllowed() const
{
    return (!SceneDirector || !SceneDirector->IsSceneInputBlocked()) && (!Menu || !Menu->bOpen);
}

UCameraComponent* AGratiaStage1Runtime::GetPlayerCamera() const { return Camera.Get(); }
APawn* AGratiaStage1Runtime::GetPlayerPawn() const { return PlayerPawn.Get(); }
APlayerController* AGratiaStage1Runtime::GetPlayerController() const { return PlayerController.Get(); }

void AGratiaStage1Runtime::PlaceAnchor(const FTransform& Transform)
{
    if (!IsFiniteTransform(Transform)) return;
    SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
    if (PlayerPawn.IsValid())
    {
        FVector Position = PlayerPawn->GetActorLocation();
        Position.Z = Transform.GetLocation().Z;
        PlayerPawn->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
    }
    bPendingRecenter = false;
    FinishRecenter();
    if (TargetCharacter.IsValid())
    {
        if (TargetCharacter->Interaction) TargetCharacter->Interaction->ResetState();
        if (TargetCharacter->SoftBodyInteraction) TargetCharacter->SoftBodyInteraction->ClearHands();
    }
}

void AGratiaStage1Runtime::AdjustHeight(float DeltaCm)
{
    if (!FMath::IsFinite(DeltaCm)) return;
    HeightOffsetCm = FMath::Clamp(HeightOffsetCm + DeltaCm, -100.0f, 100.0f);
    ApplyHeight();
    UE_LOG(LogGratiaStage1, Display, TEXT("Height offset=%.1f cm"), HeightOffsetCm);
}

void AGratiaStage1Runtime::ResetHeight()
{
    HeightOffsetCm = 0.0f;
    ApplyHeight();
    UE_LOG(LogGratiaStage1, Display, TEXT("Height offset reset to 0 cm"));
}

void AGratiaStage1Runtime::Recenter()
{
    if (!bPawnReady) return;
    // Lying down in a scene with a partner: the head goes to the partner's eyes; standing up
    // again leaves an automatically entered partner view.
    AGratiaPreviewCharacter* Character = TargetCharacter.Get();
    FTransform Eye;
    const bool bViewpoint = Character && Character->PerformanceStage && Character->PerformanceStage->GetViewpoint(Eye);
    if (bXRActive && !bPartnerView && bViewpoint && IsPlayerLying())
    {
        bPartnerViewAuto = SetPartnerView(true);
        if (bPartnerViewAuto) return;
    }
    if (bXRActive && bPartnerView && bPartnerViewAuto && !IsPlayerLying())
    {
        bPartnerViewAuto = false;
        SetPartnerView(false);
        return;
    }
    // A lying player has no meaningful HMD yaw; the partner view aligns the body axis itself.
    if (bXRActive && !bPartnerView)
    {
        // Preserve floor-space height; the room alignment is handled explicitly below.
        UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(0.0f, EOrientPositionSelector::Orientation);
    }
    bPendingRecenter = true;
}

void AGratiaStage1Runtime::FinishRecenter()
{
    bPendingRecenter = false;
    if (bPartnerView && RecenterToPartnerView()) return;
    if (!PlayerPawn.IsValid() || !Camera.IsValid() || !IsFiniteTransform(Camera->GetComponentTransform())) return;
    const double YawDelta = FMath::FindDeltaAngleDegrees(Camera->GetComponentRotation().Yaw, GetActorRotation().Yaw);
    PlayerPawn->AddActorWorldRotation(FRotator(0.0, YawDelta, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
    const FVector CameraPosition = Camera->GetComponentLocation();
    const FVector AnchorPosition = GetActorLocation();
    PlayerPawn->AddActorWorldOffset(FVector(AnchorPosition.X - CameraPosition.X, AnchorPosition.Y - CameraPosition.Y, 0.0),
        false, nullptr, ETeleportType::TeleportPhysics);
    UE_LOG(LogGratiaStage1, Display, TEXT("Recentered head XY to anchor %s; height offset=%.1f cm, XR=%s"),
        *GetActorLocation().ToString(), HeightOffsetCm, bXRActive ? TEXT("enabled") : TEXT("disabled"));
}

bool AGratiaStage1Runtime::SetPartnerView(bool bEnable)
{
    AGratiaPreviewCharacter* Character = TargetCharacter.Get();
    UGratiaPerformanceStage* Stage = Character ? Character->PerformanceStage.Get() : nullptr;
    FTransform Eye;
    if (bEnable && (!Stage || !Stage->GetViewpoint(Eye)))
    {
        UE_LOG(LogGratiaStage1, Display, TEXT("PARTNER_VIEW unavailable: the current pose has no partner viewpoint"));
        return false;
    }
    if (bEnable == bPartnerView) return true;
    bPartnerView = bEnable;
    if (Stage) Stage->SetViewpointActive(bEnable);
    if (!bEnable) bPartnerViewAuto = false;
    if (bEnable) FreeHeightOffsetCm = HeightOffsetCm;
    else
    {
        HeightOffsetCm = FreeHeightOffsetCm; ApplyHeight();
        if (!bXRActive && Camera.IsValid()) Camera->SetRelativeTransform(OriginalCameraRelative);
    }
    UE_LOG(LogGratiaStage1, Display, TEXT("PARTNER_VIEW %s"), bEnable ? TEXT("on") : TEXT("off"));
    Recenter();
    return true;
}

void AGratiaStage1Runtime::UpdatePartnerView()
{
    AGratiaPreviewCharacter* Character = TargetCharacter.Get();
    UGratiaPerformanceStage* Stage = Character ? Character->PerformanceStage.Get() : nullptr;
    if (!bPartnerViewRequested && FParse::Param(FCommandLine::Get(), TEXT("GratiaPartnerView")) && Stage)
    {
        FTransform Eye;
        if (Stage->GetViewpoint(Eye)) { bPartnerViewRequested = true; SetPartnerView(true); }
    }
    if (!bPartnerView) return;
    FTransform Eye;
    if (!Stage || !Stage->GetViewpoint(Eye)) { SetPartnerView(false); return; }
    // Desktop: the camera is the partner's eyes; VR keeps the tracked head (Recenter aligns it).
    if (!bXRActive && Camera.IsValid())
        Camera->SetWorldLocationAndRotation(Eye.GetLocation(), Eye.GetRotation());
}

bool AGratiaStage1Runtime::IsPlayerLying() const
{
    if (!Camera.IsValid() || !TrackingOrigin.IsValid()) return false;
    const double HeightCm = Camera->GetComponentLocation().Z - TrackingOrigin->GetComponentLocation().Z;
    return HeightCm < 130.0 && FMath::Abs(Camera->GetComponentQuat().GetUpVector().Z) < 0.6;
}

bool AGratiaStage1Runtime::RecenterToPartnerView()
{
    AGratiaPreviewCharacter* Character = TargetCharacter.Get();
    FTransform Eye;
    if (!Character || !Character->PerformanceStage || !Character->PerformanceStage->GetViewpoint(Eye)
        || !PlayerPawn.IsValid() || !Camera.IsValid() || !IsFiniteTransform(Camera->GetComponentTransform())) return false;
    if (!bXRActive) return true;
    // Lying: the top of the head gives the body axis (head -> partner's head). Standing: face
    // along the partner's body toward the feet, standing at the partner's eyes.
    const FQuat Head = Camera->GetComponentQuat();
    const bool bLying = IsPlayerLying();
    FVector Have = bLying ? Head.GetUpVector().GetSafeNormal2D() : Head.GetForwardVector().GetSafeNormal2D();
    FVector Want = bLying ? Eye.GetRotation().GetUpVector().GetSafeNormal2D() : -Eye.GetRotation().GetUpVector().GetSafeNormal2D();
    if (Have.IsNearlyZero() || Want.IsNearlyZero()) return false;
    const double YawDelta = FMath::FindDeltaAngleDegrees(Have.Rotation().Yaw, Want.Rotation().Yaw);
    PlayerPawn->AddActorWorldRotation(FRotator(0.0, YawDelta, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
    const FVector CameraPosition = Camera->GetComponentLocation();
    PlayerPawn->AddActorWorldOffset(FVector(Eye.GetLocation().X - CameraPosition.X, Eye.GetLocation().Y - CameraPosition.Y, 0.0),
        false, nullptr, ETeleportType::TeleportPhysics);
    // Lying on the floor or a bed: the eyes go to the partner's eye height.
    if (bLying)
    {
        HeightOffsetCm = FMath::Clamp(HeightOffsetCm + float(Eye.GetLocation().Z - Camera->GetComponentLocation().Z), -100.0f, 100.0f);
        ApplyHeight();
    }
    UE_LOG(LogGratiaStage1, Display, TEXT("PARTNER_VIEW recenter lying=%d yaw_delta=%.1f height_offset=%.1fcm eye=%s"),
        bLying ? 1 : 0, YawDelta, HeightOffsetCm, *Eye.GetLocation().ToString());
    return true;
}

bool AGratiaStage1Runtime::IsHandInteractionAllowed(bool bLeftHand) const
{
    const FHandProxy& Hand = bLeftHand ? LeftHand : RightHand;
    return bPawnReady && Hand.Gate.CanInteract() && !Hand.bForceLoss;
}

void AGratiaStage1Runtime::SetForcedTrackingLoss(bool bLeftHand, bool bForceLoss)
{
    FHandProxy& Hand = bLeftHand ? LeftHand : RightHand;
    Hand.bForceLoss = bForceLoss;
    if (bForceLoss)
    {
        Hand.Gate.Update(false, 0.0f, TrackingStableSeconds, RecoveryBlendSeconds);
        SetHandCollision(Hand, false);
    }
    UE_LOG(LogGratiaStage1, Display, TEXT("%s hand forced tracking loss=%s"), bLeftHand ? TEXT("Left") : TEXT("Right"),
        bForceLoss ? TEXT("yes") : TEXT("no"));
}

FString AGratiaStage1Runtime::GetStatusText() const
{
    const int32 Bodies = TargetCharacter.IsValid() && TargetCharacter->SecondaryMotion ? TargetCharacter->SecondaryMotion->GetActiveBodyCount() : 0;
    return FString::Printf(TEXT("GRATIA VR | %s | %s\nR: recenter | PgUp/PgDn: height | Home: reset\nF1: debug | F4 / Y/B: settings | F8/F9: tracking loss\nFrame ~%.1f ms / ~%.0f FPS | GT %.1f / RT %.1f / GPU %.1f ms\nL: %s | R: %s | Physics: %d\nHeight: %+.0f cm | Pawn: %s\nEngine timings; SteamVR delivery measured separately\n%s\n%s\n%s\n%s\n%s\nBuild: %s"),
        bXRActive ? TEXT("XR") : TEXT("DESKTOP"), Menu ? *Menu->QualityLabel() : TEXT("Medium"),
        EstimatedFrameMs, EstimatedFPS, GameThreadMs, RenderThreadMs, GPUFrameMs,
        HandStateLabel(LeftHandState), HandStateLabel(RightHandState), Bodies, HeightOffsetCm, bPawnReady ? TEXT("READY") : TEXT("MISSING COMPONENTS"),
        Locomotion ? *Locomotion->GetDiagnosticText() : TEXT("Movement missing"),
        TargetCharacter.IsValid() ? *TargetCharacter->Interaction->GetContactDiagnostics() : TEXT("Character target missing"),
        HandInput ? *HandInput->GetDiagnostics() : TEXT("Hand input missing"),
        TargetCharacter.IsValid() && TargetCharacter->SecondaryMotion ? *TargetCharacter->SecondaryMotion->GetHandDiagnostics() : TEXT("Physics missing"),
        TargetCharacter.IsValid() && TargetCharacter->SoftBodyInteraction ? *(TargetCharacter->SoftBodyInteraction->GetDiagnostics()
            + TEXT("\nBody grip: L ") + (LeftHand.GripBone.IsNone() ? FString(TEXT("-")) : LeftHand.GripBone.ToString())
            + TEXT(" | R ") + (RightHand.GripBone.IsNone() ? FString(TEXT("-")) : RightHand.GripBone.ToString())
            + TEXT("\nHands: L ") + (LeftHand.HandAnim.IsValid() ? LeftHand.HandAnim->GetDiagnostics() : FString(TEXT("default")))
            + TEXT(" | R ") + (RightHand.HandAnim.IsValid() ? RightHand.HandAnim->GetDiagnostics() : FString(TEXT("default"))))
            : TEXT("Soft body missing"), TEXT(GRATIA_BUILD_ID));
}

void AGratiaStage1Runtime::RunRequestedTests(float DeltaSeconds)
{
    Verification->RunRequestedTests(DeltaSeconds);
}

void AGratiaStage1Runtime::RunSoakAndMetrics(float DeltaSeconds)
{
    Verification->RunSoakAndMetrics(DeltaSeconds);
}
