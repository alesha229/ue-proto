#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include <atomic>
#include "GratiaHandAnimInstance.generated.h"

class UAnimSequence;
class UMirrorDataTable;

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

    UPROPERTY(Transient) TObjectPtr<UAnimSequence> OpenPose;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> ClosedPose;
    UPROPERTY(Transient) TObjectPtr<UMirrorDataTable> MirrorTable;
    bool bLeftHand = false;

    /** Controller curl targets in [0,1]: thumb, index, middle, ring, pinky. */
    float FingerInput[NumFingers] = {0, 0, 0, 0, 0};
    /** World-space contact spheres (XYZ centre, W radius) used to stop each finger. */
    TArray<FVector4> ConformSpheres;
    float FingerRadiusCm = 1.1f;
    float ConformMarginCm = 0.2f;
    bool bConform = true;

    /** Current (smoothed) curl and the surface cap of every finger. */
    float FingerAlpha[NumFingers] = {0, 0, 0, 0, 0};
    float FingerCap[NumFingers] = {1, 1, 1, 1, 1};

    /** Loads the XR template hand poses (right-hand authored; left mirrors). Editable
     *  per project by assigning OpenPose/ClosedPose/MirrorTable before play. */
    bool LoadDefaultPoses();
    bool HasConformSamples() const { return bSamplesReady.load(); }
    /** World-space distal finger joints and tips at the current curl (empty until sampled). */
    void GetFingerPoints(TArray<FVector>& OutWorld) const;
    FString GetDiagnostics() const;

    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    // Written once by the anim proxy, read by the game thread after bSamplesReady.
    FVector Samples[NumFingers][NumSamples + 1][PointsPerFinger];
    std::atomic<bool> bSamplesReady{false};

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    float CapFinger(int32 Finger, const FTransform& Component) const;
};
