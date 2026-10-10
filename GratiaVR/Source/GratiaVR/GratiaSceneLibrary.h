#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/SaveGame.h"
#include "GratiaSceneLibrary.generated.h"

class UFont;
class UMaterialInterface;
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
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
    FText Title;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
    FText Artist;
    /** Left/right channels as mono sounds for two speakers in an environment (spatial stereo). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Spatial")
    TObjectPtr<USoundBase> LeftSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Spatial")
    TObjectPtr<USoundBase> RightSound;
    /** Tracked beat times (seconds): transitions start and land on a beat. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Mix")
    TArray<float> Beats;
    /** First strong beat (quiet intros are skipped when mixing in). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Mix", meta = (Units = "s"))
    float IntroSeconds = 0.0f;
    /** Latest beat to start mixing out (8 beats before the end). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Mix", meta = (Units = "s"))
    float OutroSeconds = 0.0f;
    /** First tracked beat at or after Seconds (Seconds itself without beats). */
    float NextBeat(float Seconds) const;

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
    /**
     * Art levels streamed together with Environment at the same origin, unmodified (asset-pack maps keep
     * their baked lighting). Markers, speakers and reactive lights belong in Environment.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    TArray<TSoftObjectPtr<UWorld>> Backdrops;
    /** Character profile performance by name; None = free play (idle, touch and reactions). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    FName Performance;
    /** Free play: background music (a performance plays its own scene music). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    TSoftObjectPtr<UGratiaMusicAnalysis> Music;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene", meta = (ClampMin = "0", ClampMax = "2"))
    float MusicVolume = 0.6f;
    /** Room acoustics of the environment (reverb on the speakers and the character's voice). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    TSoftObjectPtr<class UReverbEffect> Reverb;
    /** Analysis of the performance's scene music (drives the environment during the performance). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    TSoftObjectPtr<UGratiaMusicAnalysis> PerformanceMusic;
    /** Start in the partner's eyes (first-person view), as soon as the scene loads. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    bool bStartInPartnerView = false;
    /** How wet the character is in this scene at least (rain: 0.6-1); the player's menu setting can raise it further. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene", meta = (ClampMin = "0", ClampMax = "1"))
    float CharacterWetness = 0.0f;
    /** Places from the markers tagged GratiaCharacterSpot_<SpotVariant> / GratiaPlayerSpot_<SpotVariant> in Environment
     *  (a performance that needs more room than free play); None or missing markers: the default ones. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene")
    FName SpotVariant;
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
    /** Tracks the player can switch between (menu: previous/next); transitions mix on beats. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
    TArray<TSoftObjectPtr<UGratiaMusicAnalysis>> Playlist;
    /** Crossfade length of a track change (beat aligned). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music", meta = (ClampMin = "0.5", ClampMax = "20", Units = "s"))
    float CrossfadeSeconds = 6.0f;
    /** Speaker sound falloff and spread (spatial music from the environment's speakers). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
    TObjectPtr<class USoundAttenuation> SpeakerAttenuation;
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
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
    TObjectPtr<class UStaticMesh> PlaneMesh;
    /** Neon grid floor of the lobby/loading space (reads AudioCollection). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
    TSoftObjectPtr<UMaterialInterface> GridMaterial;
    /** Menu look: rounded font, panel backdrop, hexagon picture frame, pill buttons (UI materials). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look|Menu")
    TObjectPtr<UFont> Font;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look|Menu")
    TObjectPtr<UMaterialInterface> PanelMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look|Menu")
    TObjectPtr<UMaterialInterface> HexMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look|Menu")
    TObjectPtr<UMaterialInterface> PillMaterial;
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
    static constexpr int32 CurrentVersion = 3;
    UPROPERTY() int32 Version = CurrentVersion;
    UPROPERTY() int32 Quality = 1;
    UPROPERTY() bool bHair = true;
    UPROPERTY() bool bCloth = true;
    UPROPERTY() bool bBody = true;
    UPROPERTY() bool bEars = true;
    UPROPERTY() bool bPhysics = true;
    UPROPERTY() bool bSprings = true;
    UPROPERTY() bool bSound = true;
    /** Reaction character: 0 calm, 1 cheerful, 2 reserved. */
    UPROPERTY() int32 Mood = 0;
    UPROPERTY() float MusicVolume = 1.0f;
    UPROPERTY() float HapticsScale = 1.0f;
    UPROPERTY() float HeightOffsetCm = 0.0f;
    /** Reaction voice volume, 0..1 (version 3). */
    UPROPERTY() float VoiceVolume = 1.0f;
    /** Reaction lines in a speech bubble by the head. */
    UPROPERTY() bool bCaptions = true;
    /** Right stick turning: 0 snap 30 degrees, 1 snap 45 degrees, 2 smooth. */
    UPROPERTY() int32 TurnMode = 0;
    /** Stick walking speed: 0 slow, 1 normal, 2 fast. */
    UPROPERTY() int32 WalkSpeed = 1;
    /** The player's hands may enter the character's channels (fingers, flat hand, fist). */
    UPROPERTY() bool bHandPenetration = true;
    /** Forearms continue the hands toward the elbows. */
    UPROPERTY() bool bForearms = true;
    /** The character's wetness, 0 dry .. 1 soaked (menu "Влажность"). */
    UPROPERTY() float Wetness = 0.0f;
    UPROPERTY() FName LastScene;
    /** Playlist entry (analysis asset name) playing when the game was closed. */
    UPROPERTY() FName LastTrack;

    static constexpr const TCHAR* SlotName = TEXT("GratiaUser");
    /** Loaded settings, or defaults when there is no save (or it cannot be read). */
    static UGratiaUserSettings* Load();
    void Sanitize();
    bool Save();
};
