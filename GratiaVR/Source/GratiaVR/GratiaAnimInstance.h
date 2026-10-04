#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GratiaAnimInstance.generated.h"

class AGratiaPreviewCharacter;
class UGratiaCharacterProfile;

/** Copy on Event Blueprint Update Animation; do not dereference gameplay actors on worker threads. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaAnimationSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    TObjectPtr<UGratiaCharacterProfile> Profile;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    FVector LookTarget = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    float ReactionWeight = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    float ReactionImpulse = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    float TargetHeadYaw = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    float TargetHeadPitch = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    int32 ReactionSerial = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    int32 Mood = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    int32 Quality = 1;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    bool bIdle = true;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    bool bGaze = false;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    bool bBodyMotion = false;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    bool bHairMotion = false;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    bool bEarMotion = false;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    bool bClothMotion = false;
    UPROPERTY(BlueprintReadOnly, Category = "Character Animation")
    bool bLocalSpring = false;
};

/** Game-thread snapshot source for a manually authored normal Animation Blueprint. */
UCLASS()
class GRATIAVR_API UGratiaAnimationProfileLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category = "Character|Animation")
    static FGratiaAnimationSnapshot GetCharacterAnimationSnapshot(AGratiaPreviewCharacter* Character);
};

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
    bool IsReactionCuePlaying() const { return ReactionClip && ReactionTime >= 0.0f && ReactionTime < ReactionClipDuration; }
    float ReactionClipDuration = 0.0f;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
