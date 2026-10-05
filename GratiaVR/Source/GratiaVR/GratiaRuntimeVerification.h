#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaRuntimeVerification.generated.h"

class AGratiaStage1Runtime;
class UGratiaCharacterProfile;
class UAnimSequence;

/**
 * Opt-in verification and measurement harness.
 * It is idle in normal gameplay and driven after the runtime updates tracked hands.
 * Keep synthetic inputs and acceptance assertions here, separate from player behavior.
 */
UCLASS()
class GRATIAVR_API UGratiaRuntimeVerification : public UActorComponent
{
    GENERATED_BODY()

public:
    UGratiaRuntimeVerification();
    void ConfigureFromCommandLine();
    void RunRequestedTests(float DeltaSeconds);
    void RunSoakAndMetrics(float DeltaSeconds);
    void ConfigureCaptureView();
    bool IsHandPhysicsQAActive() const { return bSelfTest && HandPhysicsQAPhase > 0 && HandPhysicsQAPhase < 7; }
    bool OwnsSyntheticContactSamples() const { return bClothQA || (SoakDuration > 0.0f && !bMetricsFinished); }

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    bool bSmokeTest = false;
    bool bClothQA = false;
    bool bSelfTest = false;
    bool bTestChecksDone = false;
    bool bTestFailed = false;
    bool bRecenterTestPending = false;
    bool bCharacterMotionChecksDone = false;
    bool bCharacterBonesRemainFinite = true;
    float TestElapsedSeconds = 0.0f;
    TArray<FTransform> InitialCharacterBones;
    TArray<FName> ObservedCharacterBones;
    TWeakObjectPtr<UGratiaCharacterProfile> ObservedCharacterProfile;
    FTransform InitialCharacterActor = FTransform::Identity;
    double MaxHeadMovementCm = 0.0;
    double MaxHeadMovementDegrees = 0.0;
    double MaxFootDriftCm = 0.0;
    double MaxFootDriftDegrees = 0.0;
    double MaxRootDriftCm = 0.0;
    double MaxRootDriftDegrees = 0.0;
    float InitialAnimationTime = 0.0f;
    bool bAnimationTimeAdvanced = false;
    float SoakDuration = 0.0f;
    float PerfDuration = 0.0f;
    float MetricsSeconds = 0.0f;
    float MetricsInterval = 0.0f;
    int32 SoakCycle = INDEX_NONE;
    bool bMetricsFinished = false;
    bool bSoakContactAvailabilityReported = false;
    bool bSoakReactionBaselineSet = false;
    int32 LastSoakReactionSerial = 0;
    int32 ActualSoakReactions = 0;
    FString PerfRows;
    bool bInputIntegrationStarted = false;
    int32 HandPhysicsQAPhase = 0;
    int32 HandPhysicsBaseline[2] = {};
    FName HandPhysicsQABone;
    int32 PhysicsProbeGroup = 1;
    float PhysicsProbeSeconds = 0;
    FName PhysicsProbeBone;
    FQuat PhysicsProbeBodyStart = FQuat::Identity, PhysicsProbeVisualStart = FQuat::Identity;
    double PhysicsProbeBodyDegrees = 0, PhysicsProbeVisualDegrees = 0;
    void RunPhysicsResponseProbe(float Delta);
    FVector HandPhysicsBefore = FVector::ZeroVector;
    FVector HandPhysicsTargets[2] = {FVector::ZeroVector, FVector::ZeroVector};
    void RunHandPhysicsIntegration();
    bool bInputIntegrationReleased = false;
    bool bInputIntegrationDone = false;
    bool bInputEventAccepted = false;
    bool bObservedKeyboardKey = false;
    bool bObservedMappedWalk = false;
    double ObservedInputTravelCm = 0.0;
    int32 InputIntegrationFrames = 0;
    FTransform InputOriginalPawn = FTransform::Identity;
    FString ReactionQAZone;
    FString ReactionQAExpectedClip;
    FString ReactionQACorrectivePrefix;
    bool bReactionQAEnabled = false;
    bool bReactionQAStarted = false;
    bool bReactionQAContactReceived = false;
    bool bReactionQAClipObserved = false;
    bool bReactionQAScreenshotRequested = false;
    bool bReactionQACompleted = false;
    float ReactionQASeconds = 0.0f;
    float ReactionQAScreenshotSeconds = 0.0f;
    float ReactionQACaptureTime = 0.8f;
    int32 ReactionQAZoneIndex = INDEX_NONE;
    int32 ReactionQAHandIndex = INDEX_NONE;
    int32 ReactionQAEventCount = 0;
    int32 ReactionQASerial = 0;
    TWeakObjectPtr<UAnimSequence> ReactionQAClip;
    TArray<FName> ReactionQAFacialCurves;
    TArray<FName> ReactionQACorrectiveCurves;
    float ReactionQACookedFacialPeak = 0.0f;
    float ReactionQACookedCorrectivePeak = 0.0f;
    float ReactionQAEvaluatedFacialPeak = 0.0f;
    float ReactionQAEvaluatedCorrectivePeak = 0.0f;
    float ReactionQAHeadRotationDegrees = 0.0f;
    bool bReactionQARequireCorrective = false;
    FVector ReactionQATouchOffset = FVector::ZeroVector;
    FString ReactionQAScreenshotPath;

    AGratiaStage1Runtime& GetRuntime() const;
    void RunWorldChecks();
    void RunCharacterWorldChecks();
    void SampleCharacterAnimation(bool bFinish);
    void RunSelfChecks();
    void RunInputIntegration();
    bool ValidateInputAssets(FString& Failure) const;
    void RunReactionResourceQA(float DeltaSeconds);
    void FinishReactionResourceQA();
    UFUNCTION()
    void OnReactionQAContact(FName ZoneName, int32 HandIndex, float HandSpeed, int32 Mood);
    void TestCheck(bool bPassed, const TCHAR* Description);
    void TestSkip(const TCHAR* Description) const;
};
