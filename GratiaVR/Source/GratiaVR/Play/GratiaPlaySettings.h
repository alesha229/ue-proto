#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GratiaPlaySettings.generated.h"

class USoundBase;
class USoundAttenuation;
class UStaticMesh;
class UMaterialInterface;

/** Limb that the player can take by the wrist or ankle (full-body IK follow). Bones are profile semantics. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaLimbGrabDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab")
    FName Name = TEXT("LeftWrist");
    /** Semantic bones: upper arm/thigh, forearm/shin, hand/foot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab")
    FName RootSemantic = TEXT("LeftUpperArm");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab")
    FName MidSemantic = TEXT("LeftForearm");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab")
    FName EndSemantic = TEXT("LeftHand");
    /** Body part the limb drags when pulled beyond its reach (Chest for arms, Pelvis for legs). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab")
    FName PullSemantic = TEXT("Chest");
    /** Bend direction when the limb is straight, in profile axes: X forward, Y right, Z up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab")
    FVector PoleHint = FVector(-1, 0, -0.3);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab", meta = (ClampMin = "1", ClampMax = "25", Units = "cm"))
    float GrabRadiusCm = 8.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab", meta = (ClampMin = "0.01", ClampMax = "1", Units = "s"))
    float BlendInSeconds = 0.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab", meta = (ClampMin = "0.01", ClampMax = "2", Units = "s"))
    float BlendOutSeconds = 0.45f;
    /** The limb follows the controller with this spring (Hz); lower is heavier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab", meta = (ClampMin = "0.5", ClampMax = "20"))
    float FollowHz = 6.0f;
    /** Share of the over-reach that moves the pulled body part, and its limit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab", meta = (ClampMin = "0", ClampMax = "1"))
    float PullFollow = 0.45f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab", meta = (ClampMin = "0", ClampMax = "40", Units = "cm"))
    float PullMaxCm = 12.0f;
    /** Feet stay where they were while the pelvis is pulled (leg IK back to the planted foot). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grab")
    bool bPlantFeet = true;
};

/** A body point kept above beds and floors. Limb ends use IK; others lift the bone (children follow). */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaGroundContactDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground")
    FName Semantic = TEXT("LeftFoot");
    /** For limb ends: semantic root and mid of the IK chain (empty: lift the bone itself). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground")
    FName RootSemantic;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground")
    FName MidSemantic;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "0.5", ClampMax = "30", Units = "cm"))
    float RadiusCm = 4.0f;
};

/** KawaiiPhysics multipliers of one chain group for the exaggerated anime feel (1 = profile values). */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSoftGroupTuning
{
    GENERATED_BODY()
    /** Lower world damping keeps more of the body's motion as lag/overshoot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anime Soft", meta = (ClampMin = "0", ClampMax = "2"))
    float WorldDamping = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anime Soft", meta = (ClampMin = "0", ClampMax = "2"))
    float Damping = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anime Soft", meta = (ClampMin = "0", ClampMax = "2"))
    float Stiffness = 1.0f;
    /** Share of the body's acceleration fed back as an opposite force (inertial swing on jolts and turns). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anime Soft", meta = (ClampMin = "0", ClampMax = "2"))
    float InertiaKick = 0.0f;
};

UENUM(BlueprintType)
enum class EGratiaGarmentAction : uint8
{
    /** Strap or edge pushed along the body (one hand). */
    Slide,
    /** Skirt or hem lifted (one hand); the fabric stays where it is left. */
    Lift,
    /** Button: pinch (trigger) on it and pull a little. */
    Unbutton,
    /** Zipper: one hand holds the fabric (anchor), the other pulls the slider. */
    Unzip,
    /** Ribbon/tie: both hands pull the ends apart. */
    Untie
};

