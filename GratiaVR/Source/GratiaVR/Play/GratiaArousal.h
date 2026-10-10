#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaPlayMath.h"
#include "GratiaArousal.generated.h"

class AGratiaPreviewCharacter;
class UGratiaPlaySettings;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGratiaArousalStageEvent, int32, Stage, FName, StageName);

/**
 * Arousal / mood meter (gameplay progression). Touch at the pace the current stage wants, on the zones it likes,
 * fills the meter; rough or absent touch drains it. Stages unlock action tags (garments, props, poses).
 * Gains are also passed to UGratiaBodyMotion::AddExcitement (the single Excitement source of the body systems).
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaArousal : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaArousal();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

    UFUNCTION(BlueprintPure, Category = "Arousal") float GetArousal() const { return State.Value; }
    UFUNCTION(BlueprintPure, Category = "Arousal") int32 GetStage() const { return State.Stage; }
    UFUNCTION(BlueprintPure, Category = "Arousal") FName GetStageName() const;
    /** 0..1: how well recent touch matched the wanted pace. */
    UFUNCTION(BlueprintPure, Category = "Arousal") float GetSatisfaction() const { return State.Satisfaction; }
    /** 0..1: how rough recent touch was. */
    UFUNCTION(BlueprintPure, Category = "Arousal") float GetRoughness() const { return State.Rough; }
    /** 0..1: strength of the current stimulus (voice and breath follow it). */
    UFUNCTION(BlueprintPure, Category = "Arousal") float GetStimulus() const { return Stimulus; }
    UFUNCTION(BlueprintPure, Category = "Arousal") bool IsUnlocked(FName Tag) const;
    /** Another source touches a zone this frame (props, channels): Speed cm/s, Scale multiplies the zone weight. */
    UFUNCTION(BlueprintCallable, Category = "Arousal") void AddStimulus(FName Zone, float Speed, float Scale = 1.0f);
    UFUNCTION(BlueprintCallable, Category = "Arousal") void SetArousal(float Value);
    UFUNCTION(BlueprintCallable, Category = "Arousal") void ResetArousal() { SetArousal(0.0f); }
    UPROPERTY(BlueprintAssignable, Category = "Arousal") FGratiaArousalStageEvent OnStageChanged;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arousal") bool bEnabled = true;
    FString GetDiagnostics() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UFUNCTION() void HandleReaction(FName ZoneName, int32 HandIndex, float HandSpeed, int32 Mood);
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<const UGratiaPlaySettings> CachedSettings;
    TArray<GratiaPlay::FArousalStage> Stages;
    GratiaPlay::FArousalState State;
    float Stimulus = 0.0f;
    float PendingBonus = 0.0f;
    float PendingSpeed = 0.0f;
    float PendingStimulus = 0.0f;
    float LastPushed = 0.0f;
    float WrittenMaterial = -1.0f;
    FName LastZone;
    bool bBound = false;
    void Rebuild(const UGratiaPlaySettings& Settings);
    void PushExcitement(float Gain);
};
