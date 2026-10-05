#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaClothVerification.generated.h"

class AGratiaPreviewCharacter;
struct FGratiaSourceClothRegion;

/** Opt-in desktop integration proof for source cloth, independent of normal play. */
UCLASS()
class GRATIAVR_API UGratiaClothVerification : public UActorComponent
{
    GENERATED_BODY()

public:
    UGratiaClothVerification();
    bool OwnsSyntheticContactSamples() const { return bRequested && !bFinished; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

private:
    enum class EPhase : uint8 { Settle, Baseline, Approach, Push, Arm, Grab, Pull, Release,
        ReArm, ReGrab, Lost, HeldRecovery, FinishRegion, Reset, Disabled, Resumed };
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    EPhase Phase = EPhase::Settle;
    float Elapsed = 0;
    float PhaseSeconds = 0;
    int32 RegionIndex = 0;
    bool bRequested = false;
    bool bFinished = false;
    bool bFailed = false;
    bool bSavedContactTick = false;
    bool bSavedDemo = false;
    bool bSavedBodyMotion = false;
    int32 InitialCollisionProxyCount = INDEX_NONE;
    bool bSawReadyHand = false;
    bool bSawGrab = false;
    bool bSawPressure = false;
    bool bSawPull = false;
    int32 CandidateParticle = INDEX_NONE;
    FTransform HandVisual = FTransform::Identity;
    FVector HandOutside = FVector::ZeroVector;
    FVector HandTouch = FVector::ZeroVector;
    FVector ProbeForward = FVector::ZeroVector;
    TArray<FVector> BaselineParticlesLocal;
    TArray<FVector> BaselineClothDeltas;
    TArray<TArray<int32>> RegionRenderVertices;
    TArray<int32> ProtectedRenderVertices;
    TMap<int32, int32> ProtectedDominantBones;
    TArray<int32> UnaffectedRenderVertices;
    double PressureParticleCm = 0;
    double PressureRenderCm = 0;
    double PullParticleCm = 0;
    double PullRenderCm = 0;
    double MaxUnaffectedCm = 0;
    double MaxProtectedCm = 0;

    bool Check(bool bPass, const FString& Description);
    void Advance(EPhase Next);
    void Finish();
    bool BuildRenderCoverage();
    bool CaptureRenderDeltas(TArray<FVector>& Deltas);
    bool StartRegion();
    bool SelectReachableParticle();
    FTransform GetRegionAnchor() const;
    void SubmitSyntheticHand(const FVector& Target, float Delta, float Trigger, bool bTracked = true);
    void SampleRegionMotion(double& ParticleCm, double& RenderCm, bool bCaptureRender);
    const FGratiaSourceClothRegion* GetRegion() const;
};
