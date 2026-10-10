#pragma once
#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "GratiaPlaySettings.h"
#include "GratiaSynthSound.generated.h"

/**
 * Placeholder voice and foley when a PlaySettings bank is empty: shaped noise (breaths, slides, rustle),
 * a soft hum for non-verbal sounds and short clicks (buttons, zipper, props). Synthesised on the audio thread,
 * no assets. Real recordings in the banks always win. One instance per playback (it keeps its own state).
 */
UCLASS()
class GRATIAVR_API UGratiaSynthSound : public USoundWaveProcedural
{
    GENERATED_BODY()
public:
    UGratiaSynthSound(const FObjectInitializer& Initializer);
    static UGratiaSynthSound* MakeVocal(UObject* Outer, EGratiaVocalBank Bank);
    static UGratiaSynthSound* MakeFoley(UObject* Outer, EGratiaFoleyBank Bank);
    /** Seconds until a one-shot is silent (callers stop the component then); 0 for loops. */
    float GetLength() const { return bLoop ? 0.0f : Length; }
    virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;

private:
    static constexpr int32 Rate = 48000;
    /** Envelope: 0 rise (inhale), 1 fall (exhale), 2 bell (sigh, hum), 3 click train, 4 steady loop. */
    uint8 Shape = 4;
    float Length = 1.0f;
    bool bLoop = false;
    float Low = 300.0f, High = 3000.0f;   // band of the noise
    float Hum = 0.0f, HumHz = 220.0f;     // voiced part
    float Clicks = 0.0f, ClickRate = 0.0f; // click train / crackle per second
    float Gain = 0.4f;
    int64 Sample = 0;
    uint32 Seed = 1;
    float LowState = 0.0f, HighState = 0.0f, HighIn = 0.0f, Phase = 0.0f, Crackle = 0.0f;
    void Configure(uint8 InShape, float InLength, float InLow, float InHigh, float InGain);
    float Noise();
};
