#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GratiaStage1Runtime.generated.h"

class APlayerController;
class APawn;
class AGratiaPreviewCharacter;
class UCameraComponent;
class UMotionControllerComponent;
class UPrimitiveComponent;
class USphereComponent;
class UTextRenderComponent;
class UGratiaLocomotion;
class UGratiaMenu;
class UGratiaRuntimeVerification;

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

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stage 1")
    TObjectPtr<UTextRenderComponent> DebugPanel;

    UPROPERTY(VisibleAnywhere, Category = "Movement")
    TObjectPtr<UGratiaLocomotion> Locomotion;
    UPROPERTY(VisibleAnywhere, Category = "Menu")
    TObjectPtr<UGratiaMenu> Menu;

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

    UFUNCTION(BlueprintCallable, Category = "Stage 1|Calibration")
    void Recenter();

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
    };

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
    float DebugRefreshSeconds = 0.0f;

    void BindPlayer();
    void BindHand(FHandProxy& Hand, FName ControllerName, FName VisualName);
    void RestoreHand(FHandProxy& Hand);
    void SetHandCollision(FHandProxy& Hand, bool bEnabled);
    void UpdateHand(FHandProxy& Hand, bool bLeft, float DeltaSeconds);
    FTransform GetParkedTransform(bool bLeft, const FVector& Scale) const;
    void ApplyHeight();
    void FinishRecenter();
    void UpdateDesktopCamera();
    void RunRequestedTests(float DeltaSeconds);
    void RunSoakAndMetrics(float DeltaSeconds);
};
