#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GratiaLoadingSpace.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * The space around the player between scenes and in the lobby: a sky sphere with a slow moving
 * gradient, floating motes and a ring that pulse with the music, the scene title and a progress
 * ring while the next environment streams in. Everything else (character, room) is hidden while
 * it is shown; it is placed around the player's head when shown.
 */
UCLASS(NotPlaceable)
class GRATIAVR_API AGratiaLoadingSpace : public AActor
{
    GENERATED_BODY()
public:
    AGratiaLoadingSpace();
    virtual void Tick(float DeltaSeconds) override;

    /** Shapes and materials come from the scene library. */
    void Configure(UStaticMesh* Sphere, UStaticMesh* Cylinder, UMaterialInterface* SkyMaterial, UMaterialInterface* GlowMaterial);
    /** Shows the space centred on the head; Title empty = lobby (no title, no progress ring). */
    void ShowAt(const FVector& Head, float Yaw, const FText& Title, const FText& Subtitle, const FLinearColor& Accent);
    void HideSpace();
    /** Visible in-world failure notice while the menu stays in lobby mode. */
    void ShowNotice(const FText& Message);
    bool IsShown() const { return bShown; }
    /** 0..1; the ring fills and spins faster while loading. */
    void SetProgress(float Value) { Progress = FMath::Clamp(Value, 0.0f, 1.0f); }
    /** Music energy (bass, mids, highs, beat) the motes and the ring pulse with. */
    void SetMusic(const FVector4f& Value) { Music = Value; }

private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Sky;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Floor;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Motes;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Ring;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> TitleText;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> SubtitleText;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SkyInstance;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MoteInstance;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RingInstance;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FloorInstance;

    struct FMote { FVector Base; float Phase = 0.0f, Speed = 0.0f, Size = 1.0f; };
    TArray<FMote> MoteData;
    FLinearColor Accent = FLinearColor(0.95f, 0.35f, 0.65f);
    FVector4f Music = FVector4f(0, 0, 0, 0);
    float Progress = 0.0f, Time = 0.0f, RingAngle = 0.0f, Pulse = 0.0f;
    bool bShown = false, bLobby = false;
    void ApplyColors();
};
