#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaLocomotion.generated.h"

class APawn;
class APlayerController;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;

/** Horizontal, head-relative walking with a swept body proxy for the small flat room. */
UCLASS()
class GRATIAVR_API UGratiaLocomotion : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaLocomotion();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    bool IsReady() const;
    void Walk(FVector2D Stick, float Seconds);
    void SnapTurn(float Degrees);
    static FVector2D FilterStick(FVector2D Stick, float DeadZone = 0.18f);
    bool RunChecks(FString& Failure);
    UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin="30", ClampMax="250"))
    float SpeedCmPerSecond = 120.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement")
    bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="0", ClampMax="0.9"))
    float StickDeadZone = 0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="1", ClampMax="90"))
    float SnapDegrees = 30.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="1"))
    float BodyRadiusCm = 22.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="1"))
    float BodyHalfHeightCm = 70.0f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Movement|Diagnostics")
    FVector2D MappedStick = FVector2D::ZeroVector;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Movement|Diagnostics")
    FVector2D RawStick = FVector2D::ZeroVector;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Movement|Diagnostics")
    FVector LastPawnDelta = FVector::ZeroVector;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Movement|Diagnostics")
    FString MovementReason = TEXT("initializing");
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Movement|Diagnostics")
    bool bRawKeyChannelAvailable = true;
    FString GetDiagnosticText() const;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY() TObjectPtr<UInputAction> WalkAction;
    UPROPERTY() TObjectPtr<UInputAction> TurnAction;
    UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
    UPROPERTY() TObjectPtr<UEnhancedInputComponent> ActionInput;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<APawn> Pawn;
    TWeakObjectPtr<UCameraComponent> Camera;
    bool bTurnArmed = true;
    float DiagnosticSeconds = 0.0f;
    bool bMappingReady = false;
    FString LastReportedReason;
    void BindPlayer();
    FVector SweepBody(FVector Delta) const;
};
