#pragma once
#include "CoreMinimal.h"

struct FPoseContext;
struct FAnimNode_KawaiiPhysics;

/** Component-space translation of one bone; unlisted children follow. */
struct FGratiaPlayBoneOffset
{
    FName Bone;
    FVector OffsetCS = FVector::ZeroVector;
};

/** Two-bone IK goal (wrist/ankle grab, ground adaptation, planted feet). */
struct FGratiaPlayLimbGoal
{
    FName Root, Mid, End;
    FVector GoalCS = FVector::ZeroVector;
    /** Bend hint in component space when the limb is straight. */
    FVector PoleCS = FVector::ZeroVector;
    float Weight = 0.0f;
    /** Goal is the end's own position before the body offsets (feet stay planted while the pelvis moves). */
    bool bPlant = false;
    /** Added to the goal after planting (ground lift of a planted foot). */
    FVector ExtraCS = FVector::ZeroVector;
};

/** KawaiiPhysics multipliers per group (0 soft body, 1 hair, 2 clothing decor, 4 ears/tail) and a world-space kick. */
struct FGratiaPlaySoftTuning
{
    bool bEnabled = false;
    float WorldDamping[5] = { 1, 1, 1, 1, 1 };
    float Damping[5] = { 1, 1, 1, 1, 1 };
    float Stiffness[5] = { 1, 1, 1, 1, 1 };
    FVector KickWS[5] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
};

/** Game-thread input of the interaction layer for the character's anim proxy (copied in PreUpdate). */
struct FGratiaPlayPoseInput
{
    /** Pelvis/chest pull and ground lift: before the limbs and the soft body. */
    TArray<FGratiaPlayBoneOffset> BodyOffsets;
    TArray<FGratiaPlayLimbGoal> Limbs;
    /** Fabric that keeps where the hand left it (skirt lifted, strap slid): after the soft body. */
    TArray<FGratiaPlayBoneOffset> LateOffsets;
    FGratiaPlaySoftTuning Soft;
    void Reset() { BodyOffsets.Reset(); Limbs.Reset(); LateOffsets.Reset(); Soft = FGratiaPlaySoftTuning(); }
};

namespace GratiaPlayPose
{
    /** Body offsets, then limb IK (worker thread, inside the proxy's Evaluate). Returns bones changed. */
    GRATIAVR_API int32 ApplyBody(FPoseContext& Output, const FGratiaPlayPoseInput& Input);
    /** Late fabric offsets (after the soft body). Returns bones changed. */
    GRATIAVR_API int32 ApplyLate(FPoseContext& Output, const FGratiaPlayPoseInput& Input);

    /** Scales a KawaiiPhysics node's settings from the values the profile gave it; restores them when disabled. */
    struct GRATIAVR_API FSoftTuner
    {
        void Apply(FAnimNode_KawaiiPhysics& Node, uint8 Group, const FGratiaPlaySoftTuning& Tuning);
    private:
        struct FEntry { float Damping = 0, Stiffness = 0, WorldLocation = 0, WorldRotation = 0; float Written[4] = {}; bool bWritten = false; };
        TMap<const void*, FEntry> Entries;
    };
}
