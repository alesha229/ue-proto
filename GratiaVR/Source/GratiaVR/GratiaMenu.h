#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaMenu.generated.h"
class UInputAction;
class UInputMappingContext;
class UTextRenderComponent;
class AGratiaPreviewCharacter;
class APlayerController;
class UEnhancedInputComponent;
UCLASS()
class GRATIAVR_API UGratiaMenu : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaMenu();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
    void Toggle();
    void ApplySelected();
    bool RunChecks();
    void SetCharacter(AGratiaPreviewCharacter* Value);
    bool bOpen = false;
    int32 Selected = 0;
    FString QualityLabel() const;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY() TObjectPtr<UInputAction> ToggleAction;
    UPROPERTY() TObjectPtr<UInputAction> NextAction;
    UPROPERTY() TObjectPtr<UInputAction> ApplyAction;
    UPROPERTY() TObjectPtr<UInputMappingContext> MenuMapping;
    UPROPERTY() TObjectPtr<UTextRenderComponent> Text;
    UPROPERTY() TObjectPtr<UEnhancedInputComponent> ActionInput;
    TWeakObjectPtr<APlayerController> BoundController;
    void BindInput(APlayerController* Controller);
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    bool bToggleArmed = true, bNextArmed = true, bApplyArmed = true;
    void Refresh();
    void ApplyQuality(int32 Profile, bool bResetMotion = true);
};
