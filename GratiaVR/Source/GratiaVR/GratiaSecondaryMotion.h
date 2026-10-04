#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaSecondaryMotion.generated.h"

class AGratiaPreviewCharacter;
class UPhysicalAnimationComponent;

/** Driven Chaos bodies on accessory chains; the planted character core stays animated. */
UCLASS()
class GRATIAVR_API UGratiaSecondaryMotion : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaSecondaryMotion();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
    void RefreshSettings(bool bForce = false);
    void ResetPhysics();
    bool RunChecks(FString& Failure);
    int32 GetActiveBodyCount() const { return ActiveBones.Num(); }
    bool HasFault() const { return bFault; }
protected:
    virtual void BeginPlay() override;
private:
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    UPROPERTY() TObjectPtr<UPhysicalAnimationComponent> Driver;
    TArray<FName> ActiveBones;
    int32 SettingsSignature = INDEX_NONE;
    bool bFault = false;
    bool bWaitForDriverTick = true;
    float CheckSeconds = 0.0f;
};
