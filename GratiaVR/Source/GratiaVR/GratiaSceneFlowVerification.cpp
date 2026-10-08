#include "GratiaSceneFlowVerification.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaMusicPlayer.h"
#include "GratiaMenu.h"
#include "GratiaMenuWidget.h"
#include "GratiaPerformanceStage.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaSceneDirector.h"
#include "GratiaSceneLibrary.h"
#include "GratiaStage1Runtime.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaFlowQA, Log, All);

UGratiaSceneFlowVerification::UGratiaSceneFlowVerification()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

AGratiaStage1Runtime* UGratiaSceneFlowVerification::Runtime() const
{
    return Cast<AGratiaStage1Runtime>(GetOwner());
}

UGratiaSceneDirector* UGratiaSceneFlowVerification::Director() const
{
    return Runtime() ? Runtime()->SceneDirector.Get() : nullptr;
}

void UGratiaSceneFlowVerification::BeginPlay()
{
    Super::BeginPlay();
    bEnabled = FParse::Param(FCommandLine::Get(), TEXT("GratiaFlowQA"));
    SetComponentTickEnabled(bEnabled);
    if (bEnabled)
    {
        if (Director()) AddTickPrerequisiteComponent(Director());
        UE_LOG(LogGratiaFlowQA, Display, TEXT("FLOW_QA_BEGIN scope=rendered-desktop-scene-flow real_tracking=0 vr_acceptance=0"));
    }
}

void UGratiaSceneFlowVerification::Check(bool bPass, const FString& Description)
{
    ++Checks;
    if (bPass) { UE_LOG(LogGratiaFlowQA, Display, TEXT("FLOW_QA_CHECK PASS %s"), *Description); }
    else
    {
        ++Failures;
        UE_LOG(LogGratiaFlowQA, Error, TEXT("FLOW_QA_CHECK FAIL %s"), *Description);
    }
}

void UGratiaSceneFlowVerification::Capture(const FString& Name)
{
    const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/Windows"), TEXT("GratiaSceneFlow_") + Name + TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(File, true, false);
    UE_LOG(LogGratiaFlowQA, Display, TEXT("FLOW_QA_CAPTURE %s"), *Name);
}

void UGratiaSceneFlowVerification::ChangePhase(EPhase Next)
{
    Phase = Next;
    PhaseSeconds = 0.0f;
}

void UGratiaSceneFlowVerification::StartNextScene()
{
    UGratiaSceneDirector* Flow = Director();
    AGratiaStage1Runtime* Host = Runtime();
    bLoadingCapture = bPlayingCapture = bSawToLoading = bSawLoading = bSawToScene = false;
    TravelSeconds = LoadingSeconds = 0.0f;
    // Every entry plus a second trip into the first scene verifies unloading/re-entry.
    SceneIndex = CompletedScenes < OriginalLibrary->Scenes.Num() ? CompletedScenes : 0;
    FString Reason;
    Check(Flow->CanStartScene(SceneIndex, Reason), TEXT("Scene preflight: ") + OriginalLibrary->Scenes[SceneIndex].Id.ToString() + TEXT(" ") + Reason);
    Host->Menu->Execute(EGratiaMenuAction::StartScene, SceneIndex);
    Check(Flow->GetState() == EGratiaFlowState::ToLoading, TEXT("Scene card starts the fade through the menu API"));
    Check(!Host->Menu->bOpen, TEXT("Starting a scene closes the menu"));
    if (Flow->GetState() != EGratiaFlowState::ToLoading) { Complete(); return; }
    ChangePhase(EPhase::Travel);
}

