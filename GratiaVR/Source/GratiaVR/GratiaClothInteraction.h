#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaClothInteraction.generated.h"

class AGratiaPreviewCharacter;
class UGratiaCharacterProfile;

/** Hands interact with the imported source cages through native Chaos Cloth.
 * Samples are consumed before the mesh can start its next parallel cloth task. */
UCLASS(ClassGroup = Gratia, meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaClothInteraction : public UActorComponent
{
    GENERATED_BODY()

public:
    UGratiaClothInteraction();
    /** Visible is the proxy-constrained hand; Raw is the controller target. The cloth
     *  collider is placed at most ClothSettings.SoftPressDepthCm from Visible toward Raw. */
    void SubmitHand(bool bLeft, const FVector& Visible, const FVector& Raw, bool bAllowed, float Delta, float Trigger);
    void ClearHands();
    UFUNCTION(BlueprintCallable, Category = "Source Cloth") void ResetCloth();
    bool HasFault() const { return bFault; }
    bool RunChecks(FString& Failure);
    FString GetDiagnostics() const;
    float GetMaxDisplacementCm() const { return MaxDisplacementCm; }
    int32 GetDynamicParticleCount() const { return DynamicParticleCount; }
    int32 GetGrabbedParticle(bool bLeft) const { return Hands[bLeft ? 0 : 1].ParticleIndex; }
    int32 GetActiveHandCount() const { return ActiveHandCount; }
    const TMap<FName, float>& GetCageDisplacements() const { return CageDisplacements; }
    /** Snapshot from the last completed solver update, ordered by asset then particle. */
    void GetParticleSnapshot(TArray<FVector>& Positions) const { Positions = ParticleSnapshot; }
    bool GetCageParticleSnapshot(FName AssetName, TArray<FVector>& Positions) const;
    bool GetCageInverseMassSnapshot(FName AssetName, TArray<float>& InverseMasses) const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

private:
    struct FClothHand
    {
        FVector Previous = FVector::ZeroVector;
        FVector Current = FVector::ZeroVector;
        FVector GrabOffset = FVector::ZeroVector;
        double SubmitTime = -1;
        float Delta = 0;
        int32 InstanceIndex = INDEX_NONE;
        int32 ClothId = INDEX_NONE;
        int32 ParticleRangeId = INDEX_NONE;
        int32 ParticleIndex = INDEX_NONE;
        bool bReady = false;
        bool bPending = false;
        bool bGrabArmed = false;
        bool bGrabPressed = false;
        void ReleaseGrab()
        {
            InstanceIndex = ClothId = ParticleRangeId = ParticleIndex = INDEX_NONE;
            GrabOffset = FVector::ZeroVector;
        }
    };
    bool IsEnabled() const;
    void ClearExternalCollisions();
    void StopCloth(bool bReset);
    void SetFault(const FString& Reason);
    void UpdateNativeCloth(float Delta, bool bApplyHands);

    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<UGratiaCharacterProfile> LastProfile;
    FClothHand Hands[2];
    TArray<FVector> ParticleSnapshot;
    TMap<FName, TArray<FVector>> CageParticleSnapshots;
    TMap<FName, TArray<float>> CageInverseMassSnapshots;
    TMap<FName, float> CageDisplacements;
    int32 DynamicParticleCount = 0;
    int32 ActiveHandCount = 0;
    float PressDepthCm[2] = {0, 0};
    int32 ManagedCageCount = 0;
    float MaxDisplacementCm = 0;
    float MeanDisplacementCm = 0;
    double NextDiagnosticTime = 0;
    bool bRunning = false;
    bool bFault = false;
    FString FaultReason;
};
