#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GratiaStage1Runtime.generated.h"

class APlayerController;
class APawn;
class AGratiaPreviewCharacter;
class AGratiaPenetrator;
class UCameraComponent;
class UMotionControllerComponent;
class UPrimitiveComponent;
class USphereComponent;
class UTextRenderComponent;
class UGratiaLocomotion;
class UGratiaMenu;
class UGratiaSceneDirector;
class UGratiaSceneFlowVerification;
class UGratiaRuntimeVerification;
class UGratiaHandInput;

UENUM(BlueprintType)
enum class EGratiaHandState : uint8
{
    Unavailable,
    Acquiring,
    Recovering,
    Tracked
};

/** A hand cannot interact until continuous full tracking and recovery have both completed. */
struct FGratiaTrackingGate
{
    EGratiaHandState State = EGratiaHandState::Unavailable;
    float StableSeconds = 0.0f;
    float RecoverySeconds = 0.0f;

    void Update(bool bFullyTracked, float DeltaSeconds, float StableRequired, float RecoveryRequired)
    {
        if (!bFullyTracked)
        {
            State = EGratiaHandState::Unavailable;
            StableSeconds = RecoverySeconds = 0.0f;
            return;
        }

        const float Step = FMath::IsFinite(DeltaSeconds) ? FMath::Clamp(DeltaSeconds, 0.0f, 0.1f) : 0.0f;
        if (State == EGratiaHandState::Tracked)
        {
            return;
        }
        if (State == EGratiaHandState::Recovering)
        {
            RecoverySeconds += Step;
            if (RecoverySeconds >= FMath::Max(0.05f, RecoveryRequired))
            {
                State = EGratiaHandState::Tracked;
            }
            return;
        }

        State = EGratiaHandState::Acquiring;
        StableSeconds += Step;
        if (StableSeconds >= FMath::Max(0.05f, StableRequired))
        {
            State = EGratiaHandState::Recovering;
            RecoverySeconds = 0.0f;
        }
    }

    float RecoveryAlpha(float RecoveryRequired) const
    {
        return FMath::Clamp(RecoverySeconds / FMath::Max(0.05f, RecoveryRequired), 0.0f, 1.0f);
    }

    bool CanInteract() const { return State == EGratiaHandState::Tracked; }
};

/**
 * Visible hand = controller pose + contact offset (surface, lean, grip, cup, press).
 * Offset changes that the controller's own motion explains (pushing into the body, a held grip
 * while the controller moves) are followed exactly: no lag, no extra sinking. Jumps beyond that
 * (contact shape switch, grip/cup/press on or off) become a residual that eases out, so the
 * hand neither pops nor trails the controller.
 */
struct FGratiaHandOffsetSmoother
{
    FTransform PrevTarget = FTransform::Identity;
    FVector PrevOffset = FVector::ZeroVector;
    FQuat PrevOffsetRotation = FQuat::Identity;
    FVector Residual = FVector::ZeroVector;
    FQuat RotationResidual = FQuat::Identity;
    bool bValid = false;

    /** Forget the previous frame: the next update continues from what is on screen. */
    void Reset() { bValid = false; }
    void Settle() { Residual = FVector::ZeroVector; RotationResidual = FQuat::Identity; }
    bool IsSettled() const
    {
        return Residual.Size() <= 0.05 && FMath::RadiansToDegrees(FQuat::Identity.AngularDistance(RotationResidual)) <= 0.2;
    }

