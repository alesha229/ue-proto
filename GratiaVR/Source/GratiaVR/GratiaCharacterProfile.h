#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GratiaCharacterProfile.generated.h"

class USkeletalMesh;
class UAnimSequence;
class UAnimInstance;
class UPhysicsAsset;
class USoundBase;
class UMaterialParameterCollection;

UENUM(BlueprintType)
enum class EGratiaCollisionProxyShape : uint8
{
    Sphere,
    Capsule
};

/** Hand-blocking geometry is independent of reaction-zone geometry. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaCollisionProxyDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
    EGratiaCollisionProxyShape Shape = EGratiaCollisionProxyShape::Sphere;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
    FName StartBoneSemantic;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
    FName EndBoneSemantic;
    /** Endpoint offsets are in actor-local space, in centimetres. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
    FVector StartOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
    FVector EndOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "0.1", Units = "cm"))
    float Radius = 5.0f;
};

/** A character owns geometry and names; interaction code refers to semantic keys. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaContactZoneDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact")
    FName Name;

    /** Key in SemanticBones, for example Head or LeftHand. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact")
    FName BoneSemantic;

    /** Offset in the actor's local space, in centimetres. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact")
    FVector Offset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.1", Units = "cm"))
    float Radius = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact")
    bool bCanHold = true;

    /** Smaller values win when zones overlap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0"))
    int32 Priority = 0;
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSecondaryBoneDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion")
    FName Bone;

    /** 1 = hair, 2 = clothing/decor, 3 = local body, 4 = ears/tail. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "1", ClampMax = "4"))
    uint8 Group = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "0.0", Units = "cm"))
    float RestLengthCm = 0.0f;

    /** False preserves a body as an animation-controlled anchor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion")
    bool bSafeSimulation = true;
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaQualityProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
    FName Name = TEXT("Medium");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality", meta = (ClampMin = "0"))
    int32 HairCap = 32;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality", meta = (ClampMin = "0"))
    int32 ClothCap = 16;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality", meta = (ClampMin = "0"))
    int32 BodyCap = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality", meta = (ClampMin = "0"))
    int32 EarCap = 11;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality", meta = (ClampMin = "0"))
    int32 TotalBodyCap = 64;

    int32 GetGroupCap(uint8 Group) const;
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSecondaryGroupSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "1", ClampMax = "4"))
    uint8 Group = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BlendWeight = 0.55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "0.0"))
    float OrientationStrength = 180.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "0.0"))
    float AngularVelocityStrength = 40.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "0.0"))
    float MaxAngularForce = 35.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "0.0", Units = "deg"))
    float SpringLimitDegrees = 3.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "0.0"))
    float SpringStiffness = 90.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "0.0"))
    float SpringDamping = 19.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion")
    float HeadInertiaScale = -0.012f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion")
    FVector SpringLocalAxis = FVector::ForwardVector;
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaContactSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0", Units = "s"))
    float HoldSeconds = 0.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0", Units = "s"))
    float CooldownSeconds = 0.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0", Units = "s"))
    float SingleTouchSeconds = 0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.1", Units = "cm"))
    float HandRadiusCm = 6.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0", Units = "cm"))
    float TouchPaddingCm = 6.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0", Units = "cm"))
    float HoverPaddingCm = 20.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0", Units = "s"))
    float ReactionSeconds = 1.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0", Units = "s"))
    float ReactionMinimumIntervalSeconds = 0.45f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0", Units = "s"))
    float CaptionSeconds = 1.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact")
    FVector CaptionOffset = FVector(0, 0, 225);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.1", Units = "s"))
    float DemoIntervalSeconds = 4.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.1"))
    float MaxHandSpeedCmPerSecond = 500.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.1"))
    float ImpulseSpeedCmPerSecond = 150.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.1"))
    float StrongReactionSpeedCmPerSecond = 120.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0"))
    float ReactionInterpSpeed = 8.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0"))
    float ImpulseDecaySpeed = 4.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HoldReactionWeight = 0.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.0", Units = "s"))
    float ContactRecoverySeconds = 0.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0.1", Units = "cm"))
    float MaxHandCorrectionCm = 20.0f;
};

/** Bounded hand pressure against active Chaos bodies; separate from reaction zones. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaHandPhysicsSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics")
    bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics", meta = (ClampMin = "0.1"))
    float RadiusCm = 6.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics", meta = (ClampMin = "0.0"))
    float Stiffness = 30.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics", meta = (ClampMin = "0.0"))
    float Damping = 0.5f;
    /** Per hand, shared across all overlapping bodies, in kg cm / s^2. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics", meta = (ClampMin = "0.0"))
    float MaxForce = 300.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics", meta = (ClampMin = "0.1"))
    float MaxSpeedCmPerSecond = 500.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics", meta = (ClampMin = "0.1"))
    float MaxTravelCm = 35.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics|Grab")
    bool bAllowGrab = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics|Grab", meta = (ClampMin = "0.0"))
    float GrabStiffness = 60.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics|Grab", meta = (ClampMin = "0.0"))
    float GrabDamping = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics|Grab", meta = (ClampMin = "0.1"))
    float GrabBreakDistanceCm = 15.0f;
};

/** Bone axis that points from a soft-body bone head toward its tip. */
UENUM(BlueprintType)
enum class EGratiaBoneAxis : uint8
{
    XPositive, XNegative, YPositive, YNegative, ZPositive, ZNegative
};

