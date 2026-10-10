#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaBodyMotionMath.h"
#include "GratiaBodyMotionTypes.h"
#include "GratiaBodyMotion.generated.h"

class AGratiaPreviewCharacter;
class AGratiaStage1Runtime;
class UAnimSequence;
class UGratiaCharacterProfile;
class UMotionControllerComponent;
class USkeletalMesh;

/**
 * Procedural body layer of a character (settings: CharacterProfile.BodyMotion):
 * breathing tied to Excitement/Stamina, Perlin micro-sway of pelvis/spine/head with planted feet,
 * playhead modulation of the idle/stance mocap by the caress speed, additive arch and shoulder
 * tension on contact, dodge from an approaching controller and lean into a touch, mocap fragments
 * chosen by pose distance and the player's position, and archetype behaviour (Mood index).
 *
 * Excitement and Stamina here are the character's shared state: face, sound and gameplay read
 * them (Find) and other contact sources add to them (AddExcitement).
 * Runs on the game thread before the mesh; the animation proxy applies the result.
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaBodyMotion : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaBodyMotion();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** The body layer of a character actor, or null. */
    UFUNCTION(BlueprintPure, Category = "Body Motion")
    static UGratiaBodyMotion* Find(const AActor* Actor);
    /** Adds the layer at runtime when the character has none. */
    static UGratiaBodyMotion* Ensure(AGratiaPreviewCharacter* InCharacter);

    /** Another stimulation source (penetration, toys, gameplay scale); 0..1 per call, saturating. */
    UFUNCTION(BlueprintCallable, Category = "Body Motion")
    void AddExcitement(float Amount);
    /** Calm, rested, no fragment; used on scene changes and profile replacement. */
    UFUNCTION(BlueprintCallable, Category = "Body Motion")
    void ResetState();
    UFUNCTION(BlueprintPure, Category = "Body Motion|Diagnostics")
    FString GetDiagnostics() const;
    /** Archetype label for menus (from the profile; Mood index). */
    FText GetArchetypeLabel(int32 Mood) const;

    // Shared character state.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|State")
    float Excitement = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|State")
    float Stamina = 1.0f;
    /** Breath cycle position 0..1 (inhale in the first 40%). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|State")
    float BreathPhase = 0.0f;
    /** Current chest expansion 0..1 (curve x depth): drive breath sounds and morphs with it. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|State")
    float BreathValue = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|State")
    float BreathsPerMinute = 14.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|State")
    float PlayRate = 1.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|State")
    float Tension = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|State")
    bool bContact = false;

    // Archetype state and hints for the face/hand layers.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|Archetype")
    int32 Archetype = 0;
    /** Tsundere melting 0..1 (1 when the archetype does not thaw). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|Archetype")
    float Thaw = 1.0f;
    /** Kuudere: the cold front broke under strong stimulation. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|Archetype")
    bool bComposureBroken = false;
    /** 1 - response: how cold/indifferent the character acts now. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|Archetype")
    float Coldness = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|Archetype")
    float GazeAversion = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|Archetype")
    float Pout = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|Archetype")
    float Smile = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|Archetype")
    float PushAway = 0.0f;

    /** Current fragment (clip@start) or empty. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|Fragments")
    FString CurrentFragment;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body Motion|Fragments")
    int32 FragmentCount = 0;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    struct FRigBone
    {
        FName Name;
        FQuat RestCS = FQuat::Identity;
        /** Accumulated component-space rotation this frame. */
        FQuat Delta = FQuat::Identity;
        bool bPre = false;
    };
    struct FFragment
    {
        UAnimSequence* Clip = nullptr;
        float Start = 0.0f, End = 0.0f, Weight = 1.0f;
        EGratiaFragmentKind Kind = EGratiaFragmentKind::Any;
        TArray<FVector> Entry, Exit;
        double LastUsed = -1.0e9;
    };
    struct FActiveFragment
    {
        int32 Index = INDEX_NONE;
        float Time = 0.0f, Weight = 0.0f;
        bool bFadingOut = false;
    };
    struct FHandSense
    {
        bool bValid = false;
        float DistanceCm = 1.0e6f, ClosingSpeed = 0.0f, Speed = 0.0f;
        /** Unit direction from the nearest body point to the hand, component space. */
        FVector DirectionCS = FVector::ZeroVector;
    };

    void Rebuild(const UGratiaCharacterProfile& Profile, const USkeletalMesh& Mesh);
    void BuildFragments(const UGratiaCharacterProfile& Profile, const USkeletalMesh& Mesh);
    void AddFragmentsFromClip(UAnimSequence* Clip, float Start, float End, EGratiaFragmentKind Kind, float Weight, bool bCut, const FGratiaBodyMotionSettings& S);
    bool SampleFeatures(const UAnimSequence* Clip, float Time, TArray<FVector>& Out) const;
    void CurrentFeatures(TArray<FVector>& Out) const;
    FHandSense SenseHands(float DeltaSeconds);
    void UpdateFragments(const FGratiaBodyMotionSettings& S, float DeltaSeconds, bool bAllowStart, const FVector& PlayerCS);
    int32 PickFragment(const TArray<FVector>& Pose, float MaxError, const FVector& PlayerCS, int32 Exclude);
    void ApplyPlayRate(bool bModulate);
    void Tilt(int32 Bone, const FVector& DirectionCS, float Degrees);
    void Twist(int32 Bone, float Degrees);
    void Publish(bool bActive);
    const FGratiaArchetypeTuning& GetTuning(const FGratiaBodyMotionSettings& S) const;

    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<const UGratiaCharacterProfile> BuiltProfile;
    TWeakObjectPtr<const USkeletalMesh> BuiltMesh;
    TWeakObjectPtr<AGratiaStage1Runtime> Runtime;

    // Rig (indices into Rig).
    TArray<FRigBone> Rig;
    int32 PelvisBone = INDEX_NONE, NeckBone = INDEX_NONE, HeadBone = INDEX_NONE;
    TArray<int32> Spine;
    TArray<int32> Clavicles;
    TArray<FVector> ClavicleDirections;
    TArray<int32> LegCompensation;
    TArray<FName> FeatureBones;
    FString MissingBones;
    FVector Up = FVector::UpVector, Forward = FVector::RightVector, Right = FVector::ForwardVector;
    FName ChestName, PelvisName, HeadName;

    TArray<FFragment> Fragments;
    FActiveFragment Active[2];
    float FragmentGap = 6.0f;
    FRandomStream Random;

    GratiaBodyMotionMath::FBreathState Breath;
    GratiaBodyMotionMath::FSpring TensionSpring, LeanForward, LeanRight, TurnSpring;
    float Seconds = 0.0f;
    float PendingExcitement = 0.0f;
    float ContactSpeed = 0.0f;
    bool bRateApplied = false;
    bool bQADisabled = false;
    bool bPerformanceMode = false;
    TMap<TWeakObjectPtr<UMotionControllerComponent>, FVector> LastHand;
    FGratiaBodyMotionFrame Frame;
};
