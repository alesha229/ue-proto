#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaPlayMath.h"
#include "GratiaHapticLayers.generated.h"

/**
 * Layered controller vibration on top of the runtime's contact haptics: skin slide (grain follows the stroke),
 * elastic resistance where the hand presses a non-soft zone, the heartbeat (heart zones, a held wrist, or
 * everywhere at high arousal), sustained sources (a vibrating toy) and short pulses (button, zipper, refusal).
 * AGratiaStage1Runtime::UpdateHaptics mixes its own amplitude with Blend() before scaling and sending.
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaHapticLayers : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaHapticLayers();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

    /** Mixes this hand's layers into the runtime's values (independent-event mix, 0..1). */
    void Blend(bool bLeft, float& Amplitude, float& Frequency) const;
    /** A continuous source for this frame (lapses unless refreshed within 0.1 s). */
    void SetSustain(bool bLeft, FName Source, float Amplitude, float Frequency);
    void Pulse(bool bLeft, float Amplitude, float Frequency, float Seconds);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics") bool bSlide = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics") bool bPress = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics") bool bHeartbeat = true;

private:
    struct FSustain { FName Source; GratiaPlay::FHaptic Value; double Time = 0.0; };
    struct FPulse { GratiaPlay::FHaptic Value; float Remaining = 0.0f; };
    GratiaPlay::FHaptic Layers[2];
    TArray<FSustain> Sustains[2];
    TArray<FPulse> Pulses[2];
};