    /** Target: controller pose; Desired: pose with contact; Shown: pose on screen last frame. */
    FTransform Update(const FTransform& Target, const FTransform& Desired, const FTransform& Shown, float DeltaSeconds,
        float TimeConstant = 0.035f, double JumpCm = 0.5, double JumpDegrees = 3.0, double LeverCm = 10.0, double MaxResidualCm = 15.0)
    {
        const FVector Offset = Desired.GetLocation() - Target.GetLocation();
        const FQuat OffsetRotation = Desired.GetRotation() * Target.GetRotation().Inverse();
        if (!bValid)
        {
            Residual = Shown.GetLocation() - Desired.GetLocation();
            RotationResidual = Shown.GetRotation() * Desired.GetRotation().Inverse();
        }
        else
        {
            // A turning controller swings the constrained palm around its origin (lever).
            const double Moved = FVector::Distance(Target.GetLocation(), PrevTarget.GetLocation());
            const double Turned = Target.GetRotation().AngularDistance(PrevTarget.GetRotation());
            if ((Offset - PrevOffset).Size() > Moved + LeverCm * Turned + JumpCm) Residual -= Offset - PrevOffset;
            if (FMath::RadiansToDegrees(OffsetRotation.AngularDistance(PrevOffsetRotation)) > FMath::RadiansToDegrees(Turned) + JumpDegrees)
                RotationResidual = RotationResidual * PrevOffsetRotation * OffsetRotation.Inverse();
        }
        PrevTarget = Target;
        PrevOffset = Offset;
        PrevOffsetRotation = OffsetRotation;
        bValid = true;
        const float Step = FMath::IsFinite(DeltaSeconds) ? FMath::Clamp(DeltaSeconds, 0.0f, 0.1f) : 0.0f;
        const float Alpha = 1.0f - FMath::Exp(-Step / FMath::Max(0.001f, TimeConstant));
        Residual = (Residual * (1.0f - Alpha)).GetClampedToMaxSize(MaxResidualCm);
        RotationResidual = FQuat::Slerp(RotationResidual, FQuat::Identity, Alpha).GetNormalized();
        if (Residual.ContainsNaN() || RotationResidual.ContainsNaN()) Settle();
        return FTransform(RotationResidual * Desired.GetRotation(), Desired.GetLocation() + Residual, Desired.GetScale3D());
    }
};

/** Companion for the unchanged XRFramework pawn; no character/model assets are required. */
UCLASS()
class GRATIAVR_API AGratiaStage1Runtime : public AActor
{
    GENERATED_BODY()

public:
    AGratiaStage1Runtime();
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Character")
    TWeakObjectPtr<AGratiaPreviewCharacter> TargetCharacter;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Interaction")
    TObjectPtr<AActor> SceneContactActor;
    UFUNCTION(BlueprintCallable, Category = "Character")
    void SetTargetCharacter(AGratiaPreviewCharacter* Character);

    /** Jointed primitive the hands can pick up (grip) and the character's channels respond to. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Primitive")
    TObjectPtr<AGratiaPenetrator> Primitive;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive")
    TSubclassOf<AGratiaPenetrator> PrimitiveClass;
    /** Shows the primitive in front of the player (spawned on demand) or removes it. */
    UFUNCTION(BlueprintCallable, Category = "Primitive")
    bool SetPrimitiveShown(bool bShown);
    bool IsPrimitiveShown() const { return Primitive != nullptr; }
    UFUNCTION(BlueprintCallable, Category = "Primitive")
    void CyclePrimitiveSize();
    FString GetPrimitiveLabel() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stage 1")
    TObjectPtr<UTextRenderComponent> DebugPanel;

    UPROPERTY(VisibleAnywhere, Category = "Movement")
    TObjectPtr<UGratiaLocomotion> Locomotion;
    UPROPERTY(VisibleAnywhere, Category = "Menu")
    TObjectPtr<UGratiaMenu> Menu;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scenes")
    TObjectPtr<UGratiaSceneDirector> SceneDirector;
    UCameraComponent* GetPlayerCamera() const;
    APawn* GetPlayerPawn() const;
    APlayerController* GetPlayerController() const;
    /** Place the calibration anchor and align the tracked head to it, preserving floor height. */
    void PlaceAnchor(const FTransform& Transform);
    bool IsSceneInteractionAllowed() const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UGratiaHandInput> HandInput;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage 1|Calibration", meta = (ClampMin = "0.5", ClampMax = "10.0", Units = "cm"))
    float HeightStepCm = 2.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Stage 1|Calibration", meta = (Units = "cm"))
    float HeightOffsetCm = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage 1|Tracking", meta = (ClampMin = "0.05", ClampMax = "2.0", Units = "s"))
    float TrackingStableSeconds = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage 1|Tracking", meta = (ClampMin = "0.05", ClampMax = "2.0", Units = "s"))
    float RecoveryBlendSeconds = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage 1|Tracking", meta = (ClampMin = "0.1", ClampMax = "30.0"))
    float ParkingInterpSpeed = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage 1|Desktop", meta = (ClampMin = "100.0", ClampMax = "220.0", Units = "cm"))
    float DesktopEyeHeightCm = 165.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage 1|Debug")
    bool bShowDebug = false;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Stage 1|Debug")
    bool bPawnReady = false;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Stage 1|Debug")
    bool bXRActive = false;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Stage 1|Debug")
    EGratiaHandState LeftHandState = EGratiaHandState::Unavailable;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Stage 1|Debug")
    EGratiaHandState RightHandState = EGratiaHandState::Unavailable;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Stage 1|Debug")
    float EstimatedFPS = 0.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Stage 1|Debug")
    float EstimatedFrameMs = 0.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Stage 1|Debug")
    float GameThreadMs = 0.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Stage 1|Debug")
    float RenderThreadMs = 0.0f;

