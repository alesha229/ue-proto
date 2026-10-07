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
class UGratiaBodySurface;
class UGratiaPenetration;
class UGratiaPerformanceStage;
struct FGratiaPerformanceClip;

UENUM(BlueprintType)
enum class EGratiaPreviewPose : uint8
{
    Idle,
    Arms,
    Head,
    /** CharacterProfile.PerformanceClips[PerformanceIndex], played from the start. */
    Performance
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
    /** Touchable body surface for hands: palm collider, leaning onto the body, wrapping grips. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gratia|Physics")
    TObjectPtr<UGratiaBodySurface> BodySurface;
    /** Body channels for the jointed primitive (AGratiaPenetrator): capture, wall bones, reactions. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gratia|Physics")
    TObjectPtr<UGratiaPenetration> Penetration;
    /** Music, partner body and partner viewpoint of the current performance. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Performance")
    TObjectPtr<UGratiaPerformanceStage> PerformanceStage;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gratia|Preview")
    EGratiaPreviewPose PreviewPose = EGratiaPreviewPose::Idle;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gratia|Preview")
    int32 PerformanceIndex = 0;
    /** Current part (0 = Clip, then Segments) of a segmented performance. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gratia|Preview")
    int32 PerformancePart = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gratia|Preview")
    bool bPerformancePaused = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gratia|Preview")
    float PerformanceRate = 1.0f;

    /** Continues a segmented performance with its next part when the current one ends. */
    void UpdatePerformance();

    /** Profile performance index by name; INDEX_NONE when the profile has none of that name. */
    int32 FindPerformance(FName Name) const;
    /** Pauses/resumes the performance and sets its speed (music follows, pitched by the speed). */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Preview")
    void SetPerformancePlayback(bool bPaused, float Rate);
    bool IsPerformancePaused() const { return bPerformancePaused; }
    float GetPerformanceRate() const { return PerformanceRate; }
    /** Jumps to the start of a part of the current performance (clamped). */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Preview")
    bool SeekPerformancePart(int32 Part);
    int32 GetPerformancePartCount() const;

    UFUNCTION(BlueprintCallable, Category = "Gratia|Preview")
    void SetPreviewPose(EGratiaPreviewPose Pose);

    /** Plays CharacterProfile.PerformanceClips[Index]; false (pose unchanged) when there is no such clip. */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Preview")
    bool SetPerformance(int32 Index);

    UFUNCTION(BlueprintPure, Category = "Gratia|Preview")
    FString GetPreviewPoseLabel() const;

    UFUNCTION(BlueprintCallable, Category = "Gratia|Preview")
    void CyclePreviewPose();

    /** Restores the beginning of the neutral idle and clears preview facial overrides. */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Preview")
    void ResetToIdle();

    UAnimSequence* GetExpectedAnimation() const;
    UAnimSequence* GetPreviewAnimation(EGratiaPreviewPose Pose) const;
    UAnimSequence* GetReactionAnimation(bool bBright) const { return bBright ? BrightReaction.Get() : SoftReaction.Get(); }
    UAnimSequence* GetReactionAnimationForZone(FName ZoneName) const;
    /** Fast touch -> StrongReactionClip; else the current mood's clip; else zone routing. */
    UAnimSequence* GetReactionAnimation(FName ZoneName, float HandSpeed, int32 Mood) const;
    static FName MoodName(int32 Mood);
    bool IsIdlePreview() const { return PreviewPose == EGratiaPreviewPose::Idle; }
    /** Continuously playing body animation (idle or a performance), as opposed to a held diagnostic pose. */
    bool IsAnimatedPreview() const { return PreviewPose == EGratiaPreviewPose::Idle || PreviewPose == EGratiaPreviewPose::Performance; }
    const FGratiaPerformanceClip* GetPerformance() const;

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