/**
 * One KawaiiPhysics chain set (for example both breasts). Each root bone is simulated
 * with a tip dummy of DummyBoneLengthCm, VRChat PhysBones style: pull/spring to pose,
 * collision with body and hand spheres, and a spring grab.
 */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSoftBodyChain
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body")
    TArray<FName> RootBones;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body")
    EGratiaBoneAxis ForwardAxis = EGratiaBoneAxis::XPositive;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body", meta = (ClampMin = "0.5", Units = "cm"))
    float DummyBoneLengthCm = 8.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Kawaii", meta = (ClampMin = "0", ClampMax = "1"))
    float Damping = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Kawaii", meta = (ClampMin = "0", ClampMax = "1"))
    float Stiffness = 0.05f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Kawaii", meta = (ClampMin = "0", ClampMax = "1"))
    float WorldDampingLocation = 0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Kawaii", meta = (ClampMin = "0", ClampMax = "1"))
    float WorldDampingRotation = 0.8f;
    /** Kawaii collision radius of the simulated tip against body and hand spheres. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Kawaii", meta = (ClampMin = "0", Units = "cm"))
    float CollisionRadiusCm = 3.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Kawaii", meta = (ClampMin = "0", ClampMax = "90"))
    float LimitAngleDegrees = 30.0f;
    /** Fraction of world gravity (980 cm/s^2). Applied relative to the authored pose: the rest shape
     * already includes gravity, so the bone sags only when the body tilts away from its reference
     * orientation (no constant sag pushing skin through clothing). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Kawaii", meta = (ClampMin = "0", ClampMax = "2"))
    float GravityScale = 0.2f;
    /** Touch / haptic / finger-conform volume around the bone, centred along it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Contact", meta = (ClampMin = "0.5", Units = "cm"))
    float ContactRadiusCm = 7.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Contact", meta = (ClampMin = "0", ClampMax = "1.5"))
    float ContactCenterAlongBone = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Grab")
    bool bAllowGrab = true;
    /** 0: spring pull toward the hand, 1: tip follows the hand immediately (VRChat Grab Movement). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Grab", meta = (ClampMin = "0", ClampMax = "1"))
    float GrabMovement = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Grab", meta = (ClampMin = "0", Units = "cm"))
    float MaxGrabStretchCm = 6.0f;
    /** Jiggle as a mass on a spring (the bone is translated) instead of a swinging KawaiiPhysics
     *  bone: flesh around a limb (thighs) wobbles with the body's motion; a swinging bone along
     *  the limb would shear the front and back of the flesh up and down. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Jiggle")
    bool bTranslational = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Jiggle", meta = (ClampMin = "0.5", ClampMax = "10", Units = "Hz"))
    float JiggleFrequencyHz = 3.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Jiggle", meta = (ClampMin = "0.02", ClampMax = "1"))
    float JiggleDampingRatio = 0.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Jiggle", meta = (ClampMin = "0", ClampMax = "10", Units = "cm"))
    float MaxJiggleCm = 2.0f;
};

/**
 * Spring chain for hair, clothing decor, ears and tails (VRChat PhysBones style): KawaiiPhysics
 * bones from each root to its tips with a TipLengthCm dummy past every end bone, a spring back to
 * the animated pose, collision with the BodySurface capsules and the hand spheres, and a trigger
 * grab. Bones of a spring chain must not also be Chaos secondary bodies (bSafeSimulation off).
 */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSpringChain
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain")
    FName Name;
    /** Each root stays on the animated pose; everything below it swings. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain")
    TArray<FName> RootBones;
    /** Menu group: 1 hair, 2 clothing/decor, 4 ears/tail (as SecondaryBones). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain", meta = (ClampMin = "1", ClampMax = "4"))
    uint8 Group = 1;
    /** Bone axis pointing along the bone (direction of the tip dummy). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain")
    EGratiaBoneAxis ForwardAxis = EGratiaBoneAxis::YPositive;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain", meta = (ClampMin = "0.5", Units = "cm"))
    float TipLengthCm = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain|Kawaii", meta = (ClampMin = "0", ClampMax = "1"))
    float Damping = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain|Kawaii", meta = (ClampMin = "0", ClampMax = "1"))
    float Stiffness = 0.05f;
    /** 1: the chain moves rigidly with the body; lower values let it lag behind body motion. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain|Kawaii", meta = (ClampMin = "0", ClampMax = "1"))
    float WorldDampingLocation = 0.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain|Kawaii", meta = (ClampMin = "0", ClampMax = "1"))
    float WorldDampingRotation = 0.6f;
    /** Bone thickness against body capsules and hand spheres. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain|Kawaii", meta = (ClampMin = "0", Units = "cm"))
    float CollisionRadiusCm = 1.0f;
    /** Maximum swing from the pose per bone; 0 = no limit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain|Kawaii", meta = (ClampMin = "0", ClampMax = "180"))
    float LimitAngleDegrees = 0.0f;
    /** Fraction of world gravity, relative to the authored pose (the rest shape already hangs). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain|Kawaii", meta = (ClampMin = "0", ClampMax = "2"))
    float GravityScale = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain|Grab")
    bool bAllowGrab = true;
    /** 0: spring pull toward the hand, 1: the grabbed bone follows the hand immediately. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain|Grab", meta = (ClampMin = "0", ClampMax = "1"))
    float GrabMovement = 0.8f;
    /** Farthest the grabbed bone is pulled from its pose (the chain's bone lengths still hold). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring Chain|Grab", meta = (ClampMin = "0", Units = "cm"))
    float MaxGrabStretchCm = 30.0f;
};

/** Source body collision approximated by spheres that follow a skinning bone. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaBodyColliderSphere
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body")
    FName Bone;
    /** Centre in component space at the reference pose. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body", meta = (Units = "cm"))
    FVector RefCenterCm = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body", meta = (ClampMin = "0.1", Units = "cm"))
    float RadiusCm = 5.0f;
};

/** A full-body clip the Pose menu item (F2) can play after the preview poses, e.g. retargeted mocap. */
/** One partner bone aimed so that its reference child lies along From -> To (partner component space, cm). */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaPartnerAim
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Partner")
    FName Bone;
    /** Child bone whose direction from Bone is aimed (reference skeleton). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Partner")
    FName Child;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Partner", meta = (Units = "cm"))
    FVector From = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Partner", meta = (Units = "cm"))
    FVector To = FVector::ZeroVector;
    /** Also place Bone at From (the chain root, e.g. the pelvis). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Partner")
    bool bPlace = false;
};

/** Everything a performance brings besides the character clip: synchronized music, a posed
 *  partner body and the partner's eye viewpoint. Transforms are relative to the character actor. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaPerformanceScene
{
    GENERATED_BODY()
    /** Plays in sync with the performance time (one track for all segments). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Scene")
    TObjectPtr<USoundBase> Music;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Scene", meta = (ClampMin = "0", ClampMax = "2"))
    float MusicVolume = 0.8f;
    /** Static partner body (any skeletal mesh); posed by PartnerPose in its own component space. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Scene")
    TObjectPtr<USkeletalMesh> PartnerMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Scene")
    FTransform PartnerTransform;
    /** Applied in order (parents before children). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Scene")
    TArray<FGratiaPartnerAim> PartnerPose;
    /** Hidden while the player watches from the partner's eyes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Scene")
    TArray<FName> PartnerHiddenInViewpoint;
    /** Partner eyes: X looks forward, Z is the top of the head. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Scene")
    bool bHasViewpoint = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance|Scene")
    FTransform Viewpoint;

    bool HasPartner() const { return PartnerMesh != nullptr && !PartnerPose.IsEmpty(); }
};

UENUM(BlueprintType)
enum class EGratiaReactionForce : uint8
{
    Any,
    /** Slower than ContactSettings.StrongReactionSpeedCmPerSecond. */
    Gentle,
    /** A fast touch: these lines win over every other match. */
    Strong
};

