#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaFaceMath.h"
#include "GratiaProceduralFace.generated.h"

class AGratiaPreviewCharacter;
class UGratiaCharacterProfile;
class USkeletalMesh;

/** Short emotional states the face snaps into; other systems (archetypes, reactions) can trigger them. */
UENUM(BlueprintType)
enum class EGratiaFaceEmotion : uint8
{
    Neutral,
    /** Brows fly up, eyes widen, mouth "o", head stretches. */
    Surprise,
    /** Blush, worried brows, squeezed eyes, bitten lip, head squash. */
    Embarrassed,
    /** Puffed cheeks, angry brows, look away (capricious). */
    Pout,
    /** Smile with squinting eyes. */
    Happy,
    /** Peak stage: half-lidded eyes, open mouth, trembling, tears, heart pupils. */
    Bliss
};

/**
 * Procedural face layer on top of the body animation (idle or performance):
 * - eyes: saccades over the viewer's eyes and mouth, gaze aversion and shy return; lids: variable blinks,
 *   reflex squint on a fast hand near the face; eyes and lids stay procedural during performances
 *   (the clip keeps the lower face);
 * - neck: head, neck and upper chest follow the gaze on underdamped springs after the eyes (idle only);
 * - elastic expressions: the facial reaction morphs written by UGratiaInteraction snap in 2-3 frames,
 *   overshoot and settle; jaw and cheeks lag head acceleration (jelly);
 * - squash and stretch of the head, asymmetric mouth in 3/4 view, micro-jitter and trembling;
 * - skin and eye shader parameters (blush, sweat, goosebumps, tears, pupil size, heart pupils).
 * Ticks after UGratiaInteraction (TG_PostUpdateWork) and overrides the morphs it owns; bone deltas
 * reach UGratiaAnimInstance (FaceBoneDeltas) for the next evaluation. Settings: profile Face.
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaProceduralFace : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaProceduralFace();
    virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Snaps into an emotion for Seconds (0 = until another one), Strength 0..1. */
    UFUNCTION(BlueprintCallable, Category = "Face")
    void TriggerEmotion(EGratiaFaceEmotion Emotion, float Strength = 1.0f, float Seconds = 2.0f);
    /** Reflex squint now (e.g. a sudden movement near the face from another system). */
    UFUNCTION(BlueprintCallable, Category = "Face")
    void TriggerReflexSquint(float Strength = 1.0f);
    /** Looks away (sideways/down) and comes back shyly. */
    UFUNCTION(BlueprintCallable, Category = "Face")
    void TriggerGazeAversion();
    /** Clears springs, timers and the morphs the face owns (pose change, profile change, reset). */
    UFUNCTION(BlueprintCallable, Category = "Face")
    void ResetFace();
    UFUNCTION(BlueprintPure, Category = "Face|Diagnostics")
    FString GetFaceDiagnostics() const;

    /** Excitement 0..1 used when no body-motion component provides one; also the lower bound. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|State", meta = (ClampMin = "0", ClampMax = "1"))
    float ExternalExcitement = 0.0f;
    /** Scene light level 0..1 for the pupils; negative = estimate from nearby lights every half second. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|State")
    float LightLevelOverride = -1.0f;
    /** Extra shader amounts added by other systems (e.g. rain makes the face wet; a scene forces a blush). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|State", meta = (ClampMin = "0", ClampMax = "1"))
    float ExtraBlush = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|State", meta = (ClampMin = "0", ClampMax = "1"))
    float ExtraTears = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face|State")
    float Excitement = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face|State")
    float Embarrassment = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face|State")
    float Blush = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face|State")
    float Sweat = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face|State")
    float Goosebumps = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face|State")
    float Tears = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face|State")
    float PupilScale = 1.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face|State")
    float HeartPupils = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face|State")
    EGratiaFaceEmotion Emotion = EGratiaFaceEmotion::Neutral;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face|State")
    bool bAverting = false;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    struct FSegment
    {
        FName Bone;
        FVector YawAxis = FVector::UpVector, NodAxis = FVector::ForwardVector;
        GratiaFaceMath::FSpring Yaw, Pitch;
        bool bValid = false;
    };
    struct FMorphChannel
    {
        FName Semantic;
        FName Morph;
        uint8 Kind = 0;
        GratiaFaceMath::FSnapSettle Spring;
        /** Weight this layer wrote last frame (-1: none) and the last weight UGratiaInteraction wrote. */
        float LastWritten = -1.0f;
        float InteractionTarget = 0.0f;
        bool bWritten = false;
    };

    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<const UGratiaCharacterProfile> BoundProfile;
    TWeakObjectPtr<USkeletalMesh> BoundMesh;
    FRandomStream Random;
    float Time = 0.0f;
    uint8 LastPose = 255;
    /** Semantic -> morph present on the mesh. */
    TMap<FName, FName> Resolved;
    TArray<FString> Missing;

    // Head frame in the head bone's local space (from the reference pose); head, neck, upper chest springs.
    FVector HeadForwardLocal = FVector::ForwardVector, HeadUpLocal = FVector::UpVector;
    int32 HeadVerticalAxis = 2;
    FSegment Segments[3];
    float Shares[3] = { 1.0f, 0.0f, 0.0f };
    float HeadGoalYaw = 0.0f, HeadGoalPitch = 0.0f;
    FQuat LastHeadQuat = FQuat::Identity;
    float LastYawRate = 0.0f, LastPitchRate = 0.0f;
    bool bHasLastHead = false;
    FVector LastEyeCenter = FVector::ZeroVector;
    FVector LastHeadRight = FVector::RightVector;

    // Eyes and aversion.
    GratiaFaceMath::EFixation Fixation = GratiaFaceMath::EFixation::LeftEye;
    float UntilSaccade = 0.4f;
    FVector MicroOffset = FVector::ZeroVector;
    /** Flustered darting: level 0..1, the side of the current dart and its pitch. */
    float Fluster = 0.0f, DartSide = 1.0f, DartPitch = 0.0f;
    GratiaFaceMath::FSpring EyeYaw, EyePitch;
    float LastEyeTargetYaw = 0.0f, LastEyeTargetPitch = 0.0f;
    float LastViewDistance = 0.0f;
    bool bHasLastView = false;
    float AversionLeft = 0.0f, ShyLeft = 0.0f, AversionCooldown = 0.0f, AversionSide = 1.0f;

    // Lids.
    float UntilBlink = 2.0f, BlinkElapsed = -1.0f, BlinkClose = 0.07f, BlinkHold = 0.03f, BlinkOpen = 0.15f;
    bool bDoubleBlinkPending = false;
    float ReflexLeft = 0.0f, ReflexCooldown = 0.0f, ReflexStrength = 0.0f;
    FVector LastHand[2] = { FVector::ZeroVector, FVector::ZeroVector };
    bool bHasLastHand[2] = { false, false };
    GratiaFaceMath::FSnapSettle Squeeze;

    // Expression, squash, mouth asymmetry.
    TArray<FMorphChannel> Channels;
    GratiaFaceMath::FSpring JawJiggle, CheekJiggle, SideJiggle;
    GratiaFaceMath::FSnapSettle Squash;
    EGratiaFaceEmotion PendingEmotion = EGratiaFaceEmotion::Neutral;
    float EmotionStrength = 0.0f, EmotionLeft = 0.0f;
    int32 LastReactionSerial = 0;
    float MouthShiftSign = 1.0f, MouthAsymmetryWeight = 0.0f;

    // Shaders.
    GratiaFaceMath::FSpring BlushSpring, TearSpring, PupilSpring, HeartSpring;
    float LightLevel = 0.5f, UntilLightProbe = 0.0f;
    TMap<FName, float> LastScalars;
    FVector LastHeadRightParam = FVector::ZeroVector;

    /** Morphs this layer overrides; released (override removed) when a feature stops driving them. */
    TSet<FName> OwnedMorphs;

    void Bind();
    FName Morph(FName Semantic) const;
    void SetMorph(FName Semantic, float Weight);
    void ReleaseMorph(FName Semantic);
    void ReleaseAll();
    void SetScalar(FName Name, float Value);
    float ReadBodyMotion(FName Property, float Fallback) const;
    void UpdateLightLevel(float DeltaSeconds);
    void UpdateEyesAndNeck(float Dt, bool bIdle, const FVector& ViewLocation, const FQuat& ViewRotation, bool bHasView,
        float& OutEyeYaw, float& OutEyePitch, float& OutViewYawAroundHead);
    float UpdateLids(float Dt, float LookPitch);
    void UpdateExpression(float Dt, bool bExpressive, float ViewYawAroundHead, float Squint);
    void UpdateShaders(float Dt, float ViewYawAroundHead);
    void PushBoneDeltas(bool bIdle, bool bEnabled);
};
