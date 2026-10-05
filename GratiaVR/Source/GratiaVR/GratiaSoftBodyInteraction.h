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
    };

    UGratiaSoftBodyInteraction();

    /** Visible: proxy-constrained hand. Raw: controller target. Fingers: world finger points. */
    void SubmitHand(bool bLeft, const FVector& Visible, const FVector& Raw, bool bAllowed, float Delta, float Trigger,
        const TArray<FVector>& Fingers);
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
    /** World-space spheres the fingers wrap around: soft zones and body colliders near Point. */
    void GetConformSpheres(const FVector& Point, float Range, TArray<FVector4>& OutWorld) const;
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
        FVector GrabOffset = FVector::ZeroVector;
    };

    void SetFault(const FString& Reason);
    void ReleaseGrab(FHand& Hand) { Hand.GrabBone = NAME_None; }
    void PushToAnimation(bool bReset);

    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<const UGratiaCharacterProfile> LastProfile;
    TArray<FZone> Zones;
    TArray<TPair<int32, FVector>> ColliderOffsets; // bone index, bone-space centre
    TArray<float> ColliderRadii;
    FHand Hands[2];
    bool bWasEnabled = false;
    bool bFault = false;
    FString FaultReason;
    double NextDiagnosticTime = 0.0;
    uint64 ZonesFrame = 0;
};