void UGratiaSceneFlowVerification::CheckSettings()
{
    AGratiaStage1Runtime* Host = Runtime();
    UGratiaSceneDirector* Flow = Director();
    AGratiaPreviewCharacter* Character = Host->TargetCharacter.Get();
    UGratiaInteraction* Interaction = Character->Interaction;
    UGratiaUserSettings* Settings = Flow->GetUserSettings();
    if (!Settings) { Check(false, TEXT("Flow has transient user settings")); return; }
    Host->Menu->Toggle();
    Check(Host->Menu->bOpen && Host->Menu->GetWidget(), TEXT("Scene menu opens with a real widget"));
    Check(!Host->IsSceneInteractionAllowed(), TEXT("An open menu blocks scene interaction"));
    for (int32 Page = 0; Page < 5; ++Page) Host->Menu->Execute(EGratiaMenuAction::Tab, Page);
    Check(Host->Menu->bOpen, TEXT("All five menu pages remain accessible"));
    const int32 Quality = Interaction->Quality;
    for (int32 Profile = 0; Profile < 3; ++Profile)
    {
        Host->Menu->Execute(EGratiaMenuAction::Quality, Profile);
        Check(Interaction->Quality == Profile, FString::Printf(TEXT("Quality menu selects profile %d"), Profile));
    }
    Host->Menu->Execute(EGratiaMenuAction::Quality, Quality);
    auto Toggle = [this, Host](EGratiaMenuAction Action, bool& Value, const TCHAR* Name)
    {
        const bool Original = Value;
        Host->Menu->Execute(Action);
        Check(Value != Original, FString(Name) + TEXT(" switches through the menu"));
        Host->Menu->Execute(Action);
        Check(Value == Original, FString(Name) + TEXT(" restores through the menu"));
    };
    Toggle(EGratiaMenuAction::Hair, Interaction->bHairMotion, TEXT("Hair"));
    Toggle(EGratiaMenuAction::Cloth, Interaction->bClothMotion, TEXT("Cloth"));
    Toggle(EGratiaMenuAction::Body, Interaction->bBodyMotion, TEXT("Body"));
    Toggle(EGratiaMenuAction::Ears, Interaction->bEarMotion, TEXT("Ears"));
    Toggle(EGratiaMenuAction::Physics, Interaction->bPhysicalMotion, TEXT("Physics"));
    Flow->SetMusicVolume(1.0f);
    Host->Menu->Execute(EGratiaMenuAction::MusicDown);
    Check(Settings->MusicVolume < 1.0f, TEXT("Music volume down changes transient settings"));
    Host->Menu->Execute(EGratiaMenuAction::MusicUp);
    Check(FMath::IsNearlyEqual(Settings->MusicVolume, 1.0f), TEXT("Music volume up restores transient settings"));
    Flow->SetHapticsScale(1.0f);
    Host->Menu->Execute(EGratiaMenuAction::HapticsDown);
    Check(Settings->HapticsScale < 1.0f, TEXT("Haptic scale down changes transient settings"));
    Host->Menu->Execute(EGratiaMenuAction::HapticsUp);
    Check(FMath::IsNearlyEqual(Settings->HapticsScale, 1.0f), TEXT("Haptic scale up restores transient settings"));
    const float Height = Host->HeightOffsetCm;
    Host->Menu->Execute(EGratiaMenuAction::HeightUp);
    Check(Host->HeightOffsetCm > Height, TEXT("Height adjustment reaches the runtime"));
    Host->Menu->Execute(EGratiaMenuAction::HeightDown);
    Check(FMath::IsNearlyEqual(Host->HeightOffsetCm, Height), TEXT("Height adjustment restores"));
    Check(Interaction->bSound, TEXT("Sound starts enabled in the isolated QA settings"));
    Host->Menu->Execute(EGratiaMenuAction::Sound);
    Check(!Interaction->bSound, TEXT("Sound menu disables audio"));
}

