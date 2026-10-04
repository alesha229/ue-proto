#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaLocomotion.generated.h"

class APawn;
class APlayerController;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;

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
    static FVector2D FilterStick(FVector2D Stick);
    bool RunChecks(FString& Failure);
    UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin="30", ClampMax="250"))
    float SpeedCmPerSecond = 120.0f;
    bool bEnabled = true;
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY() TObjectPtr<UInputAction> WalkAction;
    UPROPERTY() TObjectPtr<UInputAction> TurnAction;
    UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<APawn> Pawn;
    TWeakObjectPtr<UCameraComponent> Camera;
    bool bTurnArmed = true;
    bool bReportedInput = false;
    void BindPlayer();
    FVector SweepBody(FVector Delta) const;
};
