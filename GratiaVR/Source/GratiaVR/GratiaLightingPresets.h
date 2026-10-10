#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GratiaLightingPresets.generated.h"

/** An extra light placed relative to the character (night lamp, neon strip, sunset key). */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaMoodLight
{
    GENERATED_BODY()
    /** Character frame: X forward (toward the player), Y right, Z up from the feet. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (Units = "cm"))
    FVector OffsetCm = FVector(60.0f, 80.0f, 70.0f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
    FLinearColor Color = FLinearColor(1.0f, 0.65f, 0.35f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0", Units = "cd"))
    float Intensity = 8.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "10", Units = "cm"))
    float RadiusCm = 450.0f;
    /** Soft source size (larger is a softer highlight on skin). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0", Units = "cm"))
    float SourceRadiusCm = 6.0f;
};

/**
 * A lighting mood applied to the current room without new geometry: the room's own lights are
 * scaled/tinted (movable and stationary ones; static lights cannot change), sky light and fog are
 * tinted, extra mood lights appear around the character and a post-process grade is blended in.
 */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaLightingPreset
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
    FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
    FText Label;
    /** Point/spot/rect lights of the room. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Lights", meta = (ClampMin = "0", ClampMax = "4"))
    float LightScale = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Lights")
    FLinearColor LightTint = FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0", ClampMax = "4"))
    float SunScale = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
    FLinearColor SunTint = FLinearColor::White;
    /** Sun elevation in degrees above the horizon (movable sun only); 0 keeps the room's sun direction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0", ClampMax = "90", Units = "deg"))
    float SunElevationDegrees = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (ClampMin = "0", ClampMax = "4"))
    float SkyScale = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
    FLinearColor SkyTint = FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
    FLinearColor FogTint = FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mood Lights", meta = (TitleProperty = "Color"))
    TArray<FGratiaMoodLight> MoodLights;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade")
    bool bGrade = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade", meta = (ClampMin = "1500", ClampMax = "15000"))
    float WhiteTemperature = 6500.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade", meta = (ClampMin = "-4", ClampMax = "4"))
    float ExposureBias = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade", meta = (ClampMin = "0", ClampMax = "2"))
    float Saturation = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade", meta = (ClampMin = "0", ClampMax = "1"))
    float Vignette = 0.4f;
};

/** Lighting moods the player switches in a scene (panel, L key, console Gratia.Light). Index 0 keeps the room as authored. */
UCLASS(BlueprintType)
class GRATIAVR_API UGratiaLightingPresets : public UDataAsset
{
    GENERATED_BODY()
public:
    UGratiaLightingPresets();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presets", meta = (TitleProperty = "Label"))
    TArray<FGratiaLightingPreset> Presets;
    /** Seconds of a mood change. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presets", meta = (ClampMin = "0", ClampMax = "10", Units = "s"))
    float TransitionSeconds = 1.5f;
};
