#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GratiaCharacterProfile.generated.h"

class USkeletalMesh;
class UAnimSequence;
class UAnimInstance;
class UPhysicsAsset;
class USoundBase;

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

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSourceClothCage
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth")
    FName AssetName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "1", ClampMax = "4"))
    uint8 Group = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0"))
    int32 ExpectedParticleCount = 0;
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSourceClothRegion
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth")
    FName AssetName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0"))
    int32 FirstParticle = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "1"))
    int32 ParticleCount = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth")
    FName AnchorSemantic;
};

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaClothSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth")
    bool bEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth")
    bool bAllowGrab = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0.1"))
    float HandRadiusCm = 6.0f;
    /** Rigid contact proxies stop the visible hand outside the soft surface. The cloth
     *  collider may follow the raw controller target at most this far past the visible hand. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float SoftPressDepthCm = 4.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0.1"))
    float MaxParticleOffsetCm = 8.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0.1"))
    float GrabRadiusCm = 6.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0.1"))
    float GrabBreakDistanceCm = 15.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0.1"))
    float MaxHandTravelCm = 35.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0.1"))
    float MaxHandSpeedCmPerSecond = 500.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0.0"))
    float GrabStiffness = 25.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float GrabVelocityBlend = 0.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Cloth", meta = (ClampMin = "0.1"))
    float MaxGrabSpeedCmPerSecond = 200.0f;
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact")
    FGratiaContactSettings ContactSettings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Physics")
    FGratiaHandPhysicsSettings HandPhysics;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Source Cloth")
    FGratiaClothSettings ClothSettings;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Source Cloth")
    TArray<FGratiaSourceClothCage> SourceClothCages;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Source Cloth")
    TArray<FGratiaSourceClothRegion> SourceClothRegions;
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
