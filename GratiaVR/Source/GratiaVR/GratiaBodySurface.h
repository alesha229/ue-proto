#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaBodySurface.generated.h"

class AGratiaPreviewCharacter;
class UGratiaCharacterProfile;
struct FGratiaContactShape;
struct FGratiaPalmFrame;
struct FGratiaConformCapsule;

/** Nearest point of the character's body surface to a hand point (world space, cm). */
struct FGratiaSurfaceHit
{
    FName Bone;
    FVector Point = FVector::ZeroVector;   // on the surface
    FVector Normal = FVector::UpVector;    // outward
    FVector Axis = FVector::ZeroVector;    // capsule segment direction (zero for spheres)
    FVector WrapAxis = FVector::ZeroVector; // axis a gripping hand wraps around
    FVector SegmentA = FVector::ZeroVector, SegmentB = FVector::ZeroVector;
    bool bSlice = false;
    float Radius = 0.0f;
    float Gap = 0.0f;                      // query point to surface, negative inside
    bool bSoftZone = false;
};

/**
 * The character's touchable body surface for hands: skin-fitted capsules from the profile
 * (limbs, waist, torso) plus the soft-body zones. Provides the palm collider shapes, the
 * nearest-surface query, and the hand poses that lean onto the surface or wrap around a part.
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaBodySurface : public UActorComponent
{
    GENERATED_BODY()

public:
    UGratiaBodySurface();

    bool HasSurface() const;
    /** Nearest surface within MaxGapCm of Point (gap measured to the surface). Grip queries
     *  skip parts that are not grip targets (head, neck, soft parts). */
    bool FindNearest(const FVector& Point, float MaxGapCm, FGratiaSurfaceHit& Out, bool bIncludeSoftZones = true, bool bGripTargetsOnly = false) const;
    /** Capsule hit for a named bone (QA and diagnostics). */
    bool FindOnBone(FName Bone, const FVector& Point, FGratiaSurfaceHit& Out) const;
    /** World segment and radius of a bone's capsule. */
    bool GetBoneCapsule(FName Bone, FVector& A, FVector& B, float& Radius) const;
    /** Palm collider obstacles: every body capsule and soft zone expanded by ExtraRadiusCm. */
    void GatherContactShapes(float ExtraRadiusCm, TArray<FGratiaContactShape>& Out) const;
    /** Shapes the fingers stop on near Point: body capsules plus soft zone spheres. */
    void GatherConformShapes(const FVector& Point, float RangeCm, TArray<FVector4>& Spheres, TArray<FGratiaConformCapsule>& Capsules,
        float SoftZoneShrinkCm = 0.0f) const;

    /** Hand transform that wraps the palm around the hit part: palm on the surface, fingers
     *  across the part's axis (keeping the closer of the two directions). */
    /** The same soft-part hit squeezed by SinkCm (smaller radius): the cupping palm sinks into it. */
    static FGratiaSurfaceHit Squeezed(const FGratiaSurfaceHit& Hit, float SinkCm)
    {
        FGratiaSurfaceHit Out = Hit;
        Out.Radius = FMath::Max(0.5f, Hit.Radius - FMath::Max(0.0f, SinkCm));
        return Out;
    }
    static FTransform SolveWrap(const FTransform& Hand, const FGratiaPalmFrame& Palm, const FGratiaSurfaceHit& Hit, float PalmThicknessCm,
        float FingerClearanceCm, TConstArrayView<FGratiaConformCapsule> Neighbours = {});
    /** Partly turns the palm toward the surface (Weight 0..1, limited angle) and draws it closer. */
    static FTransform LeanToSurface(const FTransform& Hand, const FGratiaPalmFrame& Palm, const FGratiaSurfaceHit& Hit, float Weight,
        float MaxDegrees, float PalmThicknessCm);
    /** Palm frame in world space for a hand transform. */
    static void PalmWorld(const FTransform& Hand, const FGratiaPalmFrame& Palm, FVector& Point, FVector& Normal, FVector& Finger);

    int32 GetCapsuleCount() const { return Resolved.Num(); }
    FString GetDiagnostics() const;

protected:
    virtual void BeginPlay() override;

private:
    struct FResolved { int32 Bone = INDEX_NONE; FName Name; FVector Start, End, Wrap; float Radius = 0; bool bGrip = true; bool bSoft = false; };
    /** Soft-part capsules replace the coarse soft-body zone spheres when present. */
    bool UseZoneSpheres() const;
    void Resolve() const;
    void WorldCapsule(const FResolved& Capsule, FVector& A, FVector& B, float& Radius) const;
    FVector WorldWrap(const FResolved& Capsule) const;
    static void Evaluate(const FVector& Point, const FVector& A, const FVector& B, float Radius, FGratiaSurfaceHit& Out);

    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    mutable TWeakObjectPtr<const UGratiaCharacterProfile> ResolvedProfile;
    mutable TArray<FResolved> Resolved;
};
