#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/SaveGame.h"
#include "GratiaSceneLibrary.generated.h"

class UMaterialParameterCollection;
class USoundBase;
class UTexture2D;
class UWorld;

/**
 * Music analysed offline (editor script): per frame the loudness of bass, mids and highs and a
 * beat pulse, 0..1. The environment reacts to it in sync with the track time (no audio-thread
 * analysis at runtime, so it is cheap and deterministic).
 */
UCLASS(BlueprintType)
class GRATIAVR_API UGratiaMusicAnalysis : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
    TObjectPtr<USoundBase> Sound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "1", Units = "Hz"))
    float FramesPerSecond = 30.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0"))
    float BeatsPerMinute = 0.0f;
    /** X bass, Y mids, Z highs, W beat pulse (1 on a beat, decaying). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
    TArray<FVector4f> Frames;

    /** Interpolated frame at a track time (seconds); zero outside the track. */
    FVector4f Sample(float Seconds) const;
    float GetDuration() const { return FMath::IsFinite(FramesPerSecond) && FramesPerSecond > 0.0f ? Frames.Num() / FramesPerSecond : 0.0f; }
};

/** One entry of the scene library: an environment, its music and what the character does. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaSceneEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    FText Title;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    TSoftObjectPtr<UTexture2D> Thumbnail;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    FLinearColor Accent = FLinearColor(0.95f, 0.35f, 0.65f);
    /** Streamed environment level; empty keeps the persistent studio room. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    TSoftObjectPtr<UWorld> Environment;
    /** Character profile performance by name; None = free play (idle, touch and reactions). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    FName Performance;
    /** Free play: background music (a performance plays its own scene music). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    TSoftObjectPtr<UGratiaMusicAnalysis> Music;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene", meta = (ClampMin = "0", ClampMax = "2"))
    float MusicVolume = 0.6f;
    /** Analysis of the performance's scene music (drives the environment during the performance). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    TSoftObjectPtr<UGratiaMusicAnalysis> PerformanceMusic;
    /** Start in the partner's eyes (first-person view), as soon as the scene loads. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    bool bStartInPartnerView = false;
    /** Controller vibration follows the motion of this character bone (semantic name); None = off. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    FName HapticBone;
    /** Bone speed (cm/s) that gives full vibration. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene", meta = (ClampMin = "1", Units = "cm/s"))
    float HapticFullSpeedCmPerSecond = 60.0f;
};

/** Scenes the player picks in the lobby, the lobby itself and the shared look of transitions. */
UCLASS(BlueprintType)
class GRATIAVR_API UGratiaSceneLibrary : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenes", meta = (TitleProperty = "Id"))
    TArray<FGratiaSceneEntry> Scenes;
    /** Lobby: the loading space with the scene menu; its music. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby")
    TSoftObjectPtr<UGratiaMusicAnalysis> LobbyMusic;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby", meta = (ClampMin = "0", ClampMax = "2"))
    float LobbyMusicVolume = 0.5f;
    /** Materials of the environments read the music from this collection (Bass, Mid, High, Beat, Energy). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
    TObjectPtr<UMaterialParameterCollection> AudioCollection;
    /** Inside of the loading space (sphere around the player); reads AudioCollection and its own Accent/Fade. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
    TSoftObjectPtr<class UMaterialInterface> LoadingSkyMaterial;
    /** Glowing material of rings, motes and the pointer (parameters Color, Intensity). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
    TSoftObjectPtr<class UMaterialInterface> GlowMaterial;
    /** Shapes of the loading space and the pointer (engine basic shapes; referenced here so they cook). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
    TObjectPtr<class UStaticMesh> SphereMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
    TObjectPtr<class UStaticMesh> CylinderMesh;
    /** Shortest time the loading space stays (a scene never pops in mid-fade). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "10", Units = "s"))
    float MinLoadingSeconds = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0.05", ClampMax = "3", Units = "s"))
    float FadeSeconds = 0.6f;
    /** Failed/missing cooked levels return to the lobby instead of waiting forever. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenes", meta = (ClampMin = "1", ClampMax = "120", Units = "s"))
    float LoadingTimeoutSeconds = 30.0f;
    /** Fallback placement when an environment has no explicit player marker. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenes", meta = (ClampMin = "25", Units = "cm"))
    float PlayerDistanceCm = 150.0f;

    int32 FindScene(FName Id) const { return Scenes.IndexOfByPredicate([Id](const FGratiaSceneEntry& Entry) { return Entry.Id == Id; }); }
};

/** Player settings kept between launches (Saved/SaveGames/GratiaUser.sav). */
UCLASS()
class GRATIAVR_API UGratiaUserSettings : public USaveGame
{
    GENERATED_BODY()
public:
    static constexpr int32 CurrentVersion = 2;
    UPROPERTY() int32 Version = CurrentVersion;
    UPROPERTY() int32 Quality = 1;
    UPROPERTY() bool bHair = true;
    UPROPERTY() bool bCloth = true;
    UPROPERTY() bool bBody = true;
    UPROPERTY() bool bEars = true;
    UPROPERTY() bool bPhysics = true;
    UPROPERTY() bool bSprings = true;
    UPROPERTY() bool bSound = true;
    UPROPERTY() float MusicVolume = 1.0f;
    UPROPERTY() float HapticsScale = 1.0f;
    UPROPERTY() float HeightOffsetCm = 0.0f;
    UPROPERTY() FName LastScene;

    static constexpr const TCHAR* SlotName = TEXT("GratiaUser");
    /** Loaded settings, or defaults when there is no save (or it cannot be read). */
    static UGratiaUserSettings* Load();
    void Sanitize();
    bool Save();
};