/**
 * What the character answers a reaction with: a speech-bubble Text and/or a voice Sound. The presenter picks at
 * random among the most specific matches (strong touch, then zone or channel, then mood), never the same line
 * twice in a row when another one matches.
 */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaReactionLine
{
    GENERATED_BODY()

    /** Bubble text; empty shows no bubble. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction Line")
    FText Text;
    /** Voice; empty falls back to ReactionSounds / DefaultReactionSound. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction Line")
    TObjectPtr<USoundBase> Sound;
    /** More takes of the same voice: each answer picks one of Sound and these, never the take that played last. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction Line")
    TArray<TObjectPtr<USoundBase>> Variants;
    /** Mood index (0 calm, 1 cheerful, 2 reserved) or -1 for any mood. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction Line", meta = (ClampMin = "-1", ClampMax = "2"))
    int32 Mood = -1;
    /** Contact zones or penetration channels it answers; empty answers any. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction Line")
    TArray<FName> Zones;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction Line")
    EGratiaReactionForce Force = EGratiaReactionForce::Any;
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaPerformanceClip
{
    GENERATED_BODY()

    /** Menu label; empty uses the clip asset name. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    FName Name;
    /** Shown to the player (Pose item); empty uses Name. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    FText Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    TObjectPtr<UAnimSequence> Clip;
    /** A long take split for import: played after Clip in order, as one performance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    TArray<TObjectPtr<UAnimSequence>> Segments;
    /** Loop the whole performance (all segments); otherwise hold the last frame. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    bool bLoop = true;
    /** Music, partner and viewpoint of the original scene (optional). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    FGratiaPerformanceScene Scene;

    int32 NumParts() const { return 1 + Segments.Num(); }
    UAnimSequence* GetPart(int32 Index) const { return Index == 0 ? Clip.Get() : Segments.IsValidIndex(Index - 1) ? Segments[Index - 1].Get() : nullptr; }
};

/** One bone a surface capsule follows, with its share (like a skinned vertex). */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSurfaceInfluence
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Surface")
    FName Bone;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Surface", meta = (ClampMin = "0", ClampMax = "1"))
    float Weight = 1.0f;
};

