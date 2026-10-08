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
    /** XR template hand actions; fall back to the trigger when the action is unavailable. */
    float GetGrasp(bool bLeft) const;
    /** Grip squeeze (IA_GripLeft/Right in IMC_GratiaLocomotion; desktop C/V). */
    float GetGrip(bool bLeft) const;
    float GetIndexCurl(bool bLeft) const;
    /** Either stick clicked (IA_Recenter in IMC_GratiaLocomotion). */
    bool IsRecenterPressed() const;
    /** Thumb resting on the stick or a face button (IA_ThumbLeft/Right, touch; desktop B/N). */
    bool IsThumbDown(bool bLeft) const;
    FString GetDiagnostics() const;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY() TObjectPtr<UInputAction> LeftAction;
    UPROPERTY() TObjectPtr<UInputAction> RightAction;
    UPROPERTY() TObjectPtr<UInputAction> GripLeft;
    UPROPERTY() TObjectPtr<UInputAction> GripRight;
    UPROPERTY() TObjectPtr<UInputAction> GraspLeft;
    UPROPERTY() TObjectPtr<UInputAction> GraspRight;
    UPROPERTY() TObjectPtr<UInputAction> IndexLeft;
    UPROPERTY() TObjectPtr<UInputAction> IndexRight;
    UPROPERTY() TObjectPtr<UInputAction> RecenterAction;
    UPROPERTY() TObjectPtr<UInputAction> ThumbLeft;
    UPROPERTY() TObjectPtr<UInputAction> ThumbRight;
    float ReadAction(UInputAction* Action, bool bLeft) const;
    UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
    UPROPERTY() TObjectPtr<UEnhancedInputComponent> Input;
    TWeakObjectPtr<APlayerController> Controller;
    bool bReady = false;
};
