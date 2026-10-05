#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GratiaPreviewCharacter.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
class UGratiaInteraction;
class UGratiaSecondaryMotion;
class UGratiaCharacterProfile;
class UGratiaReactionPresentation;
class UGratiaSoftBodyInteraction;
class UGratiaSoftBodyVerification;

UENUM(BlueprintType)
enum class EGratiaPreviewPose : uint8
{
    Idle,
    Arms,
    Head
};

/** Profile-driven character host; class name retained for existing placed assets. */
UCLASS()
class GRATIAVR_API AGratiaPreviewCharacter : public AActor
{
    GENERATED_BODY()

public:
    AGratiaPreviewCharacter();
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
    TObjectPtr<UGratiaCharacterProfile> CharacterProfile;

    UFUNCTION(BlueprintCallable, Category = "Character")
    bool SetCharacterProfile(UGratiaCharacterProfile* Profile);

    UFUNCTION(BlueprintPure, Category = "Character|Animation")
    FVector GetLookTarget() const;
    UFUNCTION(BlueprintPure, Category = "Character|Animation")
    float GetReactionWeight() const;
    UFUNCTION(BlueprintPure, Category = "Character|Animation")
    int32 GetReactionSerial() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gratia")
    TObjectPtr<USkeletalMeshComponent> CharacterMesh;

    UPROPERTY(VisibleAnywhere, Category = "Gratia")
    TObjectPtr<UGratiaInteraction> Interaction;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Presentation")
    TObjectPtr<UGratiaReactionPresentation> ReactionPresentation;

    UPROPERTY(VisibleAnywhere, Category = "Gratia|Physics")
    TObjectPtr<UGratiaSecondaryMotion> SecondaryMotion;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gratia|Physics")
    TObjectPtr<UGratiaSoftBodyInteraction> SoftBodyInteraction;
    UPROPERTY(VisibleAnywhere, Category = "Verification")
    TObjectPtr<UGratiaSoftBodyVerification> SoftBodyVerification;

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
    UAnimSequence* GetReactionAnimationForZone(FName ZoneName) const;
    bool IsIdlePreview() const { return PreviewPose == EGratiaPreviewPose::Idle; }

protected:
    virtual void BeginPlay() override;

private:
#if WITH_EDITOR
    void RefreshEditorProfilePreview();
#endif
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
