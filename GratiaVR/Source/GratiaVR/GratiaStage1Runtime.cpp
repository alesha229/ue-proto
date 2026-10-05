#include "GratiaStage1Runtime.h"
#include "GratiaRuntimeVerification.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaLocomotion.h"
#include "GratiaInteraction.h"
#include "GratiaMenu.h"
#include "GratiaSecondaryMotion.h"
#include "GratiaSoftBodyInteraction.h"
#include "GratiaBodySurface.h"
#include "GratiaCharacterProfile.h"
#include "GratiaHandAnimInstance.h"
#include "GratiaBuildInfo.h"
#include "GratiaHandInput.h"

#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "InputCoreTypes.h"
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
}

AGratiaStage1Runtime::AGratiaStage1Runtime()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CalibrationAnchor"));
    Locomotion = CreateDefaultSubobject<UGratiaLocomotion>(TEXT("SmoothLocomotion"));
    Menu = CreateDefaultSubobject<UGratiaMenu>(TEXT("WorldMenu"));
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
    UE_LOG(LogGratiaStage1, Display, TEXT("BUILD id=%s commit=%s"), TEXT(GRATIA_BUILD_ID), TEXT(GRATIA_BUILD_COMMIT));
    UE_LOG(LogGratiaStage1, Display, TEXT("Stage 1 runtime started. R=recenter, PgUp/PgDn=height, Home=reset height, F1=debug, F8/F9=toggle forced left/right tracking loss."));
}

void AGratiaStage1Runtime::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
}