void UGratiaSceneFlowVerification::CheckErrors()
{
    UGratiaSceneDirector* Flow = Director();
    const EGratiaFlowState Before = Flow->GetState();
    Check(!Flow->StartScene(INDEX_NONE), TEXT("Invalid negative scene index is rejected"));
    Check(Flow->GetState() == Before && !Flow->GetLastError().IsEmpty(), TEXT("Invalid index keeps the lobby and provides a reason"));
    Check(!Flow->StartScene(OriginalLibrary->Scenes.Num()), TEXT("Out-of-range scene index is rejected"));
    // Fault injection is confined to a transient library; shared authored assets are untouched.
    ErrorLibrary = DuplicateObject<UGratiaSceneLibrary>(OriginalLibrary, this);
    ErrorLibrary->Scenes[0].Environment = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Gratia/Experience/__FlowQAMissing.__FlowQAMissing")));
    Flow->Library = ErrorLibrary;
    FString Reason;
    Check(!Flow->CanStartScene(0, Reason) && !Reason.IsEmpty(), TEXT("Missing environment fails asset preflight with a reason"));
    Check(!Flow->StartScene(0), TEXT("Missing environment cannot start or silently become a studio scene"));
    Check(Flow->GetState() == Before && !Flow->GetLastError().IsEmpty(), TEXT("Missing asset leaves a safe lobby and explicit error"));
    ErrorLibrary->Scenes[0].Environment = OriginalLibrary->Scenes[0].Environment;
    ErrorLibrary->Scenes[0].Performance = TEXT("__FlowQAMissingPerformance");
    Check(!Flow->CanStartScene(0, Reason) && !Reason.IsEmpty(), TEXT("Missing performance fails preflight rather than becoming idle"));
    Check(!Flow->StartScene(0) && Flow->GetState() == Before, TEXT("Missing performance cannot change the safe lobby"));
    Flow->Library = OriginalLibrary;
    ErrorLibrary = nullptr;
}

void UGratiaSceneFlowVerification::Complete()
{
    if (bFinished) return;
    bFinished = true;
    if (Director() && OriginalLibrary) Director()->Library = OriginalLibrary;
    UE_LOG(LogGratiaFlowQA, Display, TEXT("GRATIA_FLOW_QA_%s checks=%d failures=%d scenes=%d seconds=%.2f real_vr_acceptance=0"),
        Failures ? TEXT("FAIL") : TEXT("PASS"), Checks, Failures, CompletedScenes, Elapsed);
    // In the editor (Play) the run only reports: quitting mid-PIE tears the editor down under its own viewport.
    if (!GetWorld()->IsPlayInEditor()) FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0, TEXT("GratiaSceneFlowQA"));
}