/** One interactive part of the outfit. All references are profile semantics or material parameter names. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaGarmentPiece
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment")
    FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment")
    EGratiaGarmentAction Action = EGratiaGarmentAction::Slide;
    /** Where the hand takes the piece: semantic bone and offset in that bone's space. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment")
    FName HandleSemantic = TEXT("Chest");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment", meta = (Units = "cm"))
    FVector HandleOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment", meta = (ClampMin = "1", ClampMax = "20", Units = "cm"))
    float HandleRadiusCm = 6.0f;
    /** Pull direction in the handle bone's space and the distance that takes the piece from 0 to 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment")
    FVector DragAxis = FVector(0, 0, -1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment", meta = (ClampMin = "1", ClampMax = "80", Units = "cm"))
    float TravelCm = 12.0f;
    /** Second hand: holds the fabric (Unzip) or the other end (Untie). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment")
    FName AnchorSemantic;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment", meta = (Units = "cm"))
    FVector AnchorOffset = FVector::ZeroVector;
    /** Profile morph semantics driven by progress (strap slid, skirt lifted, shirt opened...). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment")
    TArray<FName> MorphSemantics;
    /** Scalar parameter on the mesh materials driven by progress (e.g. a reveal/opening mask). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment")
    FName MaterialParameter;
    /** Fabric bones that follow the drag and keep the new position (e.g. skirt spring bones). Weight 0..1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment")
    TMap<FName, float> FollowBones;
    /** Pieces that must be undone first (button before opening). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment")
    TArray<FName> Requires;
    /** Arousal stage needed before she lets this happen. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment", meta = (ClampMin = "0"))
    int32 MinStage = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment", meta = (ClampMin = "0", ClampMax = "1"))
    float SnapBack = 0.12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garment", meta = (ClampMin = "0.5", ClampMax = "1"))
    float Complete = 0.95f;
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaArousalStageDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (ClampMin = "0", ClampMax = "1"))
    float Threshold = 0.0f;
    /** Wanted pace of touch at this stage, cm/s; outside it the meter grows slower. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (Units = "cm/s"))
    float PaceMin = 4.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (Units = "cm/s"))
    float PaceMax = 20.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (Units = "cm/s"))
    float PaceSoft = 12.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (Units = "cm/s"))
    float RoughSpeed = 90.0f;
    /** Zone weights at this stage (contact zone names; missing = DefaultZoneWeight). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal")
    TMap<FName, float> ZoneWeights;
    /** Action tags this stage unlocks (poses, props, garments): IsUnlocked(Tag). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal")
    TArray<FName> Unlocks;
};

UENUM(BlueprintType)
enum class EGratiaVocalBank : uint8
{
    InhaleSoft, ExhaleSoft, InhaleDeep, Sigh, HeldRelease, Broken,
    /** Short non-verbal voice: "ннн…", "ах…" (soft and strong). */
    NonVerbalSoft, NonVerbalStrong,
    /** Breath into the player's ear when the head is close. */
    EarWhisper,
    /** Startle gasp on a sudden touch (the breath is then held). */
    Gasp
};

UENUM(BlueprintType)
enum class EGratiaFoleyBank : uint8
{
    /** Loops, volume follows hand speed. */
    SkinSlide, ClothRustle, WetSlide,
    /** One-shots. */
    ClothSnap, ButtonPop, ZipperTick, StrapSlip, OilPour, PropPickup, PropDrop
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSoundBank
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
    TArray<TObjectPtr<USoundBase>> Sounds;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = "0", ClampMax = "2"))
    float Volume = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = "0", ClampMax = "0.3"))
    float PitchJitter = 0.04f;
};

UENUM(BlueprintType)
enum class EGratiaPropKind : uint8
{
    /** Bottle: pouring raises the wetness/oil gloss where it is held over the body. */
    Oil,
    /** Vibrating toy: trigger cycles its modes; it buzzes the holding hand and stimulates the touched zone. */
    Toy,
    /** Light, ticklish touch: weak stimulus, strong reactions on sensitive zones. */
    Feather,
    /** Accessory: released near its bone it attaches there (hair clip, ribbon, collar). */
    Accessory
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaPropDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
    EGratiaPropKind Kind = EGratiaPropKind::Toy;
    /** Empty: an engine basic shape stands in (cylinder/sphere/cube by kind). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
    TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
    TObjectPtr<UMaterialInterface> Material;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
    FVector Scale = FVector(0.04, 0.04, 0.16);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
    FLinearColor Color = FLinearColor(1.0f, 0.35f, 0.6f);
    /** Working end of the prop in its own space (cm), where it touches the body. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop", meta = (Units = "cm"))
    FVector Tip = FVector(0, 0, 8);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop", meta = (ClampMin = "0", ClampMax = "3"))
    float Stimulus = 1.0f;
    /** Accessory: semantic bone it attaches to, offset and rotation there. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
    FName AttachSemantic;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
    FTransform AttachTransform;
    /** Arousal unlock tag needed to pick it up (empty: always). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
    FName RequiredUnlock;
};

/**
 * Settings of the interaction layer for one character (spec sections 4 and 5): physics press, limb grabs,
 * ground adaptation, anime soft physics, garments, arousal stages, haptics, breathing/voice/foley banks and props.
 * Referenced from UGratiaCharacterProfile::PlaySettings; a profile without it uses these defaults.
 */
UCLASS(BlueprintType)
class GRATIAVR_API UGratiaPlaySettings : public UDataAsset
{
    GENERATED_BODY()
public:
    UGratiaPlaySettings();

