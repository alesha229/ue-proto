#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaSoftBodyVerification.generated.h"

class AGratiaPreviewCharacter;
class USkeletalMeshComponent;
class UGratiaHandAnimInstance;
class ACameraActor;

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
    enum class EPhase : uint8 { Settle, Baseline, Approach, Press, Squeeze, SideSqueeze, Cup, Arm, Grab, Pull, Release, Lost, NextZone, Conform, BodyGrip, Disabled, Resumed, Tilt, Upright };
    bool Check(bool bPass, const FString& Description);
    void Advance(EPhase Next) { Phase = Next; PhaseSeconds = 0; }
    void Finish();
    void Submit(const FVector& Hand, float Delta, float Trigger, bool bAllowed = true, FName SqueezeBone = NAME_None, float Squeeze = 0.0f);
    FVector CurrentTip() const;
    /** Zone tip in its parent bone frame (cm): independent of idle body motion. */
    TArray<FVector> TipsInParentFrame() const;
    /** Desktop capture of the pressed zone with the surface dent on/off (pixel diff in the test script). */
    void AimCamera(const FVector& Center, const FVector& OutwardDir, bool bFrontal = false);
    void Shoot(const FString& Name);

    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> TestHand;
    UPROPERTY() TObjectPtr<ACameraActor> ShotCamera;
    bool bShotOn = false, bShotOff = false;
    TArray<FName> GripParts;
    /** 0: wrapping grip of a limb/torso; > 0: cupping a soft part with this trigger amount. */
    TArray<float> GripSqueeze;
    TArray<double> CupSinks;
    int32 GripIndex = 0;
    FVector GripAxis = FVector::ZeroVector;
    EPhase Phase = EPhase::Settle;
    float Elapsed = 0, PhaseSeconds = 0;
    int32 ZoneIndex = 0;
    bool bRequested = false, bFinished = false, bFailed = false;
    bool bSavedContactTick = false, bSavedDemo = false;
    FVector BaselineTip = FVector::ZeroVector, Outward = FVector::ZeroVector, Start = FVector::ZeroVector, Pressed = FVector::ZeroVector;
    /** Palm-skin gap when squash/vibration first appear; squash scale of the front press. */
    double OnsetGapCm = 100.0;
    FVector FrontScale = FVector::OneVector;
    float CupHalf = -1.0f;
    bool bLimbZone = false;
    int32 BaselineSamples = 0;
    double PressTipCm = 0, PullTipCm = 0, ReturnTipCm = 0;
    TArray<FVector2D> DepthAmplitude;
    TArray<FVector> RestTipsLocal;
    FRotator SavedRotation = FRotator::ZeroRotator;
    bool bSawGrab = false;
    int32 ConformedFingers = 0;
};
