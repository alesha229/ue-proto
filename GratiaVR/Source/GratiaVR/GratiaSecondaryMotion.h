#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaSecondaryMotion.generated.h"

class AGratiaPreviewCharacter;
class UPhysicalAnimationComponent;
class UGratiaCharacterProfile;

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
    /** Called after tracking and the independent proxy solver. Consumed once next PrePhysics. */
    void SubmitHand(bool bLeft, const FVector& Position, bool bAllowed, float Delta);
    void ClearHands();
    const TArray<FName>& GetActiveBones() const { return ActiveBones; }
    int32 GetHandPushCount(bool bLeft) const { return HandPushCounts[bLeft ? 0 : 1]; }
    int32 GetActiveBodyCount() const { return ActiveBones.Num(); }
    bool HasFault() const { return bFault; }
protected:
    virtual void BeginPlay() override;
private:
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<UGratiaCharacterProfile> LastProfile;
    UPROPERTY() TObjectPtr<UPhysicalAnimationComponent> Driver;
    TArray<FName> ActiveBones;
    int32 SettingsSignature = INDEX_NONE;
    bool bFault = false;
    bool bWaitForDriverTick = true;
    float CheckSeconds = 0.0f;
    struct FPhysicsHand
    {
        FVector Previous = FVector::ZeroVector, Current = FVector::ZeroVector;
        float Delta = 0.0f;
        bool bReady = false, bPending = false;
    };
    FPhysicsHand Hands[2];
    int32 HandPushCounts[2] = {};
    double NextHandLog[2] = {};
    void ApplyHandPressure();
};
