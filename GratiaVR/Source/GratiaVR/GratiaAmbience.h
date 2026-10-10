#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GratiaLightingPresets.h"
#include "GratiaAmbience.generated.h"

class AGratiaMirror;
class AGratiaPreviewCharacter;
class AGratiaSceneControls;
class AGratiaStage1Runtime;
class APostProcessVolume;
class UExponentialHeightFogComponent;
class ULightComponent;
class UPointLightComponent;
class USkyLightComponent;

/**
 * Scene atmosphere and in-scene changes of a game world:
 * - lighting moods (night lamp, sunset, neon, dim) over the current room, without new geometry;
 * - a mirror (full-length beside the character or on the nearest wall) to see her from the side;
 * - in-scene customization: pose, outfit slots, character archetype;
 * - adds the body-motion and customization layers to every character.
 * Controls: the floating button panel next to the player (AGratiaSceneControls), keys L/M/N/K/J,
 * console Gratia.Light / Gratia.Mirror / Gratia.Pose / Gratia.Outfit / Gratia.Archetype / Gratia.Ambience.
 */
UCLASS()
class GRATIAVR_API UGratiaAmbience : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UGratiaAmbience* Get(const UObject* WorldContext);

    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override;

    // Lighting.
    int32 GetLightingPreset() const { return Preset; }
    int32 GetLightingPresetCount() const;
    FText GetLightingLabel() const;
    void SetLightingPreset(int32 Index);
    void NextLightingPreset() { SetLightingPreset((Preset + 1) % FMath::Max(1, GetLightingPresetCount())); }

    // Mirror: 0 off, 1 full-length beside the character, 2 on the nearest wall.
    int32 GetMirrorMode() const { return MirrorMode; }
    FText GetMirrorLabel() const;
    void SetMirrorMode(int32 Mode);
    void NextMirrorMode() { SetMirrorMode((MirrorMode + 1) % 3); }

    // Character changes.
    AGratiaPreviewCharacter* GetCharacter() const;
    void NextPose();
    FText GetPoseLabel() const;
    void NextArchetype();
    FText GetArchetypeLabel() const;
    bool NextOutfit(int32 Slot);
    FText GetOutfitLabel(int32 Slot) const;
    int32 GetOutfitSlotCount() const;

    FString GetDiagnostics() const;
    /** QA/capture runs keep the authored look and no extra panel. */
    static bool IsQARun();

private:
    struct FRoomLight
    {
        TWeakObjectPtr<ULightComponent> Light;
        float BaseIntensity = 0.0f;
        FLinearColor BaseColor = FLinearColor::White;
        FRotator BaseRotation = FRotator::ZeroRotator;
        bool bSun = false, bCanRotate = false;
        float FromIntensity = 0.0f, ToIntensity = 0.0f;
        FLinearColor FromColor = FLinearColor::White, ToColor = FLinearColor::White;
        FRotator FromRotation = FRotator::ZeroRotator, ToRotation = FRotator::ZeroRotator;
    };
    struct FSky
    {
        TWeakObjectPtr<USkyLightComponent> Light;
        float BaseIntensity = 1.0f, FromIntensity = 1.0f, ToIntensity = 1.0f;
        FLinearColor BaseColor = FLinearColor::White, FromColor = FLinearColor::White, ToColor = FLinearColor::White;
    };
    struct FFog
    {
        TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
        FLinearColor BaseColor = FLinearColor::White, FromColor = FLinearColor::White, ToColor = FLinearColor::White;
    };
    struct FMood
    {
        TWeakObjectPtr<UPointLightComponent> Light;
        int32 PresetIndex = 0, LightIndex = 0;
        float FromIntensity = 0.0f, ToIntensity = 0.0f;
    };
    struct FGrade
    {
        float Weight = 0.0f, WhiteTemperature = 6500.0f, ExposureBias = 0.0f, Saturation = 1.0f, Vignette = 0.4f;
    };

    const UGratiaLightingPresets* GetPresets() const;
    const FGratiaLightingPreset* GetPreset(int32 Index) const;
    void CollectLights(bool bApplyNow);
    void Retarget(bool bSnapshot);
    void ApplyLights(float Alpha);
    void UpdateMoodLightPlacement();
    void EnsureCharacters();
    void HandleKeys();
    AGratiaStage1Runtime* GetRuntime() const;
    void OnActorSpawned(AActor* Actor);

    UPROPERTY(Transient) TObjectPtr<UGratiaLightingPresets> DefaultPresets;
    UPROPERTY(Transient) TObjectPtr<UGratiaLightingPresets> LoadedPresets;
    UPROPERTY(Transient) TObjectPtr<AActor> MoodActor;
    UPROPERTY(Transient) TObjectPtr<APostProcessVolume> GradeVolume;
    UPROPERTY(Transient) TObjectPtr<AGratiaMirror> Mirror;
    UPROPERTY(Transient) TObjectPtr<AGratiaSceneControls> Controls;

    TArray<FRoomLight> RoomLights;
    TArray<FSky> Skies;
    TArray<FFog> Fogs;
    TArray<FMood> Moods;
    FGrade FromGrade, ToGrade;
    int32 Preset = 0;
    int32 MirrorMode = 0;
    float Blend = 1.0f;
    float CollectTimer = 0.0f;
    int32 StaticLightsSkipped = 0;
    bool bBegun = false;
    bool bQA = false;
    mutable TWeakObjectPtr<AGratiaStage1Runtime> CachedRuntime;
    FDelegateHandle SpawnHandle;
};
