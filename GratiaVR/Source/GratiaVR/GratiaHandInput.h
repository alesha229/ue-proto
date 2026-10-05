#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaHandInput.generated.h"
class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;
class APlayerController;

/** Controller trigger actions, independent of movement and character reactions. */
UCLASS()
class GRATIAVR_API UGratiaHandInput : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaHandInput();
    void UpdateInput();
    float GetTrigger(bool bLeft) const;
    FString GetDiagnostics() const;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY() TObjectPtr<UInputAction> LeftAction;
    UPROPERTY() TObjectPtr<UInputAction> RightAction;
    UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
    UPROPERTY() TObjectPtr<UEnhancedInputComponent> Input;
    TWeakObjectPtr<APlayerController> Controller;
    bool bReady = false;
};