    float GPUFrameMs = 0.0f;
    /** Frame-time log window: worst frame and its thread times, slow frame count (VR hitch diagnosis). */
    float PerfWindowSeconds = 0.0f, PerfWorstMs = 0.0f, PerfWorstGameMs = 0.0f, PerfWorstRenderMs = 0.0f, PerfWorstGPUMs = 0.0f;
    int32 PerfFrames = 0, PerfSlowFrames = 0, PerfQuietWindows = 0, PerfSpikesThisSecond = 0;
    double PerfSpikeSecond = 0.0;
    void LogFramePerformance(float DeltaSeconds);
    FString GetPerformanceContext() const;

    UFUNCTION(BlueprintCallable, Category = "Stage 1|Calibration")
    void Recenter();

    /** Watch the current performance from its partner's eyes (performance scene viewpoint).
     *  In VR: lie down (floor or bed), then Recenter puts the head at the partner's eyes. */
    UFUNCTION(BlueprintCallable, Category = "Stage 1|Calibration")
    bool SetPartnerView(bool bEnable);
    bool IsPartnerView() const { return bPartnerView; }

    UFUNCTION(BlueprintCallable, Category = "Stage 1|Calibration")
    void AdjustHeight(float DeltaCm);

    UFUNCTION(BlueprintCallable, Category = "Stage 1|Calibration")
    void ResetHeight();

    /** Later interaction systems must consult this gate, rather than trusting a stale controller pose. */
    UFUNCTION(BlueprintPure, Category = "Stage 1|Tracking")
    bool IsHandInteractionAllowed(bool bLeftHand) const;

    /** Diagnostic override only removes tracking; it never fabricates a tracked controller. */
    UFUNCTION(BlueprintCallable, Category = "Stage 1|Debug")
    void SetForcedTrackingLoss(bool bLeftHand, bool bForceLoss);

    FString GetStatusText() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    friend class UGratiaRuntimeVerification;

    UPROPERTY()
    TObjectPtr<UGratiaRuntimeVerification> Verification;
    UPROPERTY() TObjectPtr<UGratiaSceneFlowVerification> SceneFlowVerification;

    struct FCollisionSnapshot
    {
        TWeakObjectPtr<UPrimitiveComponent> Component;
        ECollisionEnabled::Type Collision = ECollisionEnabled::NoCollision;
        bool bGenerateOverlapEvents = false;
    };

