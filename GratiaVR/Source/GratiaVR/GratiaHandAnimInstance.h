#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include <atomic>
#include "GratiaHandAnimInstance.generated.h"

class UAnimSequence;
class UMirrorDataTable;

/** Palm of the hand mesh in its component space: centre, palm-side normal, finger direction. */
struct FGratiaPalmFrame
{
    FVector Point = FVector::ZeroVector;
    FVector Normal = FVector::ForwardVector;
    FVector Finger = FVector::RightVector;
    /** Open-pose finger joints and tips (all fingers) in component space, for resting placement. */
    TArray<FVector> RestPoints;
};

/** World-space capsule the fingers wrap around (segment A-B, radius cm). */
struct FGratiaConformCapsule
{
    FVector A = FVector::ZeroVector;
    FVector B = FVector::ZeroVector;
    float Radius = 0.0f;
    FName Bone;
    /** Torso slice (wraps around an explicit axis): adjacent slices form one surface. */
    bool bSlice = false;
};

/**
 * Native VR hand pose: per-finger blend between an open and a closed pose, driven by
 * controller grip/trigger and capped where a finger would enter a contact sphere, so
 * the hand wraps around soft parts (procedural grip, as in Lone Echo / Auto Hand).
 * The closed/open poses are authored for the right hand; the left hand mirrors them.
 */
UCLASS(Transient)
class GRATIAVR_API UGratiaHandAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    static constexpr int32 NumFingers = 5;      // thumb, index, middle, ring, pinky
    static constexpr int32 NumSamples = 12;     // curl samples used for surface conform
    static constexpr int32 PointsPerFinger = 4; // three joints and an extrapolated tip
    /** Fingers may straighten this far past the relaxed open pose (negative curl) to lie on a
     *  wide surface; the thumb goes past its extended pose into the palm plane. */
    static float Extension(int32 Finger) { return Finger == 0 ? 1.0f : 0.4f; }
    /** Curl value of conform sample K (sample 0 is the most extended). */
    static float CurlAt(int32 Finger, int32 K) { return -Extension(Finger) + (1.0f + Extension(Finger)) * float(K) / NumSamples; }

    UPROPERTY(Transient) TObjectPtr<UAnimSequence> OpenPose;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> ClosedPose;
    /** Closed pose of the index finger (the grasp pose keeps the index straight). */
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> IndexClosedPose;
    /** Thumb curl runs from this extended pose (0) to the relaxed open pose (1), so the thumb
     *  moves out of the way when it would enter the body. */
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> ThumbOpenPose;
    UPROPERTY(Transient) TObjectPtr<UMirrorDataTable> MirrorTable;
    bool bLeftHand = false;

    /** Controller curl targets in [0,1]: thumb, index, middle, ring, pinky. */
    float FingerInput[NumFingers] = {0, 0, 0, 0, 0};
    /** World-space contact spheres (XYZ centre, W radius) used to stop each finger. */
    TArray<FVector4> ConformSpheres;
    /** World-space body capsules (limbs, waist) used to stop each finger. */
    TArray<FGratiaConformCapsule> ConformCapsules;
    float FingerRadiusCm = 1.1f;
    float ConformMarginCm = 0.2f;
    bool bConform = true;

    /** Current (smoothed) curl and the surface cap of every finger (may be negative: straightened). */
    float FingerAlpha[NumFingers] = {0, 0, 0, 0, 0};
    float FingerCap[NumFingers] = {1, 1, 1, 1, 1};

    /** Loads the XR template hand poses (right-hand authored; left mirrors). Editable
     *  per project by assigning OpenPose/ClosedPose/MirrorTable before play. */
    bool LoadDefaultPoses();
    bool HasConformSamples() const { return bSamplesReady.load(); }
    /** World-space distal finger joints and tips at the current curl (empty until sampled). */
    void GetFingerPoints(TArray<FVector>& OutWorld) const;
    /** World-space palm centre (between wrist and knuckles); false until sampled. */
    bool GetPalmPoint(FVector& OutWorld) const;
    /** Palm frame in the hand component space (from the open/closed poses); false until sampled. */
    bool GetPalmFrame(FGratiaPalmFrame& Out) const;
    FString GetDiagnostics() const;

    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    // Written once by the anim proxy, read by the game thread after bSamplesReady.
    FVector Samples[NumFingers][NumSamples + 1][PointsPerFinger];
    /** Open-pose wrist (hand_*) and palm (palm_*) bone positions; palm falls back to knuckles. */
    FVector WristSample = FVector::ZeroVector;
    FVector PalmSample = FVector::ZeroVector;
    bool bPalmBone = false;
    std::atomic<bool> bSamplesReady{false};

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    float CapFinger(int32 Finger, const FTransform& Component) const;
};
