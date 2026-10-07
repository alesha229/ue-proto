#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GratiaLoadingSpace.generated.h"

class UGratiaLoadingWidget;
class UGratiaSceneLibrary;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UTexture2D;
class UWidgetComponent;

/**
 * The space around the player in the lobby and between scenes (ViRo / synthwave look): a night
 * sky with a striped sun and stars, a neon grid floor running to the horizon that pulses with
 * the beat, drifting motes, and a loading card (scene picture in a hexagon, title, progress,
 * tip). Everything else (character, room) is hidden while it is shown; it is placed around the
 * player's head when shown.
 */
UCLASS(NotPlaceable)
class GRATIAVR_API AGratiaLoadingSpace : public AActor
{
    GENERATED_BODY()
public:
    AGratiaLoadingSpace();
    virtual void Tick(float DeltaSeconds) override;

    /** Shapes, materials and the card's look come from the scene library; false when unusable. */
    bool Configure(const UGratiaSceneLibrary* Library);
    /** Shows the space at the player's floor point; Title empty = lobby (no loading card). */
    void ShowAt(const FVector& Floor, float Yaw, const FText& Title, const FText& Subtitle, const FLinearColor& Accent, UTexture2D* Picture = nullptr);
    void HideSpace();
    /** Visible in-world failure notice while the menu stays in lobby mode. */
    void ShowNotice(const FText& Message);
    bool IsShown() const { return bShown; }
    void SetProgress(float Value);
    /** Music energy (bass, mids, highs, beat) the sky, grid and motes pulse with. */
    void SetMusic(const FVector4f& Value) { Music = Value; }

private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Sky;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Grid;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Halo;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Motes;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UWidgetComponent> Card;
    UPROPERTY(Transient) TObjectPtr<UGratiaLoadingWidget> CardWidget;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SkyInstance;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GridInstance;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MoteInstance;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HaloInstance;

    struct FMote { FVector Base; float Phase = 0.0f, Speed = 0.0f, Size = 1.0f; };
    TArray<FMote> MoteData;
    FLinearColor Accent = FLinearColor(1.0f, 0.06f, 0.42f);
    FVector4f Music = FVector4f(0, 0, 0, 0);
    float Time = 0.0f, Pulse = 0.0f;
    bool bShown = false;
    void ApplyColors();
};
