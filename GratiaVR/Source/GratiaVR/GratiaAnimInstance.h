#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "GratiaAnimInstance.generated.h"

/** Single-node playback with bounded procedural gaze/reaction and local spring animation. */
UCLASS(Transient)
class GRATIAVR_API UGratiaAnimInstance : public UAnimSingleNodeInstance
{
    GENERATED_BODY()
public:
    UGratiaAnimInstance(const FObjectInitializer& Initializer);
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    float HeadYaw = 0.0f;
    float HeadPitch = 0.0f;
    float HeadVelocity = 0.0f;
    float Reaction = 0.0f;
    float Impulse = 0.0f;
    bool bBody = true, bHair = true, bEars = true, bCloth = true, bSpring = true, bProcedural = true;
    UAnimSequence* ReactionClip = nullptr;
    float ReactionTime = 2.0f;
    uint32 LastReactionSerial = 0;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
