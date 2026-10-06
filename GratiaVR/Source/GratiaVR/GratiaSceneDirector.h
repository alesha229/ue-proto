#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaSceneLibrary.h"
#include "GratiaSceneDirector.generated.h"

class AGratiaLoadingSpace;
class AGratiaPreviewCharacter;
class AGratiaStage1Runtime;
class UAudioComponent;
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

    // Playback of a performance scene.
    void TogglePause();
    void Restart();
    void StepPart(int32 Direction);
    void StepSpeed(int32 Direction);
    FString GetPlaybackText() const;

    /** Music frame (bass, mids, highs, beat) of what plays now. */
    FVector4f GetMusicFrame() const { return MusicFrame; }
    /** Scene vibration (0..1); Runtime applies the player's scale and each hand's tracking gate. */
    float GetSceneHapticAmplitude() const { return HapticAmplitude; }

    UGratiaUserSettings* GetUserSettings() const { return Settings; }
    /** Writes the character/menu state into the settings and saves them (not in tests). */
    void SaveUserSettings();
    void SetMusicVolume(float Volume);
    void SetHapticsScale(float Scale);
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

    AGratiaStage1Runtime* GetRuntime() const;
    AGratiaPreviewCharacter* GetCharacter() const;
    void ApplyUserSettings();
    void Fade(float From, float To);
    FVector HeadFloor(float& OutYaw) const;
    void ShowLoading(const FText& Title, const FText& Subtitle, const FLinearColor& Accent);
    void ParkCharacter();
    void SetStudioVisible(bool bVisible);
    void UnloadEnvironment();
    void RestoreReactiveLights();
    void FailScene(const FString& Reason);
    void CancelContacts();
    void ResetHaptics();
    bool IsSoundEnabled() const;
    float GetFadeSeconds() const;
    float GetLoadingTimeout() const;
    void FinishScene();
    void FinishLobby();
    void PlayAmbient(const TSoftObjectPtr<UGratiaMusicAnalysis>& Track, float Volume);
    void UpdateMusic(float Delta);
    void UpdateHaptics(float Delta);
    void CollectReactiveLights();

    EGratiaFlowState State = EGratiaFlowState::Off;
    int32 Current = INDEX_NONE, Pending = INDEX_NONE;
    float StateSeconds = 0.0f;
    bool bPendingStart = false, bTestMode = false, bFlowQA = false;
    bool bVisibilityRequested = false, bHapticWarning = false;
    FString LastError;
    UPROPERTY(Transient) TObjectPtr<UGratiaUserSettings> Settings;
    UPROPERTY(Transient) TObjectPtr<AGratiaLoadingSpace> Space;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Ambient;
    UPROPERTY(Transient) TObjectPtr<ULevelStreamingDynamic> Streamed;
    UPROPERTY(Transient) TObjectPtr<UGratiaMusicAnalysis> AmbientTrack;
    UPROPERTY(Transient) TObjectPtr<UGratiaMusicAnalysis> PerformanceTrack;
    float AmbientClock = 0.0f;
    float AmbientVolume = 0.5f;
    FVector4f MusicFrame = FVector4f(0, 0, 0, 0);
    FVector4f Smoothed = FVector4f(0, 0, 0, 0);
    float HapticAmplitude = 0.0f;
    FVector LastHapticBone = FVector::ZeroVector;
    bool bHapticValid = false;
    FTransform CharacterHome = FTransform::Identity;
    FTransform AnchorHome = FTransform::Identity;
    bool bHomeSaved = false;
    TArray<TWeakObjectPtr<AActor>> StudioActors;
    TArray<FReactiveLight> ReactiveLights;
    static constexpr float SpeedSteps[5] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f};
};
