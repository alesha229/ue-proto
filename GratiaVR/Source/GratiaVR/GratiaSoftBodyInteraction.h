#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaSoftBodyInteraction.generated.h"

class AGratiaPreviewCharacter;
class UGratiaCharacterProfile;

/**
 * Hands against KawaiiPhysics soft parts (VRChat PhysBones/Contacts style).
 * Contact zones follow the simulated bones; palm and finger spheres collide with the
 * simulated tips, a trigger grabs a tip with a spring, and controller vibration grows
 * with penetration depth. Reactions/contacts remain owned by UGratiaInteraction.
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaSoftBodyInteraction : public UActorComponent
{
    GENERATED_BODY()

public:
    struct FZone
    {
        FName Chain;
        FName Bone;
        int32 ChainIndex = INDEX_NONE;
        FVector Center = FVector::ZeroVector;
        FVector Tip = FVector::ZeroVector;
        float Radius = 0.0f;
        /** Forward axis of the soft bone in its own space. */
        FVector Axis = FVector::XAxisVector;
        /** Soft bone world rotation (squash direction is kept in bone space). */
        FQuat Rotation = FQuat::Identity;
        /** Soft bone origin: the squash scales the part about this point. */
        FVector Pivot = FVector::ZeroVector;
    };

    UGratiaSoftBodyInteraction();

    /** Visible: proxy-constrained palm centre. Raw: controller target of the palm centre.
     *  Grab: max of trigger and grip. Fingers: world finger points of the visible hand.
     *  PoseOwned: the body-surface helper places the hand (cup/wrap); the hand is not moved
     *  toward the controller, and a grabbed part follows the controller (no hand/part loop).
     *  SqueezeBone/Squeeze: a cupping hand squeezes that soft bone (0..1, trigger/grip). */
    void SubmitHand(bool bLeft, const FVector& Visible, const FVector& Raw, bool bAllowed, float Delta, float Grab,
        const TArray<FVector>& Fingers, bool bPoseOwned = false, FName SqueezeBone = NAME_None, float Squeeze = 0.0f);
    void ClearHands();
    UFUNCTION(BlueprintCallable, Category = "Soft Body") void ResetSoftBody();

    bool IsEnabled() const;
    bool HasPress(bool bLeft) const { return Hands[bLeft ? 0 : 1].bPress; }
    FVector GetPressPoint(bool bLeft) const { return Hands[bLeft ? 0 : 1].Press; }
    float GetDepthCm(bool bLeft) const { return Hands[bLeft ? 0 : 1].DepthCm; }
    float GetHapticAmplitude(bool bLeft) const { return Hands[bLeft ? 0 : 1].Amplitude; }
    float GetHapticFrequency(bool bLeft) const { return Hands[bLeft ? 0 : 1].Frequency; }
    FName GetGrabbedBone(bool bLeft) const { return Hands[bLeft ? 0 : 1].GrabBone; }
    const TArray<FZone>& GetZones() const { return Zones; }
    /** World-space spheres the fingers wrap around: soft zones and body colliders near Point.
     *  Soft zones shrink by ZoneShrinkCm so a squeezing hand sinks into them. */
    void GetConformSpheres(const FVector& Point, float Range, TArray<FVector4>& OutWorld, float ZoneShrinkCm = 0.0f) const;
    /** Palm/finger spheres written to the press collection last tick (diagnostics/QA). */
    int32 GetPressSphereCount() const { return PressSpheres; }
    /** Current squeeze of a soft bone (0 rest, up to SquashAmount). */
    float GetSquash(FName Bone) const { const FVector2D* State = Squash.Find(Bone); return State ? float(State->X) : 0.0f; }
    /** Bone-space scale of a soft bone: compressed along the press direction, bulging across it. */
    FVector GetSquashScale(FName Bone) const;

    /** Surface press deformation of skin and clothing (material offset); QA can switch it off. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soft Body")
    bool bPressDeformation = true;
    bool HasFault() const { return bFault; }
    bool RunChecks(FString& Failure);
    FString GetDiagnostics() const;
    /** Updates zone positions from the current bone transforms (also used by QA). */
    void UpdateZones();

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

private:
    struct FHand
    {
        bool bReady = false;
        bool bPress = false;
        bool bGrabArmed = false;
        bool bTouching = false;
        FVector Visible = FVector::ZeroVector;
        FVector Raw = FVector::ZeroVector;
        FVector Press = FVector::ZeroVector;
        TArray<FVector> Fingers;
        float DepthCm = 0.0f;
        float Amplitude = 0.0f;
        float Frequency = 0.0f;
        float PulseRemaining = 0.0f;
        double SubmitTime = -1.0;
        FName GrabBone;
        /** Spring chain of a grabbed hair/decor bone; INDEX_NONE for a soft zone. */
        int32 GrabSpringChain = INDEX_NONE;
        FVector GrabOffset = FVector::ZeroVector;
        /** Point the grab follows: the pressed hand, or the controller when the pose is owned. */
        FVector Grabber = FVector::ZeroVector;
        FName SqueezeBone;
        float Squeeze = 0.0f;
    };

    void SetFault(const FString& Reason);
    void ReleaseGrab(FHand& Hand) { Hand.GrabBone = NAME_None; Hand.GrabSpringChain = INDEX_NONE; }
    /** Nearest simulated spring bone (hair, decor) within SpringGrabRadiusCm of the hand sphere. */
    void GrabSpringBone(FHand& Hand, const FHand& Other, const FVector& Press, const FVector& Visible, const TArray<FVector>& Fingers);
    /** The one sphere a hand meets spring chains with: between the palm and the finger centroid. */
    static FVector SpringHandCenter(const FVector& Palm, const TArray<FVector>& Fingers);
    void PushToAnimation(bool bReset);
    void PushPress();

    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<const UGratiaCharacterProfile> LastProfile;
    TArray<FZone> Zones;
    TArray<TPair<int32, FVector>> ColliderOffsets; // bone index, bone-space centre
    TArray<float> ColliderRadii;
    FHand Hands[2];
    /** Collision spheres a hand last showed the animation (world space), and how far they have grown in (0..1). */
    struct FHandSpheres
    {
        TArray<FVector4> Soft;
        FVector4 Spring = FVector4(0.0, 0.0, -1.0e6, 0.0);
        float Presence = 0.0f;
    };
    FHandSpheres ShownSpheres[2];
    /** Palm and finger spheres per hand (the animation has 24 soft slots: half per hand). */
    static constexpr int32 SoftSlotsPerHand = 12;
    static constexpr float SphereGrowSeconds = 0.12f;
    static constexpr float SphereShrinkSeconds = 0.08f;
    bool bWasEnabled = false;
    bool bFault = false;
    FString FaultReason;
    double NextDiagnosticTime = 0.0;
    uint64 ZonesFrame = 0;
    int32 PressSpheres = 0;
    /** Squeeze spring per soft bone: amount and its velocity. */
    TMap<FName, FVector2D> Squash;
    /** Press direction per soft bone, in bone space (unit; the last one is kept for the spring-back). */
    TMap<FName, FVector> SquashDirection;
    /** 0: a press flattens the part; 1: a cupping hand squeezes it like a ball (blended). */
    TMap<FName, float> SquashGrip;
    void UpdateSquash(float Delta);
    bool bPressCleared = false;
};
