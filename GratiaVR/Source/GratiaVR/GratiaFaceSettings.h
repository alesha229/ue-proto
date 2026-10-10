#pragma once
#include "CoreMinimal.h"
#include "GratiaFaceSettings.generated.h"

/** Procedural face of a character profile (UGratiaProceduralFace): eyes, lids, neck follow, elastic
 *  expressions, squash and stretch, asymmetric mouth, micro-jitter and skin/eye shader parameters.
 *  Morphs and bones are looked up through the profile's SemanticMorphs/SemanticBones; a missing
 *  mapping switches the dependent feature off and is reported once in the log. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaFaceSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
    bool bEnabled = true;

    // ------------------------------------------------------------------ eyes
    /** Eye centre relative to the Head bone in the profile's forward/up axes, cm (forward, up). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Eyes", meta = (Units = "cm"))
    FVector2D EyeCenterOffsetCm = FVector2D(8.0, 6.0);
    /** Eye yaw at which LookLeft/LookRight reach 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Eyes", meta = (ClampMin = "1", ClampMax = "60", Units = "deg"))
    float FullEyeYawDegrees = 30.0f;
    /** Eye pitch at which LookUp/LookDown reach 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Eyes", meta = (ClampMin = "1", ClampMax = "60", Units = "deg"))
    float FullEyePitchDegrees = 20.0f;
    /** Swap LookLeft and LookRight if the model's "left" means the viewer's left. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Eyes")
    bool bMirrorLookMorphs = false;
    /** Saccades between the viewer's eyes and mouth: seconds between jumps (2-3 per second). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Eyes", meta = (ClampMin = "0.1", Units = "s"))
    FVector2D SaccadeIntervalSeconds = FVector2D(0.33, 0.5);
    /** Share of jumps from an eye that go to the mouth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Eyes", meta = (ClampMin = "0", ClampMax = "1"))
    float MouthFixationShare = 0.25f;
    /** Viewer face geometry the saccades target, cm (eye spacing; mouth below the eyes). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Eyes", meta = (Units = "cm"))
    float ViewerEyeSpacingCm = 6.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Eyes", meta = (Units = "cm"))
    float ViewerMouthDropCm = 7.0f;
    /** Eye spring: very fast and nearly critical, a saccade lasts ~40 ms. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Eyes", meta = (ClampMin = "1", ClampMax = "40", Units = "Hz"))
    float EyeSpringHz = 14.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Eyes", meta = (ClampMin = "0.1", ClampMax = "2"))
    float EyeSpringDamping = 0.85f;
    /** Lids follow the gaze down: blink weight added at full LookDown. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Eyes", meta = (ClampMin = "0", ClampMax = "1"))
    float LidFollowDown = 0.25f;

    // ------------------------------------------------------------------ neck
    /** Head/neck/upper chest spring that follows the gaze after the eyes; slightly underdamped. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Neck")
    bool bNeckSpring = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Neck", meta = (ClampMin = "0.1", ClampMax = "10", Units = "Hz"))
    float NeckSpringHz = 1.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Neck", meta = (ClampMin = "0.1", ClampMax = "2"))
    float NeckSpringDamping = 0.6f;
    /** Turn shares over head, neck and upper chest (normalised; missing bones give their share to the head). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Neck")
    FVector GazeShares = FVector(0.55, 0.3, 0.15);
    /** Each segment lower in the chain follows slower by this factor (chest after neck after head). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Neck", meta = (ClampMin = "0.2", ClampMax = "1"))
    float SegmentLag = 0.7f;
    /** Turns smaller than this stay with the eyes only (the head does not chase every saccade). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Neck", meta = (ClampMin = "0", ClampMax = "30", Units = "deg"))
    float HeadDeadZoneDegrees = 4.0f;

    // ------------------------------------------------------------------ aversion
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Aversion")
    bool bGazeAversion = true;
    /** The viewer's face this close always makes her look away. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Aversion", meta = (Units = "cm"))
    float AversionCloseCm = 22.0f;
    /** Within this range a fast approach makes her look away. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Aversion", meta = (Units = "cm"))
    float AversionRangeCm = 55.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Aversion")
    float AversionApproachCmPerSecond = 60.0f;
    /** Eye direction while looking away: sideways and down (degrees), head turns a little the same way. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Aversion")
    FVector2D AversionEyeDegrees = FVector2D(20.0, -14.0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Aversion")
    FVector2D AversionHeadDegrees = FVector2D(10.0, -7.0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Aversion", meta = (Units = "s"))
    FVector2D AversionSeconds = FVector2D(1.2, 2.2);
    /** Shy return: short glances back before the gaze settles on the viewer again. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Aversion", meta = (Units = "s"))
    float ShyReturnSeconds = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Aversion", meta = (Units = "s"))
    float AversionCooldownSeconds = 2.5f;

    // ------------------------------------------------------------------ lids
    /** Spontaneous blink interval range. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Blink", meta = (Units = "s"))
    FVector2D BlinkIntervalSeconds = FVector2D(2.5, 5.0);
    /** Close/hold/open duration ranges (each blink picks its own speed). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Blink", meta = (Units = "s"))
    FVector2D BlinkCloseSeconds = FVector2D(0.055, 0.09);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Blink", meta = (Units = "s"))
    FVector2D BlinkHoldSeconds = FVector2D(0.015, 0.05);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Blink", meta = (Units = "s"))
    FVector2D BlinkOpenSeconds = FVector2D(0.11, 0.21);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Blink", meta = (ClampMin = "0", ClampMax = "1"))
    float DoubleBlinkChance = 0.15f;
    /** Chance of a blink accompanying a large gaze shift (> 20 degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Blink", meta = (ClampMin = "0", ClampMax = "1"))
    float GazeShiftBlinkChance = 0.35f;
    /** Reflex squint: a hand at least this fast within this radius of the face. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Blink", meta = (Units = "cm"))
    float ReflexRadiusCm = 30.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Blink")
    float ReflexSpeedCmPerSecond = 120.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Blink", meta = (Units = "s"))
    FVector2D ReflexSeconds = FVector2D(0.3, 0.5);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Blink", meta = (Units = "s"))
    float ReflexCooldownSeconds = 0.8f;

    // ------------------------------------------------------------------ elastic expression
    /** Expression changes snap in this time (2-3 frames at 90 Hz) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Elastic", meta = (ClampMin = "0.005", ClampMax = "0.3", Units = "s"))
    float SnapSeconds = 0.028f;
    /** ... overshoot by this share of the jump ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Elastic", meta = (ClampMin = "0", ClampMax = "1"))
    float BrowOvershoot = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Elastic", meta = (ClampMin = "0", ClampMax = "1"))
    float MouthOvershoot = 0.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Elastic", meta = (ClampMin = "0", ClampMax = "1"))
    float LidOvershoot = 0.12f;
    /** ... and settle on a damped sine of this frequency and damping. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Elastic", meta = (ClampMin = "0.3", ClampMax = "10", Units = "Hz"))
    float SettleHz = 2.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Elastic", meta = (ClampMin = "0.05", ClampMax = "1"))
    float SettleDamping = 0.22f;
    /** Jaw and cheeks lag the head: a change of head turn rate kicks a jelly spring by this much
     *  morph velocity per degree/second (a 300 deg/s snap gives about 0.1 weight) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Elastic", meta = (ClampMin = "0"))
    float JiggleGain = 0.012f;
    /** ... on a jelly spring of this frequency/damping, bounded to MaxJiggle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Elastic", meta = (Units = "Hz"))
    float JiggleHz = 5.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Elastic")
    float JiggleDamping = 0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Elastic", meta = (ClampMin = "0", ClampMax = "1"))
    float MaxJiggle = 0.18f;

    // ------------------------------------------------------------------ squash and stretch
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Squash")
    bool bSquashStretch = true;
    /** Vertical head stretch at full surprise / a moan peak (volume preserved: narrower). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Squash", meta = (ClampMin = "0", ClampMax = "0.2"))
    float StretchAmount = 0.08f;
    /** Vertical squash at full embarrassment / squeezed eyes (cheekbones widen). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Squash", meta = (ClampMin = "0", ClampMax = "0.2"))
    float SquashAmount = 0.06f;

    // ------------------------------------------------------------------ asymmetric mouth
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Mouth")
    bool bAsymmetricMouth = true;
    /** Viewer angle around the head where the mouth starts / fully moves onto the cheek (3/4 view). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Mouth", meta = (Units = "deg"))
    float AsymmetryStartDegrees = 15.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Mouth", meta = (Units = "deg"))
    float AsymmetryFullDegrees = 45.0f;
    /** Corner-up morph weight on the cheek side at full asymmetry. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Mouth", meta = (ClampMin = "0", ClampMax = "1"))
    float AsymmetryCornerWeight = 0.45f;
    /** Mouth slide along the face in the face material (GratiaMouthShift), cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Mouth", meta = (ClampMin = "0", ClampMax = "3", Units = "cm"))
    float MouthShiftCm = 0.6f;
    /** True: the mouth moves towards the silhouette (far cheek, anime-fighter style); false: towards the viewer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Mouth")
    bool bShiftTowardSilhouette = true;

    // ------------------------------------------------------------------ micro motion
    /** Blendshape micro-jitter on mouth open/smile/corners at rest (removes the frozen mask). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Micro")
    float JitterAmplitude = 0.035f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Micro", meta = (Units = "Hz"))
    float JitterHz = 0.7f;
    /** Lip and lid trembling at high excitement: amplitude at excitement 1 (scales with its square). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Micro")
    float TremorAmplitude = 0.07f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Micro", meta = (Units = "Hz"))
    float TremorHz = 11.0f;

    // ------------------------------------------------------------------ skin and eye shaders
    /** Material parameters are written on every material of the mesh (SetScalarParameterValueOnMaterials);
     *  materials without them ignore the value. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Shaders")
    bool bShaderParameters = true;
    /** Pupil size in darkness / bright light, and extra dilation at full excitement. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Shaders")
    float PupilDarkScale = 1.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Shaders")
    float PupilBrightScale = 0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Shaders")
    float PupilExcitementGain = 0.35f;
    /** Excitement above which heart pupils fade in (peak stages). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Shaders", meta = (ClampMin = "0", ClampMax = "1"))
    float HeartPupilThreshold = 0.85f;
    /** Seconds of sustained excitement above 0.5 for full sweat. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Shaders", meta = (Units = "s"))
    float SweatBuildSeconds = 40.0f;
    /** A touch slower than this raises goosebumps (a light stroke). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face|Shaders")
    float GoosebumpStrokeSpeedCmPerSecond = 25.0f;
};
