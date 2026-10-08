#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaSceneLibrary.h"
#include "GratiaSceneDirector.generated.h"

class AGratiaLoadingSpace;
class AGratiaPreviewCharacter;
class AGratiaStage1Runtime;
class UAudioComponent;
class UGratiaMusicPlayer;
class ULevelStreamingDynamic;
class ULightComponent;

UENUM(BlueprintType)
enum class EGratiaFlowState : uint8
{
    /** Tests and maps without a scene library: the studio room as before. */
    Off,
    Lobby,
    ToLoading,
    Loading,
    ToScene,
    Playing,
    ToLobby
};

/**
 * The player-facing flow (ViRo-style): a lobby in the loading space with the scene menu, a fade
 * into the loading space while the scene's environment streams in, then the scene itself
 * (character placed at the environment's marker, free play or a performance in first person).
 * The environment reacts to the music (material collection + tagged lights) and a performance
 * can drive controller vibration. Player settings persist between launches.
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaSceneDirector : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaSceneDirector();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenes")
    TObjectPtr<UGratiaSceneLibrary> Library;

    UFUNCTION(BlueprintCallable, Category = "Scenes") void EnterLobby();
    UFUNCTION(BlueprintCallable, Category = "Scenes") bool StartScene(int32 Index);
    UFUNCTION(BlueprintCallable, Category = "Scenes") void ExitToLobby();
    UFUNCTION(BlueprintPure, Category = "Scenes") EGratiaFlowState GetState() const { return State; }
    bool IsActive() const { return State != EGratiaFlowState::Off; }
    bool IsInScene() const { return State == EGratiaFlowState::Playing; }
    int32 GetCurrentScene() const { return Current; }
    const FGratiaSceneEntry* GetCurrentEntry() const;
    bool IsPerformanceScene() const;
    /** Runtime uses this gate for locomotion, contacts and hand/body constraints. */
    UFUNCTION(BlueprintPure, Category = "Scenes") bool IsSceneInputBlocked() const;
    UFUNCTION(BlueprintPure, Category = "Scenes") FString GetLastError() const { return LastError; }
    bool CanStartScene(int32 Index, FString& Reason) const;
    bool IsEnvironmentReady() const;
    const ULevelStreamingDynamic* GetStreamedEnvironment() const { return Streamed.Get(); }
    /** Backdrop levels of the current scene that are loaded and visible. */
    int32 GetVisibleBackdrops() const;

    // Playback of a performance scene.
    void TogglePause();
    void Restart();
    void StepPart(int32 Direction);
    void StepSpeed(int32 Direction);
    FString GetPlaybackText() const;

    /** Music frame (bass, mids, highs, beat) of what plays now. */
    FVector4f GetMusicFrame() const { return MusicFrame; }

    UGratiaUserSettings* GetUserSettings() const { return Settings; }
    /** Writes the character/menu state into the settings and saves them (not in tests). */
    void SaveUserSettings();
    void SetMusicVolume(float Volume);
    void SetHapticsScale(float Scale);
    /** Playlist: mixes into the next/previous track on a beat (a performance's own music mutes). */
    void NextTrack();
    void PreviousTrack();
    /** "Title - Artist" of what plays now (empty when nothing does). */
    FString GetTrackText() const;
    UGratiaMusicPlayer* GetMusicPlayer() const { return Music; }
    FString GetDiagnostics() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

private:
    struct FReactiveLight
    {
        TWeakObjectPtr<ULightComponent> Light;
        float BaseIntensity = 0.0f;
        int32 Band = 0;
    };
    /** Environment props moved by the music: tags GratiaSpin:<deg/s>, GratiaSweep:<deg>:<s>, GratiaPulse:<scale>. */
    struct FMover
    {
        TWeakObjectPtr<AActor> Actor;
        FRotator BaseRotation = FRotator::ZeroRotator;
        FVector BaseLocation = FVector::ZeroVector, BaseScale = FVector::OneVector;
        /** Rotation centre on the actor's local Z (cm, unscaled), e.g. the emitter end of a beam. */
        float PivotOffset = 0.0f;
        float Spin = 0.0f, Sweep = 0.0f, SweepPeriod = 4.0f, Pulse = 0.0f, Phase = 0.0f;
    };
    void CollectMovers();
    void UpdateMovers(float Delta);
    TArray<FMover> Movers;
    float MoverTime = 0.0f;

    AGratiaStage1Runtime* GetRuntime() const;
    AGratiaPreviewCharacter* GetCharacter() const;
    void ApplyUserSettings();
    void Fade(float From, float To);
    FVector HeadFloor(float& OutYaw) const;
    void ShowLoading(const FText& Title, const FText& Subtitle, const FLinearColor& Accent, class UTexture2D* Picture = nullptr);
    void ParkCharacter();
    void SetStudioVisible(bool bVisible);
    void UnloadEnvironment();
    void RestoreReactiveLights();
    void FailScene(const FString& Reason);
    void CancelContacts();
    bool IsSoundEnabled() const;
    float GetFadeSeconds() const;
    float GetLoadingTimeout() const;
    void FinishScene();
    void FinishLobby();
    void PlayAmbient(const TSoftObjectPtr<UGratiaMusicAnalysis>& Track, float Volume);
    void UpdateMusic(float Delta);
    void CollectReactiveLights();

    EGratiaFlowState State = EGratiaFlowState::Off;
    int32 Current = INDEX_NONE, Pending = INDEX_NONE;
    float StateSeconds = 0.0f;
    bool bPendingStart = false, bTestMode = false, bFlowQA = false;
    /** -GratiaScene=<Id>: start straight in this scene (performance and soak runs in a real environment). */
    FName PinnedScene;
    bool bVisibilityRequested = false;
    FString LastError;
    UPROPERTY(Transient) TObjectPtr<UGratiaUserSettings> Settings;
    UPROPERTY(Transient) TObjectPtr<AGratiaLoadingSpace> Space;
    UPROPERTY(Transient) TObjectPtr<UGratiaMusicPlayer> Music;
    /** A playlist track chosen during a performance replaces (mutes) the performance music. */
    bool bPlaylistOverride = false;
    UPROPERTY(Transient) TObjectPtr<ULevelStreamingDynamic> Streamed;
    UPROPERTY(Transient) TArray<TObjectPtr<ULevelStreamingDynamic>> Backdrops;
    /** Environment and backdrop streams that exist. */
    TArray<ULevelStreamingDynamic*> GetStreams() const;
    bool AreStreamsVisible() const;
    void SetStreamsVisible(bool bVisible);
    UPROPERTY(Transient) TObjectPtr<UGratiaMusicAnalysis> PerformanceTrack;
    FVector4f MusicFrame = FVector4f(0, 0, 0, 0);
    FVector4f Smoothed = FVector4f(0, 0, 0, 0);
    FTransform CharacterHome = FTransform::Identity;
    FTransform AnchorHome = FTransform::Identity;
    bool bHomeSaved = false;
    TArray<TWeakObjectPtr<AActor>> StudioActors;
    TArray<FReactiveLight> ReactiveLights;
    static constexpr float SpeedSteps[5] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f};
};
