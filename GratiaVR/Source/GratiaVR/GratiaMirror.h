#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GratiaMirror.generated.h"

class AGratiaPreviewCharacter;
class UMaterialInterface;
class UPlanarReflectionComponent;
class UStaticMeshComponent;

/**
 * A real mirror (planar reflection, correct for both eyes): full-length beside the character or
 * on the nearest wall, turned so the player sees her from the side. Can also be placed in a level.
 * Needs r.AllowGlobalClipPlane (DefaultEngine.ini) and the mirror material
 * /Game/Gratia/Environment/M_GratiaMirror (Scripts/setup_scene_extras.py); in editor builds a
 * transient material is made when the asset is missing.
 */
UCLASS()
class GRATIAVR_API AGratiaMirror : public AActor
{
    GENERATED_BODY()
public:
    AGratiaMirror();
    virtual void BeginPlay() override;

    /** Glass size (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mirror", meta = (ClampMin = "10", Units = "cm"))
    FVector2D SizeCm = FVector2D(90.0f, 180.0f);
    /** Reflection resolution relative to the view (cost: one more scene render at this scale). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mirror", meta = (ClampMin = "25", ClampMax = "100"))
    float ScreenPercentage = 60.0f;

    /** Full-length beside the character facing between her and the viewer, or on the nearest wall behind/beside her. */
    void PlaceFor(const AGratiaPreviewCharacter* Character, const FVector& Viewer, bool bOnWall);
    void SetReflectionEnabled(bool bEnabled);
    void SetSize(const FVector2D& Size);
    FString GetDiagnostics() const;

private:
    UMaterialInterface* ResolveMaterial();
    UPROPERTY(VisibleAnywhere, Category = "Mirror") TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere, Category = "Mirror") TObjectPtr<UStaticMeshComponent> Glass;
    UPROPERTY(VisibleAnywhere, Category = "Mirror") TObjectPtr<UPlanarReflectionComponent> Reflection;
    UPROPERTY(VisibleAnywhere, Category = "Mirror") TArray<TObjectPtr<UStaticMeshComponent>> Frame;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> MirrorMaterial;
    FString MaterialSource;
};