void UGratiaSceneFlowVerification::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    if (!bEnabled || bFinished) return;
    if (!FMath::IsFinite(Delta) || Delta < 0.0f) { Check(false, TEXT("Finite nonnegative flow tick")); Complete(); return; }
    Elapsed += Delta;
    PhaseSeconds += Delta;
    AGratiaStage1Runtime* Host = Runtime();
    UGratiaSceneDirector* Flow = Director();
    AGratiaPreviewCharacter* Character = Host ? Host->TargetCharacter.Get() : nullptr;
    if (Elapsed > 240.0f || PhaseSeconds > 35.0f)
    {
        Check(false, FString::Printf(TEXT("Flow phase timed out phase=%d %s"), int32(Phase), Flow ? *Flow->GetDiagnostics() : TEXT("missing director")));
        Complete(); return;
    }
    if (!Host || !Flow || !Host->Menu || !Character || !Character->Interaction) return;
    const EGratiaFlowState State = Flow->GetState();
    if (State != EGratiaFlowState::Off)
    {
        const bool bBlocked = Flow->IsSceneInputBlocked() || Host->Menu->bOpen;
        if (bBlocked && Host->IsSceneInteractionAllowed())
        {
            Check(false, TEXT("Lobby, fades, loading and menus must block interaction")); Complete(); return;
        }
        if (bBlocked && bBlockedLastTick && Character->Interaction->ReactionSerial != BlockedReactionSerial)
        {
            Check(false, TEXT("Blocked flow cannot produce a new reaction")); Complete(); return;
        }
        BlockedReactionSerial = Character->Interaction->ReactionSerial;
        bBlockedLastTick = bBlocked;
    }
    switch (Phase)
    {
    case EPhase::WaitLobby:
        if (!Host->bPawnReady || State != EGratiaFlowState::Lobby) break;
        OriginalLibrary = Flow->Library;
        Check(OriginalLibrary && OriginalLibrary->Scenes.Num() >= 2, TEXT("Cooked scene library has at least two entries"));
        if (!OriginalLibrary || OriginalLibrary->Scenes.Num() < 2) { Complete(); return; }
        {
            int32 Environments = 0, Performances = 0;
            TSet<FName> Ids;
            for (const FGratiaSceneEntry& Entry : OriginalLibrary->Scenes)
            {
                Check(!Entry.Id.IsNone() && !Ids.Contains(Entry.Id), TEXT("Scene ID is nonempty and unique: ") + Entry.Id.ToString());
                Ids.Add(Entry.Id);
                if (Entry.Performance.IsNone() && !Entry.Environment.IsNull()) ++Environments;
                if (!Entry.Performance.IsNone()) ++Performances;
                const TSoftObjectPtr<UGratiaMusicAnalysis>& MusicRef = Entry.Performance.IsNone() ? Entry.Music : Entry.PerformanceMusic;
                if (Entry.Performance.IsNone() || !MusicRef.IsNull())
                {
                    const UGratiaMusicAnalysis* Music = MusicRef.LoadSynchronous();
                    Check(Music && Music->Sound && !Music->Frames.IsEmpty() && Music->FramesPerSecond > 0.0f,
                        TEXT("Cooked music and analysis exist for scene: ") + Entry.Id.ToString());
                }
            }
            Check(Environments >= 2, TEXT("Library contains at least two environmental free-play scenes"));
            Check(Performances >= 1, TEXT("Library includes existing performances"));
        }
        Check(Host->Menu->bOpen && Host->Menu->GetWidget(), TEXT("Startup presents the scene menu in the lobby"));
        Check(Character->IsHidden() && !Character->GetActorEnableCollision(), TEXT("Lobby parks the character and disables its collision"));
        Check(!Host->IsSceneInteractionAllowed(), TEXT("Lobby blocks interaction"));
        ChangePhase(EPhase::LobbyCapture);
        break;
    case EPhase::LobbyCapture:
        if (PhaseSeconds < 0.6f) break;
        if (!bLobbyCapture) { Capture(TEXT("Lobby")); bLobbyCapture = true; break; }
        if (PhaseSeconds < 0.9f) break;
        StartNextScene();
        break;
    case EPhase::Travel:
        TravelSeconds += Delta;
        if (State == EGratiaFlowState::Loading) LoadingSeconds += Delta;
        bSawToLoading |= State == EGratiaFlowState::ToLoading;
        bSawLoading |= State == EGratiaFlowState::Loading;
        bSawToScene |= State == EGratiaFlowState::ToScene;
        // Captured once the fade into the loading space has finished (it starts black).
        if (State == EGratiaFlowState::Loading && !bLoadingCapture && LoadingSeconds >= OriginalLibrary->FadeSeconds + 0.4f)
        {
            Check(Character->IsHidden() && !Character->GetActorEnableCollision(), TEXT("Loading keeps the character hidden and noncolliding"));
            Capture(FString::Printf(TEXT("Loading_%02d"), CompletedScenes));
            bLoadingCapture = true;
            Check(!Flow->StartScene(SceneIndex), TEXT("Repeated start during loading is rejected"));
        }
        if (State != EGratiaFlowState::Playing) break;
        Check(bSawToLoading && bSawLoading && bSawToScene, TEXT("Scene traverses fade, loading and scene fade states"));
        Check(LoadingSeconds + 0.15f >= OriginalLibrary->MinLoadingSeconds, TEXT("Loading respects its configured minimum dwell"));
        Check(Flow->GetCurrentScene() == SceneIndex, TEXT("The selected scene becomes current"));
        Check(!Character->IsHidden() && Character->GetActorEnableCollision(), TEXT("Playing restores the character and collision"));
        if (!OriginalLibrary->Scenes[SceneIndex].Environment.IsNull())
        {
            const ULevelStreamingDynamic* Environment = Flow->GetStreamedEnvironment();
            Check(Environment && Environment->IsLevelLoaded() && Environment->IsLevelVisible(), TEXT("The selected environment is actually streamed and visible"));
            Check(Flow->GetVisibleBackdrops() == OriginalLibrary->Scenes[SceneIndex].Backdrops.Num(), TEXT("Every backdrop of the environment is streamed and visible"));
        }
        Check(Character->GetActorTransform().ContainsNaN() == false, TEXT("Scene placement has finite transforms"));
        ChangePhase(EPhase::Settle);
        break;
    case EPhase::Settle:
        if (PhaseSeconds < OriginalLibrary->FadeSeconds + 0.6f) break;
        if (!bPlayingCapture)
        {
            Capture(FString::Printf(TEXT("Playing_%02d"), CompletedScenes));
            bPlayingCapture = true;
            break;
        }
        if (PhaseSeconds < OriginalLibrary->FadeSeconds + 0.9f) break;
        Check(Host->IsSceneInteractionAllowed(), TEXT("Playing restores scene interaction after fade"));
        {
            const FVector4f Frame = Flow->GetMusicFrame();
            Check(FMath::IsFinite(Frame.X) && FMath::IsFinite(Frame.Y) && FMath::IsFinite(Frame.Z) && FMath::IsFinite(Frame.W), TEXT("Music analysis is finite"));
        }
        if (!OriginalLibrary->Scenes[SceneIndex].Performance.IsNone())
        {
            Check(Flow->IsPerformanceScene(), TEXT("Performance entry plays its configured character performance"));
            if (OriginalLibrary->Scenes[SceneIndex].bStartInPartnerView)
                Check(Host->IsPartnerView(), TEXT("Performance enters its configured partner viewpoint"));
            if (Character->GetPerformance() && Character->GetPerformance()->Scene.Music)
                Check(Character->PerformanceStage && Character->PerformanceStage->IsMusicPlaying(), TEXT("Performance scene music plays"));
        }
        else Check(Character->IsIdlePreview(), TEXT("Free play begins in idle"));
        if (UGratiaMusicPlayer* Music = Flow->GetMusicPlayer(); Music && OriginalLibrary->Scenes[SceneIndex].Performance.IsNone() && !Music->Playlist.IsEmpty())
        {
            // Free play: the scene's track plays from the environment speakers; the menu mixes tracks on beats.
            Check(Music->IsPlaying() && Music->GetCurrent() == OriginalLibrary->Scenes[SceneIndex].Music.Get(), TEXT("Free play plays the scene's track"));
            if (!OriginalLibrary->Scenes[SceneIndex].Environment.IsNull())
                Check(Music->IsSpatial(), TEXT("Environment speakers carry the music"));
            TrackBefore = Music->GetCurrent();
            Host->Menu->Execute(EGratiaMenuAction::TrackNext);
            Check(Music->GetCurrent() && Music->GetCurrent() != TrackBefore, TEXT("Next track is chosen from the playlist"));
            ChangePhase(EPhase::TrackNext);
            break;
        }
        CheckSettings();
        ChangePhase(EPhase::SoundOff);
        break;
    case EPhase::TrackNext:
    case EPhase::TrackBack:
    {
        // A change lands on the playing track's next beat (under a second), then crossfades for seconds.
        if (PhaseSeconds < 1.6f) break;
        UGratiaMusicPlayer* Music = Flow->GetMusicPlayer();
        Check(Music && !Music->IsQueued() && Music->IsPlaying(), TEXT("Track change starts on a beat"));
        Check(Music && Music->GetAudibleDecks() == 2, TEXT("Track change crossfades both decks"));
        if (Phase == EPhase::TrackNext)
        {
            Host->Menu->Execute(EGratiaMenuAction::TrackPrev);
            Check(Music && Music->GetCurrent() == TrackBefore, TEXT("Previous track returns to the scene's track"));
            ChangePhase(EPhase::TrackBack);
            break;
        }
        CheckSettings();
        ChangePhase(EPhase::SoundOff);
        break;
    }
    case EPhase::SoundOff:
        if (PhaseSeconds < 0.4f) break;
        if (Flow->IsPerformanceScene() && Character->PerformanceStage)
            Check(!Character->PerformanceStage->IsMusicPlaying(), TEXT("Sound-off stops performance music"));
        Host->Menu->Execute(EGratiaMenuAction::Sound);
        Check(Character->Interaction->bSound, TEXT("Sound restores through the menu"));
        if (Flow->IsPerformanceScene() && Character->PerformanceStage)
        {
            Host->Menu->Execute(EGratiaMenuAction::Pause);
            Check(Character->IsPerformancePaused(), TEXT("Playback menu pauses the performance"));
            PausedTime = Character->PerformanceStage->GetPerformanceTime();
            ChangePhase(EPhase::Pause);
        }
        else
        {
            Host->Menu->Execute(EGratiaMenuAction::Reset);
            Check(Character->IsIdlePreview(), TEXT("Free-play reset returns to idle"));
            Host->Menu->Close();
            Flow->ExitToLobby();
            ChangePhase(EPhase::Return);
        }
        break;
    case EPhase::Pause:
        if (PhaseSeconds < 0.4f) break;
        Check(FMath::Abs(Character->PerformanceStage->GetPerformanceTime() - PausedTime) <= 0.05f, TEXT("Paused performance time stays fixed across frames"));
        Host->Menu->Execute(EGratiaMenuAction::Pause);
        Check(!Character->IsPerformancePaused(), TEXT("Playback menu resumes"));
        {
            const float Rate = Character->GetPerformanceRate();
            Host->Menu->Execute(EGratiaMenuAction::SpeedUp);
            Check(Character->GetPerformanceRate() > Rate, TEXT("Playback speed increases"));
            Host->Menu->Execute(EGratiaMenuAction::SpeedDown);
            Check(FMath::IsNearlyEqual(Character->GetPerformanceRate(), Rate), TEXT("Playback speed restores"));
            if (Character->GetPerformancePartCount() > 1)
            {
                Host->Menu->Execute(EGratiaMenuAction::NextPart);
                Check(Character->PerformancePart == 1, TEXT("Next part seeks the segmented performance"));
                Host->Menu->Execute(EGratiaMenuAction::PrevPart);
                Check(Character->PerformancePart == 0, TEXT("Previous part returns to its first segment"));
            }
            Host->Menu->Execute(EGratiaMenuAction::Restart);
            Check(Character->PerformancePart == 0 && Character->PerformanceStage->GetPerformanceTime() < 0.15f, TEXT("Restart resets the performance clock and part"));
            PausedTime = Character->PerformanceStage->GetPerformanceTime();
        }
        ChangePhase(EPhase::Resume);
        break;
    case EPhase::Resume:
        if (PhaseSeconds < 0.6f) break;
        Check(Character->PerformanceStage->GetPerformanceTime() > PausedTime + 0.1f, TEXT("Resumed performance advances across frames"));
        if (Character->GetPerformance() && Character->GetPerformance()->Scene.Music)
            Check(Character->PerformanceStage->IsMusicPlaying() && FMath::Abs(Character->PerformanceStage->GetMusicTime() - Character->PerformanceStage->GetPerformanceTime()) < 0.6f, TEXT("Music resumes in sync with restarted performance"));
        Host->Menu->Close();
        Flow->ExitToLobby();
        ChangePhase(EPhase::Return);
        break;
    case EPhase::Return:
        if (State != EGratiaFlowState::Lobby || PhaseSeconds < OriginalLibrary->FadeSeconds + 0.25f) break;
        Check(Host->Menu->bOpen && Character->IsHidden(), TEXT("Return shows the lobby menu and parks the character"));
        Check(!Flow->GetStreamedEnvironment() && !Host->IsPartnerView(), TEXT("Return unloads the environment and clears partner view"));
        Check(!Host->IsSceneInteractionAllowed(), TEXT("Returned lobby blocks contacts"));
        ++CompletedScenes;
        if (CompletedScenes <= OriginalLibrary->Scenes.Num()) StartNextScene();
        else { CheckErrors(); Capture(TEXT("FinalLobby")); ChangePhase(EPhase::Finish); }
        break;
    case EPhase::Finish:
        if (PhaseSeconds >= 0.6f) Complete();
        break;
    }
}
