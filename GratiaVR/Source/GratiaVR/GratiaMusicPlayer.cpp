#include "GratiaMusicPlayer.h"
#include "GratiaSceneLibrary.h"
#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaMusic, Log, All);

namespace
{
float GratiaTrackLength(const UGratiaMusicAnalysis* Track)
{
    return Track && Track->Sound ? Track->Sound->GetDuration() : 0.0f;
}

bool GratiaTrackLoops(const UGratiaMusicAnalysis* Track)
{
    const USoundWave* Wave = Track ? Cast<USoundWave>(Track->Sound) : nullptr;
    return Wave && Wave->bLooping;
}
}

UGratiaMusicPlayer::UGratiaMusicPlayer()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

UAudioComponent* UGratiaMusicPlayer::MakeComponent(const TCHAR* Name)
{
    UAudioComponent* Component = NewObject<UAudioComponent>(GetOwner(), Name);
    Component->bAutoActivate = false;
    Component->bAllowSpatialization = false;
    Component->bIsUISound = false;
    Component->SetupAttachment(GetOwner()->GetRootComponent());
    Component->RegisterComponent();
    Components.Add(Component);
    return Component;
}

void UGratiaMusicPlayer::BeginPlay()
{
    Super::BeginPlay();
    const TCHAR* Names[2][3] = {{TEXT("MusicA"), TEXT("MusicAL"), TEXT("MusicAR")}, {TEXT("MusicB"), TEXT("MusicBL"), TEXT("MusicBR")}};
    for (int32 I = 0; I < 2; ++I)
    {
        Decks[I].Stereo = MakeComponent(Names[I][0]);
        Decks[I].Left = MakeComponent(Names[I][1]);
        Decks[I].Right = MakeComponent(Names[I][2]);
    }
}

void UGratiaMusicPlayer::EndPlay(const EEndPlayReason::Type Reason)
{
    for (FDeck& Deck : Decks) StopDeck(Deck);
    Super::EndPlay(Reason);
}

int32 UGratiaMusicPlayer::IndexOf(const UGratiaMusicAnalysis* Track) const
{
    return Playlist.IndexOfByKey(Track);
}

UGratiaMusicAnalysis* UGratiaMusicPlayer::GetCurrent() const
{
    return Queued ? Queued.Get() : Decks[Active].Track.Get();
}

float UGratiaMusicPlayer::GetCurrentTime() const { return Decks[Active].Clock; }

bool UGratiaMusicPlayer::IsPlaying() const { return Decks[0].bPlaying || Decks[1].bPlaying; }

