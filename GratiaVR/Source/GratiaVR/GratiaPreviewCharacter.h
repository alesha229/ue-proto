#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GratiaPreviewCharacter.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
class UGratiaInteraction;
class UGratiaSecondaryMotion;

UENUM(BlueprintType)
enum class EGratiaPreviewPose : uint8
{
    Idle,
    Arms,
    Head
};

/** A cooked character preview using the existing Gratia export skeleton. */
UCLASS()
class GRATIAVR_API AGratiaPreviewCharacter : public AActor
{
    GENERATED_BODY()

public:
    AGratiaPreviewCharacter();
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gratia")
    TObjectPtr<USkeletalMeshComponent> CharacterMesh;

    UPROPERTY(VisibleAnywhere, Category = "Gratia")
    TObjectPtr<UGratiaInteraction> Interaction;

    UPROPERTY(VisibleAnywhere, Category = "Gratia|Physics")
    TObjectPtr<UGratiaSecondaryMotion> SecondaryMotion;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gratia|Preview")
    EGratiaPreviewPose PreviewPose = EGratiaPreviewPose::Idle;

    UFUNCTION(BlueprintCallable, Category = "Gratia|Preview")
    void SetPreviewPose(EGratiaPreviewPose Pose);

    UFUNCTION(BlueprintCallable, Category = "Gratia|Preview")
    void CyclePreviewPose();

    /** Restores the beginning of the neutral idle and clears preview facial overrides. */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Preview")
    void ResetToIdle();

    UAnimSequence* GetExpectedAnimation() const;
    UAnimSequence* GetPreviewAnimation(EGratiaPreviewPose Pose) const;
    UAnimSequence* GetReactionAnimation(bool bBright) const { return bBright ? BrightReaction.Get() : SoftReaction.Get(); }
    bool IsIdlePreview() const { return PreviewPose == EGratiaPreviewPose::Idle; }

protected:
    virtual void BeginPlay() override;

private:
    // These references belong to the class default object, so the cooker retains all clips.
    UPROPERTY()
    TObjectPtr<UAnimSequence> IdleAnimation;

    UPROPERTY()
    TObjectPtr<UAnimSequence> ArmsAnimation;

    UPROPERTY()
    TObjectPtr<UAnimSequence> HeadAnimation;

    UPROPERTY()
    TObjectPtr<UAnimSequence> SoftReaction;

    UPROPERTY()
    TObjectPtr<UAnimSequence> BrightReaction;

    float UntilNextBlink = 2.2f;
    float BlinkElapsed = -1.0f;

    void UpdateBlink(float DeltaSeconds);
};
