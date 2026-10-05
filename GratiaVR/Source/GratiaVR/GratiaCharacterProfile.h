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
    float PalmRadiusCm = 4.0f;
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
    float PressPalmRadiusCm = 2.5f;
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
    /** Volumetric squeeze: the soft bone compresses along its forward axis by up to this
     *  fraction at full press depth (zone radius) and springs back with a wobble. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0", ClampMax = "0.8"))
    float SquashAmount = 0.45f;
    /** Sideways spread while compressed (0 none, 1 volume preserving). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0", ClampMax = "1"))
    float SquashBulge = 0.5f;
    /** Hand sphere size for pushing the KawaiiPhysics bone; below 1 the surface yields
     *  (press dent and squash) before the whole soft part swings away. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body|Press", meta = (ClampMin = "0", ClampMax = "1"))
    float HandPushFraction = 0.5f;
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
    bool bAuthoredReactionFacialCurves = false;
    /** Optional authored acknowledgement. Empty uses the presenter's short procedural chime. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation|Sound")
    TObjectPtr<USoundBase> DefaultReactionSound;
    /** Contact zone name -> sound. Optional Default key precedes DefaultReactionSound. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation|Sound")
    TMap<FName, TObjectPtr<USoundBase>> ReactionSounds;

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
