#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaRuntimeVerification.generated.h"

class AGratiaStage1Runtime;

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

private:
    bool bSmokeTest = false;
    bool bSelfTest = false;
    bool bTestChecksDone = false;
    bool bTestFailed = false;
    bool bRecenterTestPending = false;
    bool bCharacterMotionChecksDone = false;
    bool bCharacterBonesRemainFinite = true;
    float TestElapsedSeconds = 0.0f;
    TArray<FTransform> InitialCharacterBones;
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
    FString PerfRows;

    AGratiaStage1Runtime& GetRuntime() const;
    void RunWorldChecks();
    void RunCharacterWorldChecks();
    void SampleCharacterAnimation(bool bFinish);
    void RunSelfChecks();
    void TestCheck(bool bPassed, const TCHAR* Description);
};
