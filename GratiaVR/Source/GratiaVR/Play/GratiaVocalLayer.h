#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaPlayMath.h"
#include "GratiaPlaySettings.h"
#include "GratiaVocalLayer.generated.h"

class AGratiaPreviewCharacter;
class UAudioComponent;
class USoundBase;

/**
 * Non-verbal voice and ASMR foley without actors: a breathing rhythm driven by arousal (faster, deeper, broken),
 * inhales/exhales, sighs, held breaths after a startle, short non-verbal sounds ("ннн…", "ах…") picked from the
 * profile's banks and played from her mouth (in step with UGratiaBodyMotion's breathing when present); breathing into the player's ear when the head is close; skin, cloth
 * and wet slide loops at the touching hand, and one-shots (buttons, zipper, straps, props).
 * Banks are hooks: an empty bank is silent and reported once. The breath curve is exported for animation.
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaVocalLayer : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaVocalLayer();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

    /** 0..1 chest signal of the breathing rhythm (inhale rises, exhale falls) for procedural breathing. */
    UFUNCTION(BlueprintPure, Category = "Voice") float GetBreathCurve() const { return GratiaPlay::BreathCurve(Breath); }
    UFUNCTION(BlueprintPure, Category = "Voice") float GetBreathRate() const { return BreathRate; }
    UFUNCTION(BlueprintPure, Category = "Voice") bool IsBreathHeld() const { return Breath.HoldSeconds > 0.0f; }
    /** Holds the breath (startle, tension); it ends with a released exhale. */
    UFUNCTION(BlueprintCallable, Category = "Voice") void HoldBreath(float Seconds);
    UFUNCTION(BlueprintCallable, Category = "Voice") void PlayVocal(EGratiaVocalBank Bank, float VolumeScale = 1.0f);
    UFUNCTION(BlueprintCallable, Category = "Voice") void PlayFoley(EGratiaFoleyBank Bank, FVector Location, float VolumeScale = 1.0f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice") bool bBreath = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice") bool bFoley = true;
    FString GetDiagnostics() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UFUNCTION() void HandleReaction(FName ZoneName, int32 HandIndex, float HandSpeed, int32 Mood);
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    GratiaPlay::FBreathState Breath;
    float BreathRate = 14.0f;
    double QuietUntil = 0.0;
    bool bAfterHold = false;
    /** The breath events follow UGratiaBodyMotion's rhythm (chest and sound in step). */
    bool bFollowsBody = false;
    bool bBound = false;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Loops[2];
    EGratiaFoleyBank LoopBank[2] = { EGratiaFoleyBank::SkinSlide, EGratiaFoleyBank::SkinSlide };
    float LoopVolume[2] = { 0.0f, 0.0f };
    TSet<uint8> ReportedVocal, ReportedFoley;
    TMap<uint8, int32> LastPick;
    int32 Played = 0;
    USoundBase* Pick(const FGratiaSoundBank& Bank, uint8 Key);
    bool GetMouth(FVector& Out) const;
    float GetWetness() const;
    void UpdateLoops(const UGratiaPlaySettings& Settings, float Delta);
};
