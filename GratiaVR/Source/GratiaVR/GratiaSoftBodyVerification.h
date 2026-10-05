#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaSoftBodyVerification.generated.h"

class AGratiaPreviewCharacter;
class USkeletalMeshComponent;
class UGratiaHandAnimInstance;

/**
 * Opt-in desktop QA (-GratiaSoftBodyQA) of KawaiiPhysics soft parts with synthetic hands:
 * press, depth-proportional haptics, spring grab, release, tracking loss, finger conform
 * of a real XR hand mesh, and the body-motion toggle. Not a VR hardware acceptance.
 */
UCLASS()
class GRATIAVR_API UGratiaSoftBodyVerification : public UActorComponent
{
    GENERATED_BODY()

public:
    UGratiaSoftBodyVerification();
    bool OwnsSyntheticContactSamples() const { return bRequested && !bFinished; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

private:
    enum class EPhase : uint8 { Settle, Baseline, Approach, Press, Arm, Grab, Pull, Release, Lost, NextZone, Conform, Disabled, Resumed };
    bool Check(bool bPass, const FString& Description);
    void Advance(EPhase Next) { Phase = Next; PhaseSeconds = 0; }
    void Finish();
    void Submit(const FVector& Hand, float Delta, float Trigger, bool bAllowed = true);
    FVector CurrentTip() const;

    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> TestHand;
    EPhase Phase = EPhase::Settle;
    float Elapsed = 0, PhaseSeconds = 0;
    int32 ZoneIndex = 0;
    bool bRequested = false, bFinished = false, bFailed = false;
    bool bSavedContactTick = false, bSavedDemo = false;
    FVector BaselineTip = FVector::ZeroVector, Outward = FVector::ZeroVector, Start = FVector::ZeroVector, Pressed = FVector::ZeroVector;
    int32 BaselineSamples = 0;
    double PressTipCm = 0, PullTipCm = 0, ReturnTipCm = 0;
    TArray<FVector2D> DepthAmplitude;
    bool bSawGrab = false;
    int32 ConformedFingers = 0;
};