    // ---- Physics press (active ragdoll blend on top of the existing Physical Animation bodies)
    /** Drive strength of a body at full press (0..1 of the profile value): the body yields under the hand. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Blend", meta = (ClampMin = "0", ClampMax = "1"))
    float PressSoftness = 0.3f;
    /** Per-bone override of PressSoftness (bone names of the simulated bodies). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Blend")
    TMap<FName, float> PressSoftnessOverrides;
    /** Extra physics blend weight while pressed (the yielding is visible), added to the group's weight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Blend", meta = (ClampMin = "0", ClampMax = "1"))
    float PressBlendBoost = 0.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Blend", meta = (ClampMin = "0.5", ClampMax = "30", Units = "cm"))
    float PressRangeCm = 6.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Blend", meta = (ClampMin = "0.01", ClampMax = "1", Units = "s"))
    float PressAttackSeconds = 0.06f;
    /** How long the body takes to return into the animation after the hand leaves. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Blend", meta = (ClampMin = "0.05", ClampMax = "3", Units = "s"))
    float ReturnSeconds = 0.5f;

    // ---- Full body IK
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grabs", meta = (TitleProperty = "Name"))
    TArray<FGratiaLimbGrabDefinition> LimbGrabs;
    /** Grip value that takes/keeps a limb. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb Grabs", meta = (ClampMin = "0.05", ClampMax = "1"))
    float GrabThreshold = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (TitleProperty = "Semantic"))
    TArray<FGratiaGroundContactDefinition> GroundContacts;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "5", ClampMax = "200", Units = "cm"))
    float GroundTraceCm = 60.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "0", ClampMax = "60", Units = "cm"))
    float GroundMaxLiftCm = 25.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "0.01", ClampMax = "1", Units = "s"))
    float GroundBlendSeconds = 0.08f;

    // ---- Anime soft physics (multipliers of the profile's KawaiiPhysics chains)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anime Soft")
    bool bAnimeSoft = true;
    /** Soft body parts (breasts, buttocks, thighs). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anime Soft")
    FGratiaSoftGroupTuning SoftBody;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anime Soft")
    FGratiaSoftGroupTuning Hair;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anime Soft")
    FGratiaSoftGroupTuning ClothDecor;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anime Soft")
    FGratiaSoftGroupTuning EarsTail;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anime Soft", meta = (ClampMin = "0", Units = "cm/s"))
    float MaxInertiaKick = 3000.0f;

    // ---- Garments
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garments", meta = (TitleProperty = "Name"))
    TArray<FGratiaGarmentPiece> Garments;
    /** Trigger (pinch) or grip value that takes a garment handle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garments", meta = (ClampMin = "0.05", ClampMax = "1"))
    float GarmentGrabThreshold = 0.55f;
    /** A zipper without the anchoring hand sticks here. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garments", meta = (ClampMin = "0", ClampMax = "1"))
    float GarmentStuckLimit = 0.15f;

    // ---- Arousal / mood progression
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (TitleProperty = "Name"))
    TArray<FGratiaArousalStageDefinition> Stages;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (ClampMin = "0", ClampMax = "2"))
    float DefaultZoneWeight = 0.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (ClampMin = "0", ClampMax = "1"))
    float Gain = 0.035f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (ClampMin = "0", ClampMax = "1"))
    float Decay = 0.012f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (ClampMin = "0", ClampMax = "60", Units = "s"))
    float IdleDelay = 4.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (ClampMin = "0", ClampMax = "1"))
    float RoughPenalty = 0.06f;
    /** Each answered reaction (any source, including channels) adds this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal", meta = (ClampMin = "0", ClampMax = "0.2"))
    float ReactionBonus = 0.01f;
    /** Gain multiplier per archetype (Mood 0 Кудере, 1 Дередере, 2 Цундере). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal")
    TArray<float> MoodGain;
    /** Scalar parameter written to the character's materials (blush/sweat shaders may read it). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal")
    FName ArousalMaterialParameter = TEXT("Arousal");

    // ---- Haptics
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics")
    float SlideBase = 0.04f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics")
    float SlideGain = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics", meta = (Units = "cm/s"))
    float SlideFullSpeed = 60.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics", meta = (Units = "cm"))
    float SlideGrainCm = 1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics")
    float PressBase = 0.08f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics")
    float PressMax = 0.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics", meta = (Units = "cm"))
    float PressFullDepthCm = 3.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics")
    float HeartRestBpm = 68.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics")
    float HeartPeakBpm = 140.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics")
    float HeartGain = 0.3f;
    /** Contact zones where the pulse is felt (plus a held wrist). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics")
    TArray<FName> HeartZones;
    /** Below this arousal the pulse is felt only in the heart zones; above it faintly everywhere on skin. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics", meta = (ClampMin = "0", ClampMax = "1"))
    float HeartEverywhereArousal = 0.75f;

    // ---- Breath, voice and foley (hooks: empty banks stay silent and are reported once)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice")
    TMap<EGratiaVocalBank, FGratiaSoundBank> VocalBanks;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice")
    TMap<EGratiaFoleyBank, FGratiaSoundBank> FoleyBanks;
    /** Mouth from the Head semantic bone, in profile axes (forward, up), cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice", meta = (Units = "cm"))
    float MouthForwardCm = 9.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice", meta = (Units = "cm"))
    float MouthUpCm = -6.0f;
    /** Empty: the reaction presenter's attenuation (the voice thread's binaural path). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice")
    TObjectPtr<USoundAttenuation> VoiceAttenuation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice", meta = (ClampMin = "0", ClampMax = "2"))
    float BreathVolume = 0.5f;
    /** Breathing becomes audible from this arousal (quiet breaths are only heard close). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice", meta = (ClampMin = "0", ClampMax = "1"))
    float AudibleBreathArousal = 0.15f;
    /** Player's head this close to her mouth: whispering breath into the ear (ASMR). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice", meta = (Units = "cm"))
    float EarCloseCm = 30.0f;
    /** After a voiced reaction the breath layer keeps quiet (no overlap with the line). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice", meta = (Units = "s"))
    float ReactionQuietSeconds = 1.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice", meta = (ClampMin = "0", ClampMax = "2"))
    float FoleyVolume = 0.6f;
    /** Empty bank: synthesised placeholder (shaped noise, hum, clicks) instead of silence. Recordings always win. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice")
    bool bSynthFallback = true;
    /** Mesh scalar parameter that marks wet skin (wetness shader); above WetFoleyThreshold slides sound wet. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice")
    FName WetnessParameter = TEXT("Wetness");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice", meta = (ClampMin = "0", ClampMax = "1"))
    float WetFoleyThreshold = 0.3f;
    /** Contact zones covered by clothing (rustle instead of skin). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice")
    TArray<FName> ClothedZones;

    // ---- Props
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props", meta = (TitleProperty = "Name"))
    TArray<FGratiaPropDefinition> Props;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props")
    FName OilParameter = TEXT("Oil");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props", meta = (ClampMin = "0", ClampMax = "1"))
    float OilPerSecond = 0.25f;

    const FGratiaSoftGroupTuning& GetSoftTuning(uint8 Group) const
    {
        return Group == 1 ? Hair : Group == 2 ? ClothDecor : Group == 4 ? EarsTail : SoftBody;
    }
    float GetZoneWeight(int32 Stage, FName Zone) const;
    bool IsUnlocked(int32 Stage, FName Tag) const;
    bool Validate(TArray<FString>& Errors) const;
};
