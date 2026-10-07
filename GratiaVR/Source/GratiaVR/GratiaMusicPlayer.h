#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaMusicPlayer.generated.h"

class AActor;
class UAudioComponent;
class UGratiaMusicAnalysis;
class USoundAttenuation;

/**
 * Two-deck music player with DJ-style transitions. A change waits for the next beat of the
 * playing track, starts the new one at its first strong beat and crossfades with equal power:
 * the outgoing track is low-passed away while the incoming one rises through a high-pass, so
 * the two bass lines never overlap. A track mixes into the next playlist entry by itself on its
 * last beat before the end. In an environment the left/right channels play from two speakers
 * (spatialised, attenuated, with the room's reverb); elsewhere the track plays as stereo.
 */
UCLASS(ClassGroup = (Gratia))
class GRATIAVR_API UGratiaMusicPlayer : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaMusicPlayer();

    UPROPERTY(Transient) TArray<TObjectPtr<UGratiaMusicAnalysis>> Playlist;
    UPROPERTY(EditAnywhere, Category = "Music", meta = (ClampMin = "0.5", ClampMax = "20", Units = "s"))
    float CrossfadeSeconds = 6.0f;
    UPROPERTY(EditAnywhere, Category = "Music") TObjectPtr<USoundAttenuation> SpeakerAttenuation;
    /** Mixes into the next playlist track at the end of the current one. */
    UPROPERTY(EditAnywhere, Category = "Music") bool bAutoAdvance = true;

    /** Plays Track (mixed in on a beat when something plays, immediate otherwise). */
    void Play(UGratiaMusicAnalysis* Track, float Volume, bool bMix = true);
    void Next();
    void Previous();
    /** Fades everything out over Seconds (0 = immediately). */
    void Stop(float Seconds = 0.5f);
    /** Speakers of the current environment (both null = stereo without position). */
    void SetSpeakers(AActor* Left, AActor* Right);
    void SetMasterVolume(float Volume);
    void SetEnabled(bool bEnabled);
    UGratiaMusicAnalysis* GetCurrent() const;
    float GetCurrentTime() const;
    bool IsPlaying() const;
    bool IsSpatial() const { return SpeakerLeft.IsValid() && SpeakerRight.IsValid(); }
    /** A change waits for the playing track's next beat. */
    bool IsQueued() const { return Queued != nullptr; }
    /** Decks heard now (2 while a transition crossfades). */
    int32 GetAudibleDecks() const { return int32(Decks[0].bPlaying && Decks[0].Gain > 0.01f) + int32(Decks[1].bPlaying && Decks[1].Gain > 0.01f); }
    /** Music frame (bass, mids, highs, beat) of what is audible, decks weighted by their gain. */
    FVector4f GetFrame() const;
    FString GetDiagnostics() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

private:
    struct FDeck
    {
        TObjectPtr<UGratiaMusicAnalysis> Track;
        TObjectPtr<UAudioComponent> Stereo;
        TObjectPtr<UAudioComponent> Left;
        TObjectPtr<UAudioComponent> Right;
        float Clock = 0.0f;
        float Gain = 0.0f;
        float FadeFrom = 0.0f, FadeTo = 0.0f, FadeTime = 0.0f, FadeLength = 0.0f;
        bool bPlaying = false;
        /** Sweeps during a transition: 0 = clean, 1 = fully filtered. */
        float LowPass = 0.0f, HighPass = 0.0f;
    };
    UAudioComponent* MakeComponent(const TCHAR* Name);
    void StartDeck(FDeck& Deck, UGratiaMusicAnalysis* Track, float StartSeconds);
    void StopDeck(FDeck& Deck);
    void ApplyDeck(FDeck& Deck);
    void AttachSpeakers(FDeck& Deck);
    void BeginTransition(UGratiaMusicAnalysis* Track);
    int32 IndexOf(const UGratiaMusicAnalysis* Track) const;

    FDeck Decks[2];
    int32 Active = 0;
    /** Waiting for the next beat of the active deck before the incoming track starts. */
    TObjectPtr<UGratiaMusicAnalysis> Queued;
    float QueuedAt = -1.0f;
    float Volume = 1.0f, Master = 1.0f;
    bool bEnabled = true;
    TWeakObjectPtr<AActor> SpeakerLeft, SpeakerRight;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Components;
    /** Tracks the decks/queue point at (kept alive; the decks are not reflected). */
    UPROPERTY(Transient) TArray<TObjectPtr<UGratiaMusicAnalysis>> Known;
};
