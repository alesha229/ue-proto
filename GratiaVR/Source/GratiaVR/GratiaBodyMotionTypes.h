#pragma once
#include "CoreMinimal.h"
#include "GratiaBodyMotionTypes.generated.h"

class UAnimSequence;
struct FPoseContext;

/** What a mocap fragment does; the selector prefers kinds that suit the player's position. */
UENUM(BlueprintType)
enum class EGratiaFragmentKind : uint8
{
    Any,
    /** Arms above the head, a stretch. */
    Stretch,
    /** Upper body leans (forward/back) relative to the pelvis. */
    Lean,
    /** Pelvis moves sideways: a change of the supporting leg. */
    WeightShift
};

/** A short piece of a mocap clip that can be played on top of the idle and chained with others. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaMotionFragment
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fragment")
    TObjectPtr<UAnimSequence> Clip;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fragment", meta = (ClampMin = "0", Units = "s"))
    float StartSeconds = 0.0f;
    /** Zero or less: to the end of the clip. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fragment", meta = (Units = "s"))
    float EndSeconds = 0.0f;
    /** Any: classified automatically from the motion. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fragment")
    EGratiaFragmentKind Kind = EGratiaFragmentKind::Any;
    /** Relative chance among equally good fragments. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fragment", meta = (ClampMin = "0"))
    float Weight = 1.0f;
};

/**
 * Behaviour of one character archetype. The archetype is the existing Mood index of the
 * interaction (0, 1, 2); entries are matched by index, missing entries use neutral values.
 */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaArchetypeTuning
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype")
    FText Label;
    /** Excitement gained per second of well-paced contact. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "2"))
    float ExcitementGain = 0.12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "2"))
    float ExcitementDecay = 0.04f;
    /** Scales arch/shoulder tension on contact. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "3"))
    float TensionScale = 1.0f;
    /** Scales leaning away from an approaching hand. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "3"))
    float DodgeScale = 1.0f;
    /** Scales leaning into a touch. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "3"))
    float LeanTowardScale = 1.0f;
    /** Leans toward a nearby player without being touched (initiative). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "15", Units = "deg"))
    float InitiativeLeanDegrees = 0.0f;
    /** Upper body turns away from the player until thawed (degrees at Thaw 0). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "30", Units = "deg"))
    float TurnAwayDegrees = 0.0f;
    /** Scales idle sway and breathing visibility. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "3"))
    float LivelinessScale = 1.0f;
    /** Thaw gained per second of gentle contact (tsundere melting); zero means always thawed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "1"))
    float ThawPerSecond = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "1"))
    float ThawDecayPerSecond = 0.01f;
    /** Below this excitement responses are muted to MutedResponse (kuudere); 0 disables. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "1"))
    float BreakThreshold = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0", ClampMax = "1"))
    float MutedResponse = 1.0f;
    /** Face/gaze hints for the face layer (0..1 at Thaw 0 / unbroken). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Face", meta = (ClampMin = "0", ClampMax = "1"))
    float GazeAversion = 0.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Face", meta = (ClampMin = "0", ClampMax = "1"))
    float Pout = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Face", meta = (ClampMin = "0", ClampMax = "1"))
    float Smile = 0.3f;
    /** Hands push the player's hands away until thawed (hint for the hand/IK layer). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Face", meta = (ClampMin = "0", ClampMax = "1"))
    float PushAway = 0.0f;
};

/** Procedural body layer of a character: breathing, idle micro-noise, playhead modulation,
 *  additive tension, dodge/lean, mocap fragments and archetype behaviour. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaBodyMotionSettings
{
    GENERATED_BODY()
    FGratiaBodyMotionSettings();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion")
    bool bEnabled = true;

    // Breathing: clavicles, ribs (chest), neck and pelvis move together.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Breathing", meta = (ClampMin = "4", ClampMax = "40"))
    float RestBreathsPerMinute = 14.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Breathing", meta = (ClampMin = "4", ClampMax = "80"))
    float ExcitedBreathsPerMinute = 38.0f;
    /** Extra breathing rate when out of stamina (fraction of the rate). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Breathing", meta = (ClampMin = "0", ClampMax = "2"))
    float FatigueBreathBoost = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Breathing", meta = (ClampMin = "0", ClampMax = "1"))
    float RestBreathDepth = 0.35f;
    /** Chest extension at a full breath, spread over the spine bones above the pelvis. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Breathing", meta = (ClampMin = "0", ClampMax = "10", Units = "deg"))
    float BreathChestDegrees = 1.8f;
    /** Shoulders rise at a full breath. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Breathing", meta = (ClampMin = "0", ClampMax = "10", Units = "deg"))
    float BreathClavicleDegrees = 2.2f;
    /** Pelvis tilts forward (belly) at a full breath; the legs are compensated. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Breathing", meta = (ClampMin = "0", ClampMax = "5", Units = "deg"))
    float BreathPelvisDegrees = 0.4f;
    /** Fraction of the chest motion the neck cancels, so the head (and gaze) stays steady. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Breathing", meta = (ClampMin = "0", ClampMax = "1"))
    float BreathNeckCompensation = 0.8f;
    /** Breathing in performances (their mocap already moves the body). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Breathing", meta = (ClampMin = "0", ClampMax = "1"))
    float PerformanceBreathScale = 0.5f;

    // Perlin micro-noise: slow sway of pelvis, spine and head; the feet stay planted.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Noise", meta = (ClampMin = "0.01", ClampMax = "2", Units = "Hz"))
    float NoiseFrequencyHz = 0.22f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Noise", meta = (ClampMin = "0", ClampMax = "5", Units = "deg"))
    float PelvisNoiseDegrees = 0.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Noise", meta = (ClampMin = "0", ClampMax = "5", Units = "deg"))
    float SpineNoiseDegrees = 0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Noise", meta = (ClampMin = "0", ClampMax = "8", Units = "deg"))
    float HeadNoiseDegrees = 1.4f;
    /** Noise grows with excitement by this factor (restlessness). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Noise", meta = (ClampMin = "0", ClampMax = "3"))
    float ExcitedNoiseBoost = 0.6f;

    // Playhead modulation of the idle/stance mocap.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Playhead")
    bool bModulatePlayhead = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Playhead", meta = (ClampMin = "0.1", ClampMax = "1"))
    float MinPlayRate = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Playhead", meta = (ClampMin = "1", ClampMax = "3"))
    float MaxPlayRate = 1.8f;
    /** Hand speed (cm/s) of a caress that plays at MinPlayRate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Playhead", meta = (ClampMin = "1", ClampMax = "100"))
    float SlowHandSpeed = 8.0f;
    /** Hand speed (cm/s) that plays at MaxPlayRate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Playhead", meta = (ClampMin = "10", ClampMax = "500"))
    float FastHandSpeed = 80.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Playhead", meta = (ClampMin = "0.05", ClampMax = "5", Units = "s"))
    float PlayRateSmoothingSeconds = 0.6f;

    // Additive tension on contact: lumbar arch, shoulder blades together.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Tension", meta = (ClampMin = "0", ClampMax = "20", Units = "deg"))
    float ArchDegrees = 7.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Tension", meta = (ClampMin = "0", ClampMax = "20", Units = "deg"))
    float ShoulderRetractDegrees = 6.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Tension", meta = (ClampMin = "0.1", ClampMax = "10", Units = "Hz"))
    float TensionFrequencyHz = 1.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Tension", meta = (ClampMin = "0.1", ClampMax = "2"))
    float TensionDampingRatio = 0.65f;

    // Dodge / lean: away from an approaching controller, into a touch.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Lean", meta = (ClampMin = "0", ClampMax = "20", Units = "deg"))
    float DodgeDegrees = 6.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Lean", meta = (ClampMin = "0", ClampMax = "20", Units = "deg"))
    float LeanTowardDegrees = 3.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Lean", meta = (ClampMin = "1", ClampMax = "100", Units = "cm"))
    float DodgeNearCm = 22.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Lean", meta = (ClampMin = "10", ClampMax = "200", Units = "cm"))
    float DodgeFarCm = 60.0f;
    /** Closing speed (cm/s) of the hand for a full dodge. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Lean", meta = (ClampMin = "5", ClampMax = "500"))
    float DodgeFullSpeed = 60.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Lean", meta = (ClampMin = "0.1", ClampMax = "10", Units = "Hz"))
    float LeanFrequencyHz = 1.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Lean", meta = (ClampMin = "0.1", ClampMax = "2"))
    float LeanDampingRatio = 0.8f;
    /** Fraction of the upper-body lean the neck cancels (the head keeps looking at the player). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Lean", meta = (ClampMin = "0", ClampMax = "1"))
    float LeanNeckCompensation = 0.4f;

    // Excitement and stamina (shared by face, sound and gameplay layers).
    /** Caress speed range (cm/s) that counts as the right pace. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Excitement", meta = (ClampMin = "1", ClampMax = "100"))
    float GoodPaceLow = 10.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Excitement", meta = (ClampMin = "2", ClampMax = "300"))
    float GoodPaceHigh = 45.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Excitement", meta = (ClampMin = "0", ClampMax = "1"))
    float StaminaDrainPerSecond = 0.03f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Excitement", meta = (ClampMin = "0", ClampMax = "1"))
    float StaminaRecoverPerSecond = 0.04f;

    // Motion fragments (motion matching over a small database of mocap pieces).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Fragments")
    bool bFragments = true;
    /** Explicit fragments. Empty: the idle and the stance clips are cut automatically at calm frames. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Fragments", meta = (TitleProperty = "Clip"))
    TArray<FGratiaMotionFragment> Fragments;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Fragments", meta = (ClampMin = "0.5", ClampMax = "10", Units = "s"))
    float AutoFragmentMinSeconds = 1.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Fragments", meta = (ClampMin = "1", ClampMax = "20", Units = "s"))
    float AutoFragmentMaxSeconds = 4.5f;
    /** Calm time on the idle between fragments (random in the range). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Fragments")
    FVector2D FragmentGapSeconds = FVector2D(4.0f, 10.0f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Fragments", meta = (ClampMin = "0.05", ClampMax = "2", Units = "s"))
    float FragmentBlendSeconds = 0.45f;
    /** A fragment whose first pose is further than this from the current pose (mean of pelvis,
     *  feet, head, hands) is not started: the blend would slide the feet. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Fragments", meta = (ClampMin = "0.5", ClampMax = "50", Units = "cm"))
    float MaxEntryPoseErrorCm = 7.0f;
    /** Chance to chain straight into a matching next fragment instead of returning to the idle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Fragments", meta = (ClampMin = "0", ClampMax = "1"))
    float ChainChance = 0.45f;

    /** Indexed by the interaction Mood: 0 kuudere, 1 deredere, 2 tsundere (defaults). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Motion|Archetypes", meta = (TitleProperty = "Name"))
    TArray<FGratiaArchetypeTuning> Archetypes;
};

/** One procedural bone change for the animation thread. Post: Local = Local * Rotation.
 *  Pre (child compensation): Local = Rotation * Local and the translation is rotated too. */
struct FGratiaBodyBoneDelta
{
    FName Bone;
    FQuat Rotation = FQuat::Identity;
    bool bPre = false;
};

/** Game-thread result of UGratiaBodyMotion, copied to the animation proxy. */
struct FGratiaBodyMotionFrame
{
    struct FLayer
    {
        UAnimSequence* Clip = nullptr;
        float Time = 0.0f;
        float Weight = 0.0f;
    };
    /** Up to two fragments blended over the base pose in order (the second fades in over the first). */
    FLayer Layers[2];
    /** Keep the base root bone while blending fragments (no sliding of the whole character). */
    bool bKeepRoot = true;
    TArray<FGratiaBodyBoneDelta> Bones;
};

namespace GratiaBodyMotionPose
{
    /** Blends the fragment layers over the evaluated base pose (body only; morph curves are kept). */
    GRATIAVR_API void BlendFragments(FPoseContext& Output, const FGratiaBodyMotionFrame& Frame);
    /** Applies the additive bone rotations (breathing, noise, tension, lean). */
    GRATIAVR_API void ApplyBones(FPoseContext& Output, const FGratiaBodyMotionFrame& Frame);
}
