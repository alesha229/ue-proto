#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaSceneFlowVerification.generated.h"

class AGratiaStage1Runtime;
class UGratiaSceneDirector;
class UGratiaSceneLibrary;

/** Opt-in rendered integration test of the scene flow. Never feeds real controller tracking. */
UCLASS(ClassGroup = (Gratia))
class GRATIAVR_API UGratiaSceneFlowVerification : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaSceneFlowVerification();

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

private:
    enum class EPhase : uint8 { WaitLobby, LobbyCapture, Travel, Settle, SoundOff, Pause, Resume, Return, Finish };
    AGratiaStage1Runtime* Runtime() const;
    UGratiaSceneDirector* Director() const;
    void Check(bool bPass, const FString& Description);
    void Capture(const FString& Name);
    void ChangePhase(EPhase Next);
    void StartNextScene();
    void CheckSettings();
    void CheckErrors();
    void Complete();

    EPhase Phase = EPhase::WaitLobby;
    float Elapsed = 0.0f, PhaseSeconds = 0.0f, TravelSeconds = 0.0f, LoadingSeconds = 0.0f;
    float PausedTime = 0.0f;
    int32 Checks = 0, Failures = 0, SceneIndex = 0, CompletedScenes = 0;
    int32 BlockedReactionSerial = 0;
    bool bEnabled = false, bFinished = false, bLoadingCapture = false, bLobbyCapture = false, bPlayingCapture = false;
    bool bSawToLoading = false, bSawLoading = false, bSawToScene = false;
    bool bBlockedLastTick = false;
    UPROPERTY(Transient) TObjectPtr<UGratiaSceneLibrary> OriginalLibrary;
    UPROPERTY(Transient) TObjectPtr<UGratiaSceneLibrary> ErrorLibrary;
};