/** Capsule fitted to the visible skin/clothing around one bone (reference pose, bone space). */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSurfaceCapsule
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Surface")
    FName Bone;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Surface", meta = (Units = "cm"))
    FVector StartCm = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Surface", meta = (Units = "cm"))
    FVector EndCm = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Surface", meta = (ClampMin = "0.2", Units = "cm"))
    float RadiusCm = 3.0f;
    /** Bone-space axis a gripping hand wraps around; zero = along the capsule (limbs). Torso
     *  slices are capsules across the body's width and wrap around the spine direction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Surface")
    FVector WrapAxis = FVector::ZeroVector;
    /** A gripping hand may wrap around this part (head, neck and soft parts: no). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Surface")
    bool bGrip = true;
    /** Soft part (moves with its KawaiiPhysics bone): squeezing fingers may sink into it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Surface")
    bool bSoft = false;
    /** The capsule moves like the skin it was fitted to: blended over these bones (weights
     *  of the fitted vertices). Empty: rigid with Bone. A butt sphere follows pelvis and thigh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Surface")
    TArray<FGratiaSurfaceInfluence> Influences;
};

/** Hands on the body surface: palm collider, leaning onto the surface and wrapping grips. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaHandSurfaceSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Surface")
    bool bEnabled = true;
    /** Palm collider radius against BodySurface (replaces the wrist sphere of ContactSettings). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Surface", meta = (ClampMin = "0.2", ClampMax = "6", Units = "cm"))
    float PalmContactRadiusCm = 1.5f;
    /** Palm centre to palm skin; a gripping palm centre stays this far from the surface. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Surface", meta = (ClampMin = "0", ClampMax = "5", Units = "cm"))
    float PalmThicknessCm = 1.4f;
    /** Within this gap the hand turns its palm to the surface and the fingers lie on it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Surface", meta = (ClampMin = "0", ClampMax = "15", Units = "cm"))
    float AdaptDistanceCm = 4.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Surface", meta = (ClampMin = "0", ClampMax = "90", Units = "deg"))
    float AdaptMaxDegrees = 45.0f;
    /** Grip within this gap of a body part wraps the hand around it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Surface", meta = (ClampMin = "0", ClampMax = "20", Units = "cm"))
    float GripReachCm = 7.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Surface", meta = (ClampMin = "0.05", ClampMax = "1"))
    float GripStartInput = 0.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Surface", meta = (ClampMin = "0", ClampMax = "1"))
    float GripReleaseInput = 0.3f;
    /** Trigger/grip at which the hand cups a soft part; squeeze depth follows the input above it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Surface", meta = (ClampMin = "0.01", ClampMax = "1"))
    float CupStartInput = 0.15f;
    /** The wrapped hand lets go when the controller moves this far from it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Surface", meta = (ClampMin = "2", ClampMax = "60", Units = "cm"))
    float GripBreakDistanceCm = 18.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Surface", meta = (ClampMin = "0", ClampMax = "1", Units = "s"))
    float GripBlendSeconds = 0.12f;
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSoftBodySettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body")
    bool bEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body", meta = (TitleProperty = "Name"))
    TArray<FGratiaSoftBodyChain> Chains;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body")
    TArray<FGratiaBodyColliderSphere> BodyColliders;
    /** Rigid contact proxies stop the visible hand outside a soft zone; inside a zone the
     *  hand may follow the raw controller at most this much deeper (soft tissue yields). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Hands", meta = (ClampMin = "0", ClampMax = "10", Units = "cm"))
    float SoftPressDepthCm = 4.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Hands", meta = (ClampMin = "0.5", Units = "cm"))
    float PalmRadiusCm = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Hands", meta = (ClampMin = "0.2", Units = "cm"))
    float FingerRadiusCm = 1.1f;
    /** Grab search distance beyond the contact volume surface. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Hands", meta = (ClampMin = "0", Units = "cm"))
    float GrabRadiusCm = 4.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Hands", meta = (ClampMin = "1", Units = "cm"))
    float GrabBreakDistanceCm = 20.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Hands", meta = (ClampMin = "1"))
    float MaxHandSpeedCmPerSecond = 500.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Hands")
    bool bFingerConform = true;
    /** Finger joints stop this far outside a contact or body sphere. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Hands", meta = (ClampMin = "0", Units = "cm"))
    float FingerConformMarginCm = 0.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Haptics")
    bool bHaptics = true;
    /** Penetration depth at which vibration reaches HapticMaxAmplitude. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Haptics", meta = (ClampMin = "0.1", Units = "cm"))
    float HapticFullDepthCm = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Haptics", meta = (ClampMin = "0", ClampMax = "1"))
    float HapticMaxAmplitude = 0.85f;
    /** Shapes the depth response: amplitude = max * (depth / full)^exponent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Haptics", meta = (ClampMin = "0.2", ClampMax = "4"))
    float HapticDepthExponent = 1.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Haptics", meta = (ClampMin = "0", ClampMax = "1"))
    float HapticMinFrequency = 0.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Haptics", meta = (ClampMin = "0", ClampMax = "1"))
    float HapticMaxFrequency = 0.6f;
    /** Short tap when a hand first touches a soft zone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Haptics", meta = (ClampMin = "0", ClampMax = "1"))
    float HapticContactPulse = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Haptics", meta = (ClampMin = "0", ClampMax = "0.5", Units = "s"))
    float HapticPulseSeconds = 0.06f;
    /** Surface press of skin and clothing (material vertex offset). Receives hand spheres
     *  Sphere00..Sphere23, soft zones Zone0..Zone3 and Config (softness, -, zone falloff,
     *  strength). Materials reference the same collection; empty disables the press. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press")
    TObjectPtr<UMaterialParameterCollection> PressCollection;
    /** Palm sphere of the surface dent (the palm itself; PalmRadiusCm is the contact/haptics volume). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0.5", ClampMax = "8", Units = "cm"))
    float PressPalmRadiusCm = 1.5f;
    /** Width of the smooth dent rim around each palm/finger sphere. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0.1", ClampMax = "10", Units = "cm"))
    float PressSoftnessCm = 1.5f;
    /** Press mask radius = soft zone contact radius x this scale, plus PressZoneFalloffCm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0.5", ClampMax = "4"))
    float PressZoneScale = 1.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0.1", ClampMax = "20", Units = "cm"))
    float PressZoneFalloffCm = 4.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0", ClampMax = "1"))
    float PressStrength = 1.0f;
    /** With full grip the fingers may sink this far into a soft zone; the surface yields under them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0", ClampMax = "6", Units = "cm"))
    float SquishDepthCm = 3.5f;
    /** Volumetric squeeze: the soft bone compresses along the press direction by up to this
     *  fraction (also the squeeze of a full-trigger cup) and springs back with a wobble. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0", ClampMax = "0.6"))
    float SquashAmount = 0.55f;
    /** 1: the skin under the hand moves with the press depth (no gap, no sinking); above 1
     *  the part compresses more than the hand presses. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0.25", ClampMax = "3"))
    float SquashResponse = 1.0f;
    /** Sideways spread while compressed (0 none, 1 volume preserving). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0", ClampMax = "1"))
    float SquashBulge = 0.5f;
    /** Hand sphere size for pushing the KawaiiPhysics bone; below 1 the surface yields
     *  (press dent and squash) before the whole soft part swings away. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0", ClampMax = "1"))
    float HandPushFraction = 0.4f;
};

/** A bone a penetration channel moves: pushed away from the channel axis as the shaft opens the
 *  channel at the bone's depth, and dragged along with the shaft's motion. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaChannelBone
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    FName Bone;
    /** Offset per cm of opening (1: the wall stays on the shaft surface). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "3"))
    float Response = 1.0f;
    /** Opening before this bone moves at all: outer rings (buttocks, hips) yield only to large sizes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "20", Units = "cm"))
    float StartOpeningCm = 0.0f;
    /** The stretch saturates smoothly toward this offset (no hard stop for large sizes). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "20", Units = "cm"))
    float MaxOffsetCm = 3.0f;
    /** Pull along the shaft: cm of offset per cm/s of insertion speed (in and out), at most MaxDragCm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "0.2", Units = "s"))
    float DragSeconds = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "5", Units = "cm"))
    float MaxDragCm = 0.8f;
    /** Outer ring (hips, buttocks): spreads with the channel's opening beyond StartOpeningCm, so only large sizes
     *  move it; otherwise the bone only clears the shaft surface from its rest distance to the axis. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    bool bOuterRing = false;
};

/** A swelling of the body (mostly the belly in front) where a deep shaft passes DepthCm along the channel. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaChannelBulge
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    FName Morph;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "100", Units = "cm"))
    float DepthCm = 15.0f;
};

/**
 * A body channel a jointed shaft (AGratiaPenetrator) can enter. It follows AnchorBoneSemantic:
 * entrance at the reference-pose centroid of EntranceBones (+ EntranceOffsetCm), straight inward
 * toward InwardTargetBone (+ InwardOffsetCm) for DepthCm. Name is the reaction zone: ReactionClips
 * and ReactionSounds use it as a key. Missing bones disable the channel with a warning.
 */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaPenetrationChannel
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    bool bEnabled = true;
    /** Key in SemanticBones; the channel moves with this bone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    FName AnchorBoneSemantic = TEXT("Pelvis");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    TArray<FName> EntranceBones;
    /** Component space at the reference pose, cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (Units = "cm"))
    FVector EntranceOffsetCm = FVector::ZeroVector;
    /** The channel points from the entrance toward this bone (reference pose); None: the anchor bone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    FName InwardTargetBone;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (Units = "cm"))
    FVector InwardOffsetCm = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "1", ClampMax = "60", Units = "cm"))
    float DepthCm = 14.0f;
    /** The tip enters within this distance of the entrance (plus half the shaft radius). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0.5", ClampMax = "10", Units = "cm"))
    float CaptureRadiusCm = 3.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "5", ClampMax = "89", Units = "deg"))
    float CaptureAngleDegrees = 50.0f;
    /** An engaged shaft bent further than this from the channel lets go. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "30", ClampMax = "170", Units = "deg"))
    float ReleaseAngleDegrees = 115.0f;
    /** Closed channel radius: the walls move once the shaft is thicker than this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "5", Units = "cm"))
    float RestRadiusCm = 0.4f;
    /** The wall stretches over this length around the shaft (ahead of the tip and behind it). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "10", Units = "cm"))
    float WallFalloffCm = 2.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (TitleProperty = "Bone"))
    TArray<FGratiaChannelBone> Bones;
    /** Optional morph target driven by the entrance opening (1 at MorphFullOpeningCm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    FName OpeningMorph;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0.1", ClampMax = "20", Units = "cm"))
    float MorphFullOpeningCm = 4.0f;
    /** Material scalar parameter on the character mesh that receives the opening morph's weight (a skin material
     *  shading the stretched skin by a baked mask); none: not sent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    FName StretchParameter;
    /** Swellings along the channel: each morph follows the radius of the shaft passing its depth, fully at
     *  BulgeFullRadiusCm (a thin shaft barely shows, a fist or a large size bulges the belly). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Bulge")
    TArray<FGratiaChannelBulge> Bulges;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Bulge", meta = (ClampMin = "0.5", ClampMax = "20", Units = "cm"))
    float BulgeFullRadiusCm = 4.5f;
    /** Shafts thicker than BulgeFullRadiusCm keep growing the swelling (the morph past 1), up to this weight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Bulge", meta = (ClampMin = "1", ClampMax = "3"))
    float BulgeMaxWeight = 1.0f;
    /** Tightness along the channel (X depth cm, Y 0..1, linear between points): rings that a wider part of the shaft
     *  has to stretch (the shaft stays behind the hand until pushed hard enough, then pops through) and rub against;
     *  empty: no resistance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Resistance")
    TArray<FVector2D> Resistance;
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaPenetrationSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    bool bEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (TitleProperty = "Name"))
    TArray<FGratiaPenetrationChannel> Channels;
    /** Wall bones approach their offsets at this rate (1/s): soft, without popping. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "1", ClampMax = "100"))
    float WallFollowSpeed = 22.0f;
    /** After the shaft narrows or leaves, the walls stay open this long before they start closing; a wider
     *  opening waits longer (CloseDelayPerCm per cm of the widest opening). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Closing", meta = (ClampMin = "0", ClampMax = "5", Units = "s"))
    float CloseDelaySeconds = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Closing", meta = (ClampMin = "0", ClampMax = "5", Units = "s"))
    float CloseDelayPerCm = 0.2f;
    /** Closing time constant; a wider and longer opening closes slower (a lasting gape after a large size). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Closing", meta = (ClampMin = "0.05", ClampMax = "20", Units = "s"))
    float CloseSeconds = 0.9f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Closing", meta = (ClampMin = "0", ClampMax = "10", Units = "s"))
    float CloseSecondsPerCm = 0.6f;
    /** Seconds of being held open that double the closing time (a long stretch relaxes the walls). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Closing", meta = (ClampMin = "1", ClampMax = "600", Units = "s"))
    float RelaxSeconds = 40.0f;
    /** Contractions: the walls clench around the shaft on entering and now and then while it is inside. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Contraction", meta = (ClampMin = "0", ClampMax = "0.5"))
    float ClenchAmount = 0.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Contraction", meta = (ClampMin = "0.05", ClampMax = "2", Units = "s"))
    float ClenchSeconds = 0.4f;
    /** Mean pause between contractions while a shaft is inside (0: only on entering). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Contraction", meta = (ClampMin = "0", ClampMax = "60", Units = "s"))
    float ClenchEverySeconds = 5.0f;
    /** Resistance: the hand holds the shaft like a spring; the shaft slides back in the grip by at most this much
     *  (cm) against a tight ring, then it is forced in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Resistance", meta = (ClampMin = "0", ClampMax = "15", Units = "cm"))
    float MaxLagCm = 6.0f;
    /** How hard the rings push back, in cm of hand lead per unit of ring stretch (tightness x radius x its growth,
     *  over ResistanceRadiusCm): a widening part (head, knot, bead, the tip of a thick shaft) is held at a tight ring,
     *  a narrowing part is drawn in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Resistance", meta = (ClampMin = "0", ClampMax = "20"))
    float ResistanceGain = 4.0f;
    /** Rubbing of the stretched walls along the shaft, relative to ResistanceGain (sliding needs this much more lead;
     *  a resting shaft a quarter more to start moving). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Resistance", meta = (ClampMin = "0", ClampMax = "1"))
    float FrictionShare = 0.05f;
    /** Pulled back, the tight walls drag the entrance outward and hold the shaft (this share of the push resistance). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Resistance", meta = (ClampMin = "0", ClampMax = "1"))
    float SuctionShare = 0.5f;
    /** Ring stretch is measured in units of this radius (a shaft of this radius at a ring of tightness 1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Resistance", meta = (ClampMin = "0.5", ClampMax = "10", Units = "cm"))
    float ResistanceRadiusCm = 3.0f;
    /** A free shaft slides over the body surface instead of passing through it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
    bool bShaftBodyCollision = true;
    /** A reaction cue for every this much travel inside (rate limited by ContactSettings). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Reaction", meta = (ClampMin = "1", ClampMax = "50", Units = "cm"))
    float ReactionTravelCm = 8.0f;
    /** Reaction weight held while engaged, scaled up with the stretch. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Reaction", meta = (ClampMin = "0", ClampMax = "1"))
    float HoldReactionWeight = 0.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Haptics", meta = (ClampMin = "0", ClampMax = "1"))
    float HapticCapturePulse = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Haptics", meta = (ClampMin = "0", ClampMax = "1"))
    float HapticBase = 0.12f;
    /** Insertion speed at which vibration reaches HapticMax. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Haptics", meta = (ClampMin = "1", Units = "cm/s"))
    float HapticFullSpeed = 40.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Haptics", meta = (ClampMin = "0", ClampMax = "1"))
    float HapticMax = 0.85f;
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaCharacterCapabilities
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capabilities")
    bool bBlink = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capabilities")
    bool bGaze = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capabilities")
    bool bFacialReactions = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capabilities")
    bool bReactionAnimations = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capabilities")
    bool bContacts = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capabilities")
    bool bSecondaryPhysics = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capabilities")
    bool bLocalSprings = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capabilities")
    bool bSound = true;
};

/** Editor-editable contract for a replaceable character. Hard references retain its cooked resources. */
UCLASS(BlueprintType)
class GRATIAVR_API UGratiaCharacterProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UGratiaCharacterProfile();
    virtual FPrimaryAssetId GetPrimaryAssetId() const override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
    FName ProfileId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
    FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources")
    TObjectPtr<USkeletalMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources")
    TObjectPtr<UPhysicsAsset> PhysicsAsset;
    /** Leave empty for native single-node playback. A custom class must implement its own pose/response contract. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources")
    TSubclassOf<UAnimInstance> AnimationClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
    TObjectPtr<UAnimSequence> Idle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
    TObjectPtr<UAnimSequence> Arms;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
    TObjectPtr<UAnimSequence> Head;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
    TObjectPtr<UAnimSequence> ReactSoft;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
    TObjectPtr<UAnimSequence> ReactBright;
    /** Contact zone name -> authored response. Default is optional restrained fallback. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
    TMap<FName, TObjectPtr<UAnimSequence>> ReactionClips;
    /** A fast touch (above ContactSettings.StrongReactionSpeedCmPerSecond) plays this instead of the zone clip. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
    TObjectPtr<UAnimSequence> StrongReactionClip;
    /** Mood name (Calm, Cheerful, Reserved) -> reaction played for every zone in that mood; missing moods use zone routing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
    TMap<FName, TObjectPtr<UAnimSequence>> MoodReactionClips;
    /** Selectable after Idle/Arms/Head in the Pose menu item; their own face curves replace the procedural blink. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
    TArray<FGratiaPerformanceClip> PerformanceClips;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
    bool bAuthoredReactionFacialCurves = false;
    /** Optional authored acknowledgement. Empty uses the presenter's short procedural chime. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation|Sound")
    TObjectPtr<USoundBase> DefaultReactionSound;
    /** Contact zone name -> sound. Optional Default key precedes DefaultReactionSound. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation|Sound")
    TMap<FName, TObjectPtr<USoundBase>> ReactionSounds;
    /** Speech bubble and voice per reaction (FGratiaReactionLine). Empty: zone sounds and no bubble. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation|Lines", meta = (TitleProperty = "Text"))
    TArray<FGratiaReactionLine> ReactionLines;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
    TMap<FName, FName> SemanticBones;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
    TMap<FName, FName> SemanticMorphs;
    /** Character forward direction in actor-local space; Gratia uses +Y and the mannequin uses +X. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
    FVector ForwardAxis = FVector::RightVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
    FVector UpAxis = FVector::UpVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
    float MaxHeadYawDegrees = 28.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
    float MaxHeadPitchDegrees = 12.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze", meta = (ClampMin = "0.0"))
    float GazeInterpSpeed = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact")
    TArray<FGratiaContactZoneDefinition> ContactZones;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
    TArray<FGratiaCollisionProxyDefinition> CollisionProxies;
    /** Skin-fitted capsules (editor: GratiaSoftBodySetupLibrary::MeasureBodySurface). When set,
     *  the palm collides with them instead of CollisionProxies, and grips wrap around them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (TitleProperty = "Bone"))
    TArray<FGratiaSurfaceCapsule> BodySurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
    FGratiaHandSurfaceSettings HandSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact")
    FGratiaContactSettings ContactSettings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics")
    FGratiaHandPhysicsSettings HandPhysics;

    /** Soft body parts (KawaiiPhysics), hand contact, grab, finger conform and haptics. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Soft Body")
    FGratiaSoftBodySettings SoftBody;
    /** Body channels a jointed shaft (AGratiaPenetrator) enters: wall bones, stretch, reactions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Penetration")
    FGratiaPenetrationSettings Penetration;
    /** Hair, clothing decor, ears and tails as KawaiiPhysics spring chains (menu groups 1/2/4). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion|Spring Chains", meta = (TitleProperty = "Name"))
    TArray<FGratiaSpringChain> SpringChains;
    /** Spring chains meet each hand as one sphere around palm and fingers (no finger spheres:
     *  hair is not combed by every finger, and it costs 2 limits per chain instead of 24). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion|Spring Chains", meta = (ClampMin = "1", ClampMax = "15", Units = "cm"))
    float SpringHandRadiusCm = 6.0f;
    /** A trigger grabs the nearest spring bone within this gap of the hand sphere. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion|Spring Chains", meta = (ClampMin = "0", ClampMax = "20", Units = "cm"))
    float SpringGrabRadiusCm = 2.0f;
    /** Spring chains collide only with BodySurface capsules at least this thick (head, neck,
     *  torso, shoulders, hips): a simplified body without hands and forearms. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion|Spring Chains", meta = (ClampMin = "0", ClampMax = "20", Units = "cm"))
    float SpringMinColliderRadiusCm = 4.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion")
    TArray<FGratiaSecondaryBoneDefinition> SecondaryBones;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion")
    TArray<FGratiaSecondaryGroupSettings> SecondaryGroups;
    /** Runtime indices 0/1/2 correspond to Low/Medium/High. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
    TArray<FGratiaQualityProfile> QualityProfiles;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capabilities")
    FGratiaCharacterCapabilities Capabilities;

    /** Zero disables a model-specific regression assertion. These are never universal model requirements. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0"))
    int32 ExpectedBoneCount = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0"))
    int32 ExpectedMorphCount = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0"))
    int32 ExpectedPhysicsBodyCount = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0"))
    int32 ExpectedConstraintCount = 0;
    /** Opt-in animation acceptance contract; another model may legitimately move its root/feet. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
    bool bRequirePlantedIdle = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0", Units = "cm"))
    float MaxIdleFootDriftCm = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0", Units = "deg"))
    float MaxIdleFootRotationDegrees = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0", Units = "cm"))
    float MaxIdleRootDriftCm = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0", Units = "deg"))
    float MaxIdleRootRotationDegrees = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "0.1", Units = "cm"))
    float MaxPhysicsTargetDeviationCm = 35.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary Motion", meta = (ClampMin = "0.01", Units = "s"))
    float PhysicsSafetyCheckSeconds = 0.25f;
    /** Maximum world-space AABB side for a secondary body at actor scale 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.1", Units = "cm"))
    float MaxSecondaryCollisionSizeCm = 60.0f;

    UFUNCTION(BlueprintPure, Category = "Character Profile")
    FName ResolveBone(FName Semantic) const;
    UFUNCTION(BlueprintPure, Category = "Character Profile")
    FName ResolveMorph(FName Semantic) const;
    UFUNCTION(BlueprintPure, Category = "Character Profile")
    FGratiaQualityProfile GetQualitySettings(int32 Quality) const;
    UFUNCTION(BlueprintPure, Category = "Character Profile")
    FGratiaSecondaryGroupSettings GetSecondaryGroupSettings(uint8 Group) const;
    UFUNCTION(BlueprintCallable, Category = "Character Profile")
    bool ValidateProfile(TArray<FString>& Errors, TArray<FString>& Warnings) const;

    const FGratiaSecondaryBoneDefinition* FindSecondaryBone(FName Bone) const;
};