void AGratiaStage1Runtime::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    BindPlayer();
    bXRActive = UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
    UpdateDesktopCamera();

    // Apply recenter on the next tick, after the runtime has updated the head pose.
    if (bPendingRecenter)
    {
        FinishRecenter();
    }

    if (PlayerController.IsValid())
    {
        APlayerController* PC = PlayerController.Get();
        if (PC->WasInputKeyJustPressed(EKeys::R)) Recenter();
        if (PC->WasInputKeyJustPressed(EKeys::PageUp)) AdjustHeight(HeightStepCm);
        if (PC->WasInputKeyJustPressed(EKeys::PageDown)) AdjustHeight(-HeightStepCm);
        if (PC->WasInputKeyJustPressed(EKeys::Home)) ResetHeight();
        if (PC->WasInputKeyJustPressed(EKeys::F1)) bShowDebug = !bShowDebug;
        if (PC->WasInputKeyJustPressed(EKeys::F8)) SetForcedTrackingLoss(true, !LeftHand.bForceLoss);
        if (PC->WasInputKeyJustPressed(EKeys::F9)) SetForcedTrackingLoss(false, !RightHand.bForceLoss);
    }

    // Character and scene target are serialized on the runtime actor or set explicitly.
    HandInput->UpdateInput();
    UpdateHand(LeftHand, true, DeltaSeconds);
    UpdateHand(RightHand, false, DeltaSeconds);
    LeftHandState = LeftHand.Gate.State;
    RightHandState = RightHand.Gate.State;

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
        if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
        {
            FCollisionSnapshot Snapshot;
            Snapshot.Component = Primitive;
            Snapshot.Collision = Primitive->GetCollisionEnabled();
            Snapshot.bGenerateOverlapEvents = Primitive->GetGenerateOverlapEvents();
            if (Primitive == Contact)
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
    if (!Hand.Visual.IsValid() || !Hand.Controller.IsValid()) return;
    if (!Hand.OriginalParent.IsValid()) return;
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
    if (Hand.Gate.State != EGratiaHandState::Tracked && !Hand.GripBone.IsNone()) ReleaseBodyGrip(Hand, bLeft, TEXT("tracking"));
    if (Hand.Gate.State == EGratiaHandState::Tracked)
    {
        // The palm collides with the body surface (not a wide sphere around the wrist).
        FGratiaPalmFrame Palm;
        const FVector PalmLocal = Hand.HandAnim.IsValid() && Hand.HandAnim->GetPalmFrame(Palm) ? Palm.Point * Target.GetScale3D() : FVector::ZeroVector;
        const FTransform Constrained = TargetCharacter.IsValid() && TargetCharacter->Interaction
            ? TargetCharacter->Interaction->ConstrainHand(Hand.LastWorld, Target, bLeft, PalmLocal) : Target;
        const FTransform ContactTarget = ApplyBodySurface(Hand, bLeft, Target, Constrained, DeltaSeconds);
        const bool bBlocked = !ContactTarget.Equals(Target, 0.01);
        // Exact original local transform retains the template's late controller update and hand alignment.
        if (bBlocked)
        {
            if (Hand.Visual->GetAttachParent()) Hand.Visual->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
            Hand.Visual->SetWorldTransform(ContactTarget, false, nullptr, ETeleportType::TeleportPhysics);
        }
        else if (Hand.Visual->GetAttachParent() != Hand.OriginalParent.Get())
        {
            Hand.Visual->AttachToComponent(Hand.OriginalParent.Get(), FAttachmentTransformRules::KeepWorldTransform, Hand.OriginalSocket);
        }
        if (!bBlocked) Hand.Visual->SetRelativeTransform(Hand.OriginalRelative, false, nullptr, ETeleportType::TeleportPhysics);
        VisualWorld = ContactTarget;
        SetHandCollision(Hand, true);
    }
    else if (Hand.Gate.State == EGratiaHandState::Recovering)
    {
        if (Before != EGratiaHandState::Recovering) Hand.RecoveryStart = Hand.LastWorld;
        VisualWorld = BlendTransform(Hand.RecoveryStart, Target, Hand.Gate.RecoveryAlpha(RecoveryBlendSeconds));
        Hand.Visual->SetWorldTransform(VisualWorld, false, nullptr, ETeleportType::TeleportPhysics);
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
        }
    }
    if (IsFiniteTransform(VisualWorld)) Hand.LastWorld = VisualWorld;
    // The opt-in soak supplies its own samples after this update. Parked desktop
    // hands must not keep resetting that scenario's correction-recovery timer.
    if (TargetCharacter.IsValid() && TargetCharacter->Interaction && !Verification->OwnsSyntheticContactSamples())
    {
        TargetCharacter->Interaction->SetHandSample(bLeft, Target, VisualWorld, Hand.Gate.CanInteract());
        if (TargetCharacter->SecondaryMotion && !Verification->IsHandPhysicsQAActive())
            TargetCharacter->SecondaryMotion->SubmitHand(bLeft, VisualWorld.GetLocation(),
                TargetCharacter->Interaction->IsHandSampleReady(bLeft) && (!Menu || !Menu->bOpen), DeltaSeconds,
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
            const bool bAllowed = TargetCharacter->Interaction->IsHandSampleReady(bLeft) && (!Menu || !Menu->bOpen);
            const float Grab = HandInput ? FMath::Max(HandInput->GetTrigger(bLeft), HandInput->GetGrip(bLeft)) : 0.0f;
            SoftBody->SubmitHand(bLeft, VisiblePalm, RawPalm, bAllowed, DeltaSeconds, Grab, Fingers);
            // Inside a soft zone the visible hand sinks by the bounded press depth.
            if (SoftBody->HasPress(bLeft) && Hand.Gate.State == EGratiaHandState::Tracked)
            {
                if (Hand.Visual->GetAttachParent()) Hand.Visual->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
                Hand.Visual->SetWorldLocation(SoftBody->GetPressPoint(bLeft) - (VisiblePalm - VisualWorld.GetLocation()), false, nullptr, ETeleportType::TeleportPhysics);
            }
            // Grip onto a body part gives a short tap; soft-zone vibration follows depth.
            Hand.GripPulse = FMath::Max(0.0f, Hand.GripPulse - DeltaSeconds);
            const float Amplitude = FMath::Max(SoftBody->GetHapticAmplitude(bLeft), Hand.GripPulse > 0 ? 0.45f : 0.0f);
            UpdateHaptics(Hand, bLeft, bAllowed && bXRActive ? Amplitude : 0.0f, Hand.GripPulse > 0 ? 0.3f : SoftBody->GetHapticFrequency(bLeft));
        }
    }
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
    // Wrapping grip closes every finger onto the part; near a surface the fingers rest on it.
    const float Rest = Hand.GripBone.IsNone() ? 0.85f * Hand.SurfaceWeight : 1.0f;
    // Thumb: 1 is its relaxed pose; contact caps extend it out of the body.
    Anim->FingerInput[0] = Anim->ThumbOpenPose ? 1.0f : FMath::Max(Grasp, Rest);
    Anim->FingerInput[1] = FMath::Max(Index, Rest);
    Anim->FingerInput[2] = Anim->FingerInput[3] = Anim->FingerInput[4] = FMath::Max(Grasp, Rest);
    const auto* Profile = TargetCharacter.IsValid() ? TargetCharacter->CharacterProfile.Get() : nullptr;
    Anim->bConform = Profile && Profile->SoftBody.bFingerConform && Hand.Gate.CanInteract();
    if (Profile) { Anim->FingerRadiusCm = Profile->SoftBody.FingerRadiusCm; Anim->ConformMarginCm = Profile->SoftBody.FingerConformMarginCm; }
    // Squeezing (grip) lets the fingers sink into soft zones; the press dent opens under them.
    Anim->ConformCapsules.Reset();
    if (Anim->bConform && TargetCharacter->BodySurface && TargetCharacter->BodySurface->HasSurface())
        TargetCharacter->BodySurface->GatherConformShapes(Near, 30.0f, Anim->ConformSpheres, Anim->ConformCapsules, Profile->SoftBody.SquishDepthCm * Grasp);
    else if (Anim->bConform && TargetCharacter->SoftBodyInteraction)
        TargetCharacter->SoftBodyInteraction->GetConformSpheres(Near, 30.0f, Anim->ConformSpheres, Profile->SoftBody.SquishDepthCm * Grasp);
    else Anim->ConformSpheres.Reset();
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
    AGratiaPreviewCharacter* Character = TargetCharacter.Get();
    const UGratiaCharacterProfile* Profile = Character ? Character->CharacterProfile.Get() : nullptr;
    UGratiaBodySurface* Surface = Character ? Character->BodySurface.Get() : nullptr;
    FGratiaPalmFrame Palm;
    if (!Profile || !Profile->HandSurface.bEnabled || !Surface || !Hand.HandAnim.IsValid() || !Hand.HandAnim->GetPalmFrame(Palm)
        || (Menu && Menu->bOpen) || !Character->CharacterMesh)
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
    // Soft zones keep their own trigger/grip grab; limbs and torso are wrapped here.
    if (Hand.bGripArmed && Grip >= Settings.GripStartInput
        && Surface->FindNearest(PalmPoint, Settings.GripReachCm + Settings.PalmThicknessCm, Hit, false))
    {
        FTransform Bone;
        if (BoneFrame(Hit.Bone, Bone))
        {
            const FTransform Wrap = UGratiaBodySurface::SolveWrap(Constrained, Palm, Hit, Settings.PalmThicknessCm,
                Profile->SoftBody.FingerRadiusCm + Profile->SoftBody.FingerConformMarginCm);
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
    if (Settings.AdaptDistanceCm > 0 && Surface->FindNearest(PalmPoint, Settings.AdaptDistanceCm + Settings.PalmThicknessCm, Hit, true))
    {
        Hand.SurfaceWeight = 1.0f - FMath::Clamp((Hit.Gap - Settings.PalmThicknessCm) / Settings.AdaptDistanceCm, 0.0f, 1.0f);
        return UGratiaBodySurface::LeanToSurface(Constrained, Palm, Hit, Hand.SurfaceWeight, Settings.AdaptMaxDegrees, Settings.PalmThicknessCm);
    }
    return Constrained;
}

void AGratiaStage1Runtime::UpdateHaptics(FHandProxy& Hand, bool bLeft, float Amplitude, float Frequency)
{
    APlayerController* PC = PlayerController.Get();
    if (!PC) return;
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    Amplitude = FMath::IsFinite(Amplitude) ? FMath::Clamp(Amplitude, 0.0f, 1.0f) : 0.0f;
    Frequency = FMath::IsFinite(Frequency) ? FMath::Clamp(Frequency, 0.0f, 1.0f) : 0.0f;
    // OpenXR haptics expire on their own; refresh while active, change only on a real difference.
    const bool bChanged = FMath::Abs(Amplitude - Hand.SentHapticAmplitude) > 0.02f || FMath::Abs(Frequency - Hand.SentHapticFrequency) > 0.05f;
    const bool bKeepAlive = Amplitude > 0.0f && Now - Hand.SentHapticTime > 0.25;
    if (!bChanged && !bKeepAlive) return;
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
    if (bXRActive)
    {
        // Preserve floor-space height; the room alignment is handled explicitly below.
        UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(0.0f, EOrientPositionSelector::Orientation);
    }
    bPendingRecenter = true;
}

void AGratiaStage1Runtime::FinishRecenter()
{
    bPendingRecenter = false;
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