void UGratiaMusicPlayer::AttachSpeakers(FDeck& Deck)
{
    AActor* Left = SpeakerLeft.Get();
    AActor* Right = SpeakerRight.Get();
    for (TPair<UAudioComponent*, AActor*> Pair : {TPair<UAudioComponent*, AActor*>(Deck.Left, Left), TPair<UAudioComponent*, AActor*>(Deck.Right, Right)})
    {
        UAudioComponent* Component = Pair.Key;
        if (!Component) continue;
        if (Pair.Value && Pair.Value->GetRootComponent())
            Component->AttachToComponent(Pair.Value->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        else
            Component->AttachToComponent(GetOwner()->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        Component->bAllowSpatialization = Pair.Value != nullptr;
        Component->AttenuationSettings = Pair.Value ? SpeakerAttenuation.Get() : nullptr;
    }
}

void UGratiaMusicPlayer::StartDeck(FDeck& Deck, UGratiaMusicAnalysis* Track, float StartSeconds)
{
    StopDeck(Deck);
    if (!Track || !Track->Sound) return;
    Known.AddUnique(Track);
    Deck.Track = Track;
    Deck.Clock = FMath::Clamp(StartSeconds, 0.0f, FMath::Max(0.0f, GratiaTrackLength(Track) - 1.0f));
    Deck.bPlaying = true;
    Deck.LowPass = Deck.HighPass = 0.0f;
    const bool bSpatial = IsSpatial() && Track->LeftSound && Track->RightSound;
    if (bSpatial)
    {
        AttachSpeakers(Deck);
        Deck.Left->SetSound(Track->LeftSound);
        Deck.Right->SetSound(Track->RightSound);
        ApplyDeck(Deck);
        Deck.Left->Play(Deck.Clock);
        Deck.Right->Play(Deck.Clock);
    }
    else
    {
        Deck.Stereo->SetSound(Track->Sound);
        ApplyDeck(Deck);
        Deck.Stereo->Play(Deck.Clock);
    }
    UE_LOG(LogGratiaMusic, Display, TEXT("MUSIC deck start %s at %.2fs spatial=%d"), *Track->GetName(), Deck.Clock, bSpatial ? 1 : 0);
}

void UGratiaMusicPlayer::StopDeck(FDeck& Deck)
{
    for (UAudioComponent* Component : {Deck.Stereo.Get(), Deck.Left.Get(), Deck.Right.Get()})
        if (Component && Component->IsPlaying()) Component->Stop();
    Deck.bPlaying = false;
    Deck.Gain = Deck.FadeFrom = Deck.FadeTo = 0.0f;
    Deck.FadeLength = Deck.FadeTime = 0.0f;
}

void UGratiaMusicPlayer::ApplyDeck(FDeck& Deck)
{
    const float Gain = Deck.Gain * Volume * Master * (bEnabled ? 1.0f : 0.0f);
    // Exponential sweeps: low-pass 20 kHz -> 250 Hz, high-pass 10 Hz -> 500 Hz.
    const float LowPass = 20000.0f * FMath::Pow(250.0f / 20000.0f, FMath::Clamp(Deck.LowPass, 0.0f, 1.0f));
    const float HighPass = 10.0f * FMath::Pow(500.0f / 10.0f, FMath::Clamp(Deck.HighPass, 0.0f, 1.0f));
    for (UAudioComponent* Component : {Deck.Stereo.Get(), Deck.Left.Get(), Deck.Right.Get()})
    {
        if (!Component) continue;
        Component->SetVolumeMultiplier(FMath::Max(Gain, 0.0001f));
        Component->SetLowPassFilterEnabled(Deck.LowPass > 0.001f);
        Component->SetLowPassFilterFrequency(LowPass);
        Component->SetHighPassFilterEnabled(Deck.HighPass > 0.001f);
        Component->SetHighPassFilterFrequency(HighPass);
    }
}

void UGratiaMusicPlayer::Play(UGratiaMusicAnalysis* Track, float InVolume, bool bMix)
{
    Volume = FMath::Clamp(FMath::IsFinite(InVolume) ? InVolume : 1.0f, 0.0f, 2.0f);
    if (!Track || !Track->Sound) { Stop(0.5f); return; }
    if (GetCurrent() == Track && IsPlaying()) { for (FDeck& Deck : Decks) ApplyDeck(Deck); return; }
    if (!bMix || !Decks[Active].bPlaying)
    {
        Queued = nullptr;
        StopDeck(Decks[1 - Active]);
        FDeck& Deck = Decks[Active];
        StartDeck(Deck, Track, Track->IntroSeconds);
        Deck.FadeFrom = 0.0f; Deck.FadeTo = 1.0f; Deck.FadeTime = 0.0f; Deck.FadeLength = bMix ? 1.5f : 0.3f;
        return;
    }
    BeginTransition(Track);
}

void UGratiaMusicPlayer::BeginTransition(UGratiaMusicAnalysis* Track)
{
    const FDeck& Playing = Decks[Active];
    Known.AddUnique(Track);
    Queued = Track;
    // Start on the playing track's next beat (soon: a far beat would feel unresponsive).
    const float Beat = Playing.Track ? Playing.Track->NextBeat(Playing.Clock + 0.05f) : Playing.Clock;
    QueuedAt = Beat - Playing.Clock <= 2.0f ? Beat : Playing.Clock;
    UE_LOG(LogGratiaMusic, Display, TEXT("MUSIC transition to %s at %.2fs (now %.2fs)"), *Track->GetName(), QueuedAt, Playing.Clock);
}

void UGratiaMusicPlayer::Next()
{
    if (Playlist.IsEmpty()) return;
    const int32 Index = IndexOf(GetCurrent());
    Play(Playlist[(Index + 1 + Playlist.Num()) % Playlist.Num()], Volume, true);
}

void UGratiaMusicPlayer::Previous()
{
    if (Playlist.IsEmpty()) return;
    const int32 Index = IndexOf(GetCurrent());
    Play(Playlist[(Index == INDEX_NONE ? 0 : Index - 1 + Playlist.Num()) % Playlist.Num()], Volume, true);
}

void UGratiaMusicPlayer::Stop(float Seconds)
{
    Queued = nullptr;
    for (FDeck& Deck : Decks)
    {
        if (!Deck.bPlaying) continue;
        if (Seconds <= 0.0f) { StopDeck(Deck); continue; }
        Deck.FadeFrom = Deck.Gain; Deck.FadeTo = 0.0f; Deck.FadeTime = 0.0f; Deck.FadeLength = Seconds;
    }
}

void UGratiaMusicPlayer::SetSpeakers(AActor* Left, AActor* Right)
{
    if (SpeakerLeft.Get() == Left && SpeakerRight.Get() == Right) return;
    SpeakerLeft = Left;
    SpeakerRight = Right;
    // Re-route what plays (a scene change happens behind a fade).
    for (FDeck& Deck : Decks)
    {
        if (!Deck.bPlaying || !Deck.Track) continue;
        UGratiaMusicAnalysis* Track = Deck.Track;
        const float Clock = Deck.Clock, Gain = Deck.Gain, From = Deck.FadeFrom, To = Deck.FadeTo, Time = Deck.FadeTime, Length = Deck.FadeLength;
        StartDeck(Deck, Track, Clock);
        Deck.Gain = Gain; Deck.FadeFrom = From; Deck.FadeTo = To; Deck.FadeTime = Time; Deck.FadeLength = Length;
        ApplyDeck(Deck);
    }
}

void UGratiaMusicPlayer::SetMasterVolume(float InMaster)
{
    Master = FMath::Clamp(FMath::IsFinite(InMaster) ? InMaster : 1.0f, 0.0f, 2.0f);
    for (FDeck& Deck : Decks) ApplyDeck(Deck);
}

void UGratiaMusicPlayer::SetEnabled(bool bInEnabled)
{
    bEnabled = bInEnabled;
    for (FDeck& Deck : Decks) ApplyDeck(Deck);
}

void UGratiaMusicPlayer::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    if (!FMath::IsFinite(Delta) || Delta <= 0.0f) return;
    FDeck& Playing = Decks[Active];
    // Queued change: the incoming track lands on the playing track's beat.
    if (Queued && (!Playing.bPlaying || Playing.Clock + Delta >= QueuedAt))
    {
        const int32 Incoming = 1 - Active;
        StartDeck(Decks[Incoming], Queued, Queued->IntroSeconds);
        FDeck& In = Decks[Incoming];
        In.FadeFrom = 0.0f; In.FadeTo = 1.0f; In.FadeTime = 0.0f; In.FadeLength = CrossfadeSeconds;
        In.HighPass = 1.0f;
        if (Playing.bPlaying) { Playing.FadeFrom = Playing.Gain; Playing.FadeTo = 0.0f; Playing.FadeTime = 0.0f; Playing.FadeLength = CrossfadeSeconds; }
        Active = Incoming;
        Queued = nullptr;
    }
    for (int32 I = 0; I < 2; ++I)
    {
        FDeck& Deck = Decks[I];
        if (!Deck.bPlaying) continue;
        Deck.Clock += Delta;
        const float Length = GratiaTrackLength(Deck.Track);
        if (Length > 0.0f && Deck.Clock >= Length)
        {
            if (GratiaTrackLoops(Deck.Track)) Deck.Clock = FMath::Fmod(Deck.Clock, Length);
            else if (I == Active && !Queued) { StartDeck(Deck, Deck.Track, Deck.Track->IntroSeconds); Deck.Gain = 1.0f; }
            else { StopDeck(Deck); continue; }
        }
        if (Deck.FadeLength > 0.0f)
        {
            Deck.FadeTime = FMath::Min(Deck.FadeTime + Delta, Deck.FadeLength);
            const float T = Deck.FadeTime / Deck.FadeLength;
            // Equal power: sine in, cosine out; the filters clear/close over the fade.
            const float Curve = Deck.FadeTo > Deck.FadeFrom ? FMath::Sin(T * HALF_PI) : FMath::Cos(T * HALF_PI);
            Deck.Gain = Deck.FadeTo > Deck.FadeFrom ? FMath::Lerp(Deck.FadeFrom, Deck.FadeTo, Curve) : Deck.FadeTo + (Deck.FadeFrom - Deck.FadeTo) * Curve;
            if (Deck.FadeTo > Deck.FadeFrom) Deck.HighPass = FMath::Min(Deck.HighPass, 1.0f - FMath::Clamp(T * 1.6f, 0.0f, 1.0f));
            else if (I != Active) Deck.LowPass = FMath::Clamp(T * 1.3f, 0.0f, 1.0f);
            if (T >= 1.0f)
            {
                Deck.FadeLength = 0.0f;
                if (Deck.FadeTo <= 0.0f) { StopDeck(Deck); continue; }
            }
        }
        ApplyDeck(Deck);
    }
    // Auto mix into the next playlist track on the last beat before the end.
    const FDeck& Now = Decks[Active];
    if (bAutoAdvance && !Queued && Now.bPlaying && Now.Track && !GratiaTrackLoops(Now.Track) && Playlist.Num() > 1
        && IndexOf(Now.Track) != INDEX_NONE && Now.Track->OutroSeconds > 0.0f && Now.Clock >= Now.Track->OutroSeconds && Now.FadeLength <= 0.0f)
        Next();
}

FVector4f UGratiaMusicPlayer::GetFrame() const
{
    FVector4f Frame(0, 0, 0, 0);
    for (const FDeck& Deck : Decks)
        if (Deck.bPlaying && Deck.Track && bEnabled) Frame += Deck.Track->Sample(Deck.Clock) * FMath::Clamp(Deck.Gain, 0.0f, 1.0f);
    for (int32 I = 0; I < 4; ++I) Frame[I] = FMath::Clamp(Frame[I], 0.0f, 1.0f);
    return Frame;
}

FString UGratiaMusicPlayer::GetDiagnostics() const
{
    const FDeck& Now = Decks[Active];
    return FString::Printf(TEXT("track=%s time=%.1f gain=%.2f spatial=%d queued=%s other=%s"), Now.Track ? *Now.Track->GetName() : TEXT("-"),
        Now.Clock, Now.Gain, IsSpatial() ? 1 : 0, Queued ? *Queued->GetName() : TEXT("-"),
        Decks[1 - Active].bPlaying && Decks[1 - Active].Track ? *Decks[1 - Active].Track->GetName() : TEXT("-"));
}