    struct FHandProxy
    {
        TWeakObjectPtr<UMotionControllerComponent> Controller;
        TWeakObjectPtr<USceneComponent> Visual;
        TWeakObjectPtr<USphereComponent> ContactCollider;
        TWeakObjectPtr<USceneComponent> OriginalParent;
        FName OriginalSocket;
        TArray<FCollisionSnapshot> Collisions;
        FTransform OriginalRelative = FTransform::Identity;
        FTransform LastWorld = FTransform::Identity;
        FTransform RecoveryStart = FTransform::Identity;
        FGratiaTrackingGate Gate;
        bool bCollisionsEnabled = false;
        bool bForceLoss = false;
        TWeakObjectPtr<class UGratiaHandAnimInstance> HandAnim;
        float SentHapticAmplitude = 0.0f;
        float SentHapticFrequency = 0.0f;
        double SentHapticTime = -1.0;
        // Body grip: the hand wraps around a body part and stays on it until released.
        FName GripBone;
        FTransform GripRelative = FTransform::Identity;
        FTransform GripFrom = FTransform::Identity;
        float GripBlend = 1.0f;
        bool bGripArmed = false;
        float GripPulse = 0.0f;
        /** 0..1 how much the free hand leans onto a nearby surface (fingers rest on it). */
        float SurfaceWeight = 0.0f;
        /** Cupping a soft part (breast/butt): blend in, and the squeeze amount (trigger/grip). */
        FName CupBone;
        float CupBlend = 0.0f;
        float CupSqueeze = 0.0f;
        /** Visible hand after the final smoothing stage (contact/lean/grip/cup/press changes). */
        FTransform Smoothed = FTransform::Identity;
        bool bSmoothedValid = false;
        FGratiaHandOffsetSmoother OffsetSmoother;
        float LeanWeight = 0.0f;
        /** Holding the primitive: it rides the controller (not the constrained hand) at this offset. */
        bool bHoldsPrimitive = false;
        bool bPrimitiveArmed = true;
        FTransform PrimitiveRelative = FTransform::Identity;
    };
    /** Grip near the primitive's handle picks it up, release lets go; the held primitive follows the controller. */
    void UpdatePrimitiveGrab(FHandProxy& Hand, bool bLeft, const FTransform& Target);
    void ReleasePrimitive(FHandProxy& Hand, bool bLeft, const TCHAR* Reason);
    /** Body-surface hand pose for a tracked hand: wrap grip, hold, release, or lean onto the body. */
    FTransform ApplyBodySurface(FHandProxy& Hand, bool bLeft, const FTransform& Target, const FTransform& Constrained, float DeltaSeconds);
    void ReleaseBodyGrip(FHandProxy& Hand, bool bLeft, const TCHAR* Reason);
    /** Applies the desired visible hand smoothly; a free hand rides the controller directly. */
    void ApplyVisualHand(FHandProxy& Hand, const FTransform& Target, const FTransform& Desired, float DeltaSeconds);
    void UpdateHandPose(FHandProxy& Hand, bool bLeft, const FVector& Near);
    void UpdateHaptics(FHandProxy& Hand, bool bLeft, float Amplitude, float Frequency);

    TWeakObjectPtr<APlayerController> PlayerController;
    TWeakObjectPtr<APawn> PlayerPawn;
    TWeakObjectPtr<UCameraComponent> Camera;
    TWeakObjectPtr<USceneComponent> TrackingOrigin;
    FHandProxy LeftHand;
    FHandProxy RightHand;
    FTransform OriginalCameraRelative = FTransform::Identity;
    double OriginalOriginZ = 0.0;
    bool bDesktopCameraApplied = false;
    bool bPendingRecenter = false;
    bool bPartnerView = false;
    bool bPartnerViewRequested = false;
    /** Partner view entered by recentering while lying (standing up and recentering leaves it). */
    bool bPartnerViewAuto = false;
    float FreeHeightOffsetCm = 0.0f;
    float DebugRefreshSeconds = 0.0f;

    void BindPlayer();
    void BindHand(FHandProxy& Hand, FName ControllerName, FName VisualName);
    void RestoreHand(FHandProxy& Hand);
    void SetHandCollision(FHandProxy& Hand, bool bEnabled);
    void UpdateHand(FHandProxy& Hand, bool bLeft, float DeltaSeconds);
    FTransform GetParkedTransform(bool bLeft, const FVector& Scale) const;
    void ApplyHeight();
    void FinishRecenter();
    bool RecenterToPartnerView();
    /** HMD below 1.3 m (floor, bed, reclined) with the head tilted far from upright: the player lies. */
    bool IsPlayerLying() const;
    void UpdatePartnerView();
    void UpdateDesktopCamera();
    void RunRequestedTests(float DeltaSeconds);
    void RunSoakAndMetrics(float DeltaSeconds);
};
