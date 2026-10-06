#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaPerformanceStage.generated.h"

class AGratiaPreviewCharacter;
class UAudioComponent;
class UPoseableMeshComponent;
class USoundBase;
struct FGratiaPerformanceClip;

/** Plays the scene of the owner's current performance (FGratiaPerformanceScene): music kept in
 *  sync with the performance time, a posed partner body and the partner's eye viewpoint.
 *  Nothing is spawned for performances without a scene. */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaPerformanceStage : public UActorComponent
{
    GENERATED_BODY()

public:
    UGratiaPerformanceStage();

    /** Seconds since the start of the current performance across its segments; negative without one. */
    float GetPerformanceTime() const;
    /** Partner eyes in world space (X looks, Z is the top of the head); false without a viewpoint. */
    bool GetViewpoint(FTransform& OutWorld) const;
    /** While the player watches from the partner's eyes the scene's hidden bones (head) are hidden. */
    void SetViewpointActive(bool bActive);
    bool IsViewpointActive() const { return bViewpointActive; }
    UPoseableMeshComponent* GetPartner() const { return Partner; }
    /** Largest angle between an aimed partner bone and its target direction (degrees). */
    float GetPartnerPoseErrorDegrees() const { return PartnerPoseError; }
    /** Track position the music should be at now; negative while no music plays. */
    float GetMusicTime() const;
    bool IsMusicPlaying() const;
    /** Re-sync tolerance between the music and the performance time. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "0.05", ClampMax = "2", Units = "s"))
    float MusicResyncSeconds = 0.25f;
    /** Updates partner and music now (also called every tick; QA calls it directly). */
    void UpdateStage();
    /** Paused music holds; a speed other than 1 plays the music faster/slower (pitched). */
    void SetPlayback(bool bInPaused, float Rate) { bPaused = bInPaused; PlaybackRate = FMath::Clamp(Rate, 0.25f, 2.0f); }
    /** Player music volume (0..2) on top of the scene's MusicVolume. */
    void SetMusicVolumeScale(float Scale) { MusicVolumeScale = FMath::Clamp(Scale, 0.0f, 2.0f); }
    FString GetDiagnostics() const;

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

private:
    AGratiaPreviewCharacter* GetCharacter() const;
    const FGratiaPerformanceClip* Current() const;
    void PosePartner(const FGratiaPerformanceClip& Performance);
    void HidePartner();
    void UpdateHiddenBones();
    void StopMusic();

    UPROPERTY(Transient)
    TObjectPtr<UPoseableMeshComponent> Partner;
    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> Music;
    /** Performance entry the partner is posed for (pointer identity within the profile). */
    const FGratiaPerformanceClip* PosedFor = nullptr;
    /** Track position of the playing music, advanced by the playback rate while not paused. */
    float MusicClock = -1.0f;
    float PlaybackRate = 1.0f;
    float MusicVolumeScale = 1.0f;
    bool bPaused = false;
    bool bViewpointActive = false;
    float PartnerPoseError = 0.0f;
};
