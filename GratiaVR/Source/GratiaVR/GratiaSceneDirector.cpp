#include "GratiaSceneDirector.h"
#include "GratiaAnimInstance.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaLoadingSpace.h"
#include "GratiaMenu.h"
#include "GratiaMusicPlayer.h"
#include "GratiaPerformanceStage.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaSoftBodyInteraction.h"
#include "GratiaStage1Runtime.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"
#include "Sound/ReverbEffect.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaScenes, Log, All);

namespace
{
const FName GratiaStudioTag(TEXT("GratiaStudio"));
const FName GratiaCharacterSpotTag(TEXT("GratiaCharacterSpot"));
const FName GratiaPlayerSpotTag(TEXT("GratiaPlayerSpot"));
const FName GratiaAudioLightTag(TEXT("GratiaAudioLight"));

AActor* GratiaFindTagged(const ULevel* Level, FName Tag)
{
    if (!Level) return nullptr;
    for (AActor* Actor : Level->Actors)
        if (Actor && Actor->ActorHasTag(Tag)) return Actor;
    return nullptr;
}

FString GratiaClock(float Seconds)
{
    const int32 Total = FMath::Max(0, FMath::FloorToInt(Seconds));
    return FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);
}
}

UGratiaSceneDirector::UGratiaSceneDirector()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

AGratiaStage1Runtime* UGratiaSceneDirector::GetRuntime() const { return Cast<AGratiaStage1Runtime>(GetOwner()); }

AGratiaPreviewCharacter* UGratiaSceneDirector::GetCharacter() const
{
    const AGratiaStage1Runtime* Runtime = GetRuntime();
    return Runtime ? Runtime->TargetCharacter.Get() : nullptr;
}

const FGratiaSceneEntry* UGratiaSceneDirector::GetCurrentEntry() const
{
    return Library && Library->Scenes.IsValidIndex(Current) ? &Library->Scenes[Current] : nullptr;
}

bool UGratiaSceneDirector::IsPerformanceScene() const
{
    const AGratiaPreviewCharacter* Character = GetCharacter();
    return State == EGratiaFlowState::Playing && Character && Character->PreviewPose == EGratiaPreviewPose::Performance;
}

float UGratiaSceneDirector::GetFadeSeconds() const
{
    return Library && FMath::IsFinite(Library->FadeSeconds) ? FMath::Clamp(Library->FadeSeconds, 0.05f, 3.0f) : 0.6f;
}

float UGratiaSceneDirector::GetLoadingTimeout() const
{
    const float Configured = Library && FMath::IsFinite(Library->LoadingTimeoutSeconds) ? FMath::Clamp(Library->LoadingTimeoutSeconds, 1.0f, 120.0f) : 30.0f;
    const float Minimum = Library && FMath::IsFinite(Library->MinLoadingSeconds) ? FMath::Clamp(Library->MinLoadingSeconds, 0.0f, 10.0f) : 2.0f;
    return FMath::Max(Configured, Minimum + GetFadeSeconds() + 1.0f);
}

bool UGratiaSceneDirector::IsSceneInputBlocked() const
{
    return bPendingStart || (State != EGratiaFlowState::Off && (State != EGratiaFlowState::Playing || StateSeconds < GetFadeSeconds()));
}

bool UGratiaSceneDirector::CanStartScene(int32 Index, FString& Reason) const
{
    Reason.Reset();
    if (!Library || !Library->Scenes.IsValidIndex(Index)) { Reason = TEXT("Scene is missing from the library."); return false; }
    const FGratiaSceneEntry& Entry = Library->Scenes[Index];
    if (Entry.Id.IsNone()) { Reason = TEXT("Scene has no ID."); return false; }
    for (int32 Other = 0; Other < Library->Scenes.Num(); ++Other)
        if (Other != Index && Library->Scenes[Other].Id == Entry.Id) { Reason = TEXT("Scene ID is duplicated in the library."); return false; }
    if (!Entry.Environment.IsNull() && !FPackageName::DoesPackageExist(Entry.Environment.ToSoftObjectPath().GetLongPackageName()))
    { Reason = FString::Printf(TEXT("Environment is unavailable: %s"), *Entry.Environment.ToString()); return false; }
    for (const TSoftObjectPtr<UGratiaMusicAnalysis>& Track : {Entry.Music, Entry.PerformanceMusic})
        if (!Track.IsNull() && !FPackageName::DoesPackageExist(Track.ToSoftObjectPath().GetLongPackageName()))
        { Reason = FString::Printf(TEXT("Music analysis is unavailable: %s"), *Track.ToString()); return false; }
    const AGratiaPreviewCharacter* Character = GetCharacter();
    if (!Character || !Character->CharacterProfile || !Character->CharacterMesh || !Character->CharacterMesh->GetSkeletalMeshAsset())
    { Reason = TEXT("Character profile or mesh is unavailable."); return false; }
    if (Entry.Performance.IsNone())
    {
        if (Entry.bStartInPartnerView) { Reason = TEXT("Partner view requires a performance."); return false; }
        return true;
    }
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    if (Profile->AnimationClass && !Profile->AnimationClass->IsChildOf(UGratiaAnimInstance::StaticClass()))
    { Reason = TEXT("This profile's Animation Blueprint does not support single-node performance playback."); return false; }
    const int32 PerformanceIndex = Character->FindPerformance(Entry.Performance);
    if (!Profile->PerformanceClips.IsValidIndex(PerformanceIndex))
    { Reason = FString::Printf(TEXT("Performance '%s' is unavailable in this character profile."), *Entry.Performance.ToString()); return false; }
    const FGratiaPerformanceClip& Performance = Profile->PerformanceClips[PerformanceIndex];
    for (int32 Part = 0; Part < Performance.NumParts(); ++Part)
    {
        const UAnimSequence* Clip = Performance.GetPart(Part);
        if (!Clip || !Clip->GetSkeleton() || !Clip->GetSkeleton()->IsCompatibleMesh(Character->CharacterMesh->GetSkeletalMeshAsset())
            || !FMath::IsFinite(Clip->GetPlayLength()) || Clip->GetPlayLength() <= 0.0f)
        { Reason = FString::Printf(TEXT("Performance '%s' part %d is missing or incompatible with this skeleton."), *Entry.Performance.ToString(), Part + 1); return false; }
    }
    if (Entry.bStartInPartnerView && (!Performance.Scene.bHasViewpoint || Performance.Scene.Viewpoint.ContainsNaN()))
    { Reason = TEXT("This performance has no valid partner viewpoint."); return false; }
    return true;
}

void UGratiaSceneDirector::BeginPlay()
{
    Super::BeginPlay();
    // Tests start straight in the studio room; -GratiaLobby / -GratiaFlowQA ask for the flow.
    FString Command = FCommandLine::Get();
    bFlowQA = FParse::Param(*Command, TEXT("GratiaFlowQA"));
    Command.ReplaceInline(TEXT("-GratiaFlowQA"), TEXT(""), ESearchCase::IgnoreCase);
    Command.ReplaceInline(TEXT("-GratiaLobby"), TEXT(""), ESearchCase::IgnoreCase);
    // -GratiaScene=<Id> keeps the scene flow on in test runs and opens that scene without the lobby.
    FString Pinned;
    if (FParse::Value(*Command, TEXT("GratiaScene="), Pinned)) PinnedScene = FName(*Pinned);
    bTestMode = !bFlowQA && PinnedScene.IsNone() && Command.Contains(TEXT("-Gratia"), ESearchCase::IgnoreCase);
    if (!Library) Library = LoadObject<UGratiaSceneLibrary>(nullptr, TEXT("/Game/Gratia/Experience/DA_SceneLibrary.DA_SceneLibrary"));
    Settings = bTestMode || bFlowQA || !PinnedScene.IsNone() ? NewObject<UGratiaUserSettings>(this) : UGratiaUserSettings::Load();
    if (!Library || Library->Scenes.IsEmpty() || bTestMode)
    {
        UE_LOG(LogGratiaScenes, Display, TEXT("SCENES off (%s)"), bTestMode ? TEXT("test run") : TEXT("no scene library"));
        return;
    }
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Space = GetWorld()->SpawnActor<AGratiaLoadingSpace>(Spawn);
    if (!Space || !Space->Configure(Library))
    {
        LastError = TEXT("Loading-space mesh or material is unavailable.");
        UE_LOG(LogGratiaScenes, Error, TEXT("SCENES unavailable: %s"), *LastError);
        if (Space) Space->Destroy();
        Space = nullptr;
        return;
    }
    Music = NewObject<UGratiaMusicPlayer>(GetOwner(), TEXT("SceneMusic"));
    Music->CrossfadeSeconds = Library->CrossfadeSeconds;
    Music->SpeakerAttenuation = Library->SpeakerAttenuation;
    for (const TSoftObjectPtr<UGratiaMusicAnalysis>& Track : Library->Playlist)
        if (UGratiaMusicAnalysis* Loaded = Track.LoadSynchronous()) Music->Playlist.Add(Loaded);
    Music->RegisterComponent();
    if (GetCharacter() && GetCharacter()->PerformanceStage) AddTickPrerequisiteComponent(GetCharacter()->PerformanceStage);
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        if (It->ActorHasTag(GratiaStudioTag)) StudioActors.Add(*It);
    bPendingStart = true;
    UE_LOG(LogGratiaScenes, Display, TEXT("SCENES library=%s scenes=%d studio_actors=%d"), *Library->GetName(), Library->Scenes.Num(), StudioActors.Num());
}

void UGratiaSceneDirector::EndPlay(const EEndPlayReason::Type Reason)
{
    UnloadEnvironment();
    if (Music) Music->Stop(0.0f);
    if (Space) Space->Destroy();
    Super::EndPlay(Reason);
}

void UGratiaSceneDirector::ApplyUserSettings()
{
    AGratiaStage1Runtime* Runtime = GetRuntime();
    AGratiaPreviewCharacter* Character = GetCharacter();
    if (!Settings || !Runtime || !Character || !Character->Interaction) return;
    UGratiaInteraction* Interaction = Character->Interaction;
    if (Runtime->Menu) Runtime->Menu->ApplyQuality(FMath::Clamp(Settings->Quality, 0, 2), false);
    Interaction->bHairMotion = Settings->bHair;
    Interaction->bClothMotion = Settings->bCloth;
    Interaction->bBodyMotion = Settings->bBody;
    Interaction->bEarMotion = Settings->bEars;
    Interaction->bPhysicalMotion = Settings->bPhysics;
    Interaction->bLocalSpring = Settings->bSprings;
    Interaction->bSound = Settings->bSound;
    Runtime->AdjustHeight(Settings->HeightOffsetCm - Runtime->HeightOffsetCm);
    SetMusicVolume(Settings->MusicVolume);
}

void UGratiaSceneDirector::SaveUserSettings()
{
    const AGratiaStage1Runtime* Runtime = GetRuntime();
    const AGratiaPreviewCharacter* Character = GetCharacter();
    if (!Settings || !Character || !Character->Interaction) return;
    const UGratiaInteraction* Interaction = Character->Interaction;
    Settings->Quality = Interaction->Quality;
    Settings->bHair = Interaction->bHairMotion;
    Settings->bCloth = Interaction->bClothMotion;
    Settings->bBody = Interaction->bBodyMotion;
    Settings->bEars = Interaction->bEarMotion;
    Settings->bPhysics = Interaction->bPhysicalMotion;
    Settings->bSprings = Interaction->bLocalSpring;
    Settings->bSound = Interaction->bSound;
    if (Runtime && !Runtime->IsPartnerView()) Settings->HeightOffsetCm = Runtime->HeightOffsetCm;
    Settings->Sanitize();
    if (!bTestMode && !bFlowQA && PinnedScene.IsNone() && !Settings->Save()) UE_LOG(LogGratiaScenes, Warning, TEXT("SCENE_SETTINGS failed to save player settings"));
}

void UGratiaSceneDirector::SetMusicVolume(float Volume)
{
    if (!Settings) return;
    Settings->MusicVolume = FMath::Clamp(FMath::IsFinite(Volume) ? Volume : 1.0f, 0.0f, 2.0f);
    if (AGratiaPreviewCharacter* Character = GetCharacter())
        if (Character->PerformanceStage) Character->PerformanceStage->SetMusicVolumeScale(Settings->MusicVolume);
    if (Music) Music->SetMasterVolume(Settings->MusicVolume);
    if (AGratiaPreviewCharacter* Character = GetCharacter())
        if (Character->PerformanceStage && bPlaylistOverride) Character->PerformanceStage->SetMusicVolumeScale(0.0f);
}

void UGratiaSceneDirector::SetHapticsScale(float Scale)
{
    if (Settings) Settings->HapticsScale = FMath::Clamp(FMath::IsFinite(Scale) ? Scale : 1.0f, 0.0f, 1.0f);
}

void UGratiaSceneDirector::Fade(float From, float To)
{
    APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (Camera && Library) Camera->StartCameraFade(From, To, GetFadeSeconds(), FLinearColor::Black, false, To > 0.5f);
    StateSeconds = 0.0f;
}

FVector UGratiaSceneDirector::HeadFloor(float& OutYaw) const
{
    const AGratiaStage1Runtime* Runtime = GetRuntime();
    const UCameraComponent* Camera = Runtime ? Runtime->GetPlayerCamera() : nullptr;
    const APawn* Pawn = Runtime ? Runtime->GetPlayerPawn() : nullptr;
    if (!Camera || !Pawn) { OutYaw = Runtime ? float(Runtime->GetActorRotation().Yaw) : 0.0f; return Runtime ? Runtime->GetActorLocation() : FVector::ZeroVector; }
    OutYaw = float(Camera->GetComponentRotation().Yaw);
    return FVector(Camera->GetComponentLocation().X, Camera->GetComponentLocation().Y, Pawn->GetActorLocation().Z);
}

void UGratiaSceneDirector::ShowLoading(const FText& Title, const FText& Subtitle, const FLinearColor& Accent, UTexture2D* Picture)
{
    if (!Space) return;
    float Yaw = 0.0f;
    const FVector Floor = HeadFloor(Yaw);
    Space->ShowAt(Floor, Yaw, Title, Subtitle, Accent, Picture);
}

void UGratiaSceneDirector::ParkCharacter()
{
    AGratiaPreviewCharacter* Character = GetCharacter();
    AGratiaStage1Runtime* Runtime = GetRuntime();
    if (!Character) return;
    CancelContacts();
    if (Runtime) Runtime->SetPartnerView(false);
    Character->ResetToIdle();
    Character->SetActorHiddenInGame(true);
    Character->SetActorEnableCollision(false);
}

void UGratiaSceneDirector::CancelContacts()
{
    AGratiaPreviewCharacter* Character = GetCharacter();
    if (!Character) return;
    if (Character->Interaction)
    {
        Character->Interaction->SetHandSample(true, FTransform::Identity, FTransform::Identity, false);
        Character->Interaction->SetHandSample(false, FTransform::Identity, FTransform::Identity, false);
        Character->Interaction->ResetState();
    }
    if (Character->SoftBodyInteraction) Character->SoftBodyInteraction->ResetSoftBody();
}

bool UGratiaSceneDirector::IsSoundEnabled() const
{
    const AGratiaPreviewCharacter* Character = GetCharacter();
    return Character && Character->Interaction && Character->Interaction->bSound
        && Character->CharacterProfile && Character->CharacterProfile->Capabilities.bSound;
}

void UGratiaSceneDirector::SetStudioVisible(bool bVisible)
{
    for (const TWeakObjectPtr<AActor>& Weak : StudioActors)
    {
        AActor* Actor = Weak.Get();
        if (!Actor) continue;
        Actor->SetActorHiddenInGame(!bVisible);
        Actor->SetActorEnableCollision(bVisible);
        // Lights, sky and fog stop affecting the environment only when their components hide.
        Actor->ForEachComponent<USceneComponent>(false, [bVisible](USceneComponent* Component) { Component->SetVisibility(bVisible); });
        if (APostProcessVolume* Volume = Cast<APostProcessVolume>(Actor)) Volume->bEnabled = bVisible;
    }
}

void UGratiaSceneDirector::UnloadEnvironment()
{
    RestoreReactiveLights();
    for (ULevelStreamingDynamic* Stream : GetStreams())
    {
        Stream->SetShouldBeVisible(false);
        Stream->SetShouldBeLoaded(false);
        Stream->SetIsRequestingUnloadAndRemoval(true);
    }
    Streamed = nullptr;
    Backdrops.Reset();
    bVisibilityRequested = false;
}

TArray<ULevelStreamingDynamic*> UGratiaSceneDirector::GetStreams() const
{
    TArray<ULevelStreamingDynamic*> Streams;
    if (Streamed) Streams.Add(Streamed);
    for (ULevelStreamingDynamic* Stream : Backdrops)
        if (Stream) Streams.Add(Stream);
    return Streams;
}

bool UGratiaSceneDirector::AreStreamsVisible() const
{
    for (const ULevelStreamingDynamic* Stream : GetStreams())
        if (!Stream->IsLevelVisible()) return false;
    return true;
}

void UGratiaSceneDirector::SetStreamsVisible(bool bVisible)
{
    for (ULevelStreamingDynamic* Stream : GetStreams()) Stream->SetShouldBeVisible(bVisible);
}

int32 UGratiaSceneDirector::GetVisibleBackdrops() const
{
    int32 Count = 0;
    for (const ULevelStreamingDynamic* Stream : Backdrops)
        Count += Stream && Stream->IsLevelLoaded() && Stream->IsLevelVisible();
    return Count;
}

void UGratiaSceneDirector::RestoreReactiveLights()
{
    for (const FReactiveLight& Light : ReactiveLights)
        if (ULightComponent* Component = Light.Light.Get()) Component->SetIntensity(Light.BaseIntensity);
    ReactiveLights.Reset();
}

bool UGratiaSceneDirector::IsEnvironmentReady() const
{
    const FGratiaSceneEntry* Entry = GetCurrentEntry();
    if (!Entry || (!Entry->Environment.IsNull() && !Streamed) || Backdrops.Num() != Entry->Backdrops.Num()) return false;
    for (const ULevelStreamingDynamic* Stream : GetStreams())
        if (!Stream->IsLevelLoaded()) return false;
    return true;
}

void UGratiaSceneDirector::FailScene(const FString& Reason)
{
    LastError = Reason;
    Pending = INDEX_NONE;
    CancelContacts();
    UE_LOG(LogGratiaScenes, Warning, TEXT("SCENE_FAILED %s"), *Reason);
    if (State == EGratiaFlowState::Lobby)
    {
        if (Space) Space->ShowNotice(FText::FromString(LastError));
        return;
    }
    EnterLobby();
}

void UGratiaSceneDirector::EnterLobby()
{
    if (!Library || !Space) return;
    if (State == EGratiaFlowState::Off)
    {
        // First lobby after start: from black straight into the lobby.
        FinishLobby();
        return;
    }
    if (State == EGratiaFlowState::Lobby || State == EGratiaFlowState::ToLobby) return;
    CancelContacts();
    if (AGratiaStage1Runtime* Runtime = GetRuntime(); Runtime && Runtime->Menu) Runtime->Menu->Close();
    Fade(0.0f, 1.0f);
    State = EGratiaFlowState::ToLobby;
}

void UGratiaSceneDirector::ExitToLobby() { EnterLobby(); }

bool UGratiaSceneDirector::StartScene(int32 Index)
{
    FString Reason;
    if (!CanStartScene(Index, Reason))
    {
        LastError = Reason;
        UE_LOG(LogGratiaScenes, Warning, TEXT("SCENE_REJECTED %s"), *Reason);
        if (State == EGratiaFlowState::Lobby && Space) Space->ShowNotice(FText::FromString(LastError));
        return false;
    }
    if (!Space || !bHomeSaved || (State != EGratiaFlowState::Lobby && State != EGratiaFlowState::Playing)) return false;
    LastError.Reset();
    CancelContacts();
    Pending = Index;
    if (AGratiaStage1Runtime* Runtime = GetRuntime()) if (Runtime->Menu) Runtime->Menu->Close();
    Fade(0.0f, 1.0f);
    State = EGratiaFlowState::ToLoading;
    UE_LOG(LogGratiaScenes, Display, TEXT("SCENE_START %s"), *Library->Scenes[Index].Id.ToString());
    return true;
}

void UGratiaSceneDirector::FinishLobby()
{
    AGratiaStage1Runtime* Runtime = GetRuntime();
    UnloadEnvironment();
    ParkCharacter();
    SetStudioVisible(false);
    PerformanceTrack = nullptr;
    Current = INDEX_NONE;
    Movers.Reset();
    UGameplayStatics::DeactivateReverbEffect(this, TEXT("GratiaScene"));
    if (Music) Music->SetSpeakers(nullptr, nullptr);
    PlayAmbient(Library->LobbyMusic, Library->LobbyMusicVolume);
    if (Runtime) Runtime->PlaceAnchor(AnchorHome);
    ShowLoading(FText::GetEmpty(), FText::GetEmpty(), FLinearColor(0.95f, 0.35f, 0.65f));
    if (!LastError.IsEmpty()) Space->ShowNotice(FText::FromString(LastError));
    if (Runtime)
    {
        if (Runtime->Menu) Runtime->Menu->OpenLobby();
    }
    Fade(1.0f, 0.0f);
    State = EGratiaFlowState::Lobby;
    Pending = INDEX_NONE;
    UE_LOG(LogGratiaScenes, Display, TEXT("SCENE_LOBBY"));
}

void UGratiaSceneDirector::FinishScene()
{
    AGratiaStage1Runtime* Runtime = GetRuntime();
    AGratiaPreviewCharacter* Character = GetCharacter();
    const FGratiaSceneEntry* Entry = GetCurrentEntry();
    if (!Runtime || !Character || !Entry) { FailScene(TEXT("Character or scene disappeared during loading.")); return; }
    FString Reason;
    if (!CanStartScene(Current, Reason)) { FailScene(Reason); return; }
    const ULevel* Level = Streamed ? Streamed->GetLoadedLevel() : nullptr;
    if (!Entry->Environment.IsNull() && (!Level || !Streamed->IsLevelVisible())) { FailScene(TEXT("Environment did not become visible.")); return; }
    if (GetVisibleBackdrops() != Entry->Backdrops.Num()) { FailScene(TEXT("Environment backdrop did not become visible.")); return; }
    const AActor* CharacterSpot = GratiaFindTagged(Level, GratiaCharacterSpotTag);
    const AActor* PlayerSpot = GratiaFindTagged(Level, GratiaPlayerSpotTag);
    if (Level)
    {
        int32 CharacterMarkers = 0, PlayerMarkers = 0;
        for (const AActor* Actor : Level->Actors)
            if (Actor) { CharacterMarkers += Actor->ActorHasTag(GratiaCharacterSpotTag); PlayerMarkers += Actor->ActorHasTag(GratiaPlayerSpotTag); }
        if (CharacterMarkers > 1 || PlayerMarkers > 1) { FailScene(TEXT("Environment has ambiguous character/player markers.")); return; }
    }
    SetStudioVisible(!Level);
    const FTransform Place = CharacterSpot ? FTransform(CharacterSpot->GetActorRotation(), CharacterSpot->GetActorLocation()) : CharacterHome;
    Character->SetActorTransform(Place, false, nullptr, ETeleportType::TeleportPhysics);
    Character->SetActorHiddenInGame(false);
    Character->SetActorEnableCollision(true);
    const int32 Performance = Character->FindPerformance(Entry->Performance);
    if (!Entry->Performance.IsNone() && (Performance == INDEX_NONE || !Character->SetPerformance(Performance)))
    { FailScene(FString::Printf(TEXT("Performance '%s' could not start."), *Entry->Performance.ToString())); return; }
    if (Performance != INDEX_NONE && (!Character->CharacterMesh->GetSingleNodeInstance()
        || Character->CharacterMesh->GetSingleNodeInstance()->GetAnimationAsset() != Character->GetPerformance()->GetPart(0)))
    { FailScene(TEXT("Performance animation instance did not accept the clip.")); return; }
    if (Performance != INDEX_NONE) Character->SetPerformancePlayback(false, Character->GetPerformanceRate());
    else Character->ResetToIdle();
    if (Character->SoftBodyInteraction) Character->SoftBodyInteraction->ResetSoftBody();
    // The player stands at the environment's marker, or 1.5 m in front of the character facing her.
    FTransform Stand = AnchorHome;
    if (PlayerSpot) Stand = FTransform(FRotator(0, PlayerSpot->GetActorRotation().Yaw, 0), PlayerSpot->GetActorLocation());
    else if (CharacterSpot)
    {
        const FVector Forward = Place.TransformVectorNoScale(Character->CharacterProfile ? Character->CharacterProfile->ForwardAxis : FVector::ForwardVector).GetSafeNormal2D();
        const float Distance = FMath::IsFinite(Library->PlayerDistanceCm) ? FMath::Max(25.0f, Library->PlayerDistanceCm) : 150.0f;
        Stand = FTransform(FRotator(0, (-Forward).Rotation().Yaw, 0), Place.GetLocation() + Forward * Distance);
    }
    Runtime->PlaceAnchor(Stand);
    if (Performance != INDEX_NONE && Entry->bStartInPartnerView && !Runtime->SetPartnerView(true))
    { FailScene(TEXT("Performance partner viewpoint could not be activated.")); return; }
    // Music plays from the environment's speakers (left/right channels), stereo without them.
    AActor* SpeakerLeft = GratiaFindTagged(Level, TEXT("GratiaSpeakerL"));
    AActor* SpeakerRight = GratiaFindTagged(Level, TEXT("GratiaSpeakerR"));
    if (Music) Music->SetSpeakers(SpeakerLeft && SpeakerRight ? SpeakerLeft : nullptr, SpeakerLeft && SpeakerRight ? SpeakerRight : nullptr);
    // Ambient light of a streamed environment comes from its own (emissive) sky.
    for (const ULevelStreamingDynamic* Stream : GetStreams())
        if (const ULevel* StreamLevel = Stream->GetLoadedLevel())
            for (AActor* Actor : StreamLevel->Actors)
                if (Actor) Actor->ForEachComponent<USkyLightComponent>(false, [](USkyLightComponent* Sky) { Sky->RecaptureSky(); });
    bPlaylistOverride = false;
    if (Character->PerformanceStage) Character->PerformanceStage->SetMusicVolumeScale(Settings->MusicVolume);
    if (Performance != INDEX_NONE)
    {
        if (Music) Music->Stop(1.0f);
        if (PerformanceTrack && PerformanceTrack->Sound != Character->GetPerformance()->Scene.Music)
        {
            UE_LOG(LogGratiaScenes, Warning, TEXT("SCENE music analysis does not match the performance track; reactive music disabled"));
            PerformanceTrack = nullptr;
        }
    }
    else
    {
        PerformanceTrack = nullptr;
        PlayAmbient(Entry->Music, Entry->MusicVolume);
    }
    CollectReactiveLights();
    CollectMovers();
    if (UReverbEffect* Reverb = Entry->Reverb.LoadSynchronous()) UGameplayStatics::ActivateReverbEffect(this, Reverb, TEXT("GratiaScene"), 1.0f, 0.6f, 1.0f);
    else UGameplayStatics::DeactivateReverbEffect(this, TEXT("GratiaScene"));
    Space->HideSpace();
    Settings->LastScene = Entry->Id;
    SaveUserSettings();
    Fade(1.0f, 0.0f);
    State = EGratiaFlowState::Playing;
    UE_LOG(LogGratiaScenes, Display, TEXT("SCENE_READY %s environment=%s backdrops=%d performance=%d partner_view=%d lights=%d"), *Entry->Id.ToString(),
        Level ? *Level->GetOuter()->GetName() : TEXT("studio"), GetVisibleBackdrops(), Performance, Runtime->IsPartnerView() ? 1 : 0, ReactiveLights.Num());
}

void UGratiaSceneDirector::PlayAmbient(const TSoftObjectPtr<UGratiaMusicAnalysis>& Track, float Volume)
{
    if (!Music) return;
    UGratiaMusicAnalysis* Analysis = Track.LoadSynchronous();
    Music->SetMasterVolume(Settings ? Settings->MusicVolume : 1.0f);
    if (!Analysis || !Analysis->Sound) { Music->Stop(0.5f); return; }
    Music->Play(Analysis, FMath::Clamp(FMath::IsFinite(Volume) ? Volume : 0.5f, 0.0f, 2.0f), true);
}

void UGratiaSceneDirector::NextTrack()
{
    if (!Music || Music->Playlist.IsEmpty()) return;
    AGratiaPreviewCharacter* Character = GetCharacter();
    if (IsPerformanceScene() && !bPlaylistOverride)
    {
        // The performance's own music steps aside for the playlist.
        bPlaylistOverride = true;
        if (Character && Character->PerformanceStage) Character->PerformanceStage->SetMusicVolumeScale(0.0f);
        const int32 Last = Settings ? Music->Playlist.IndexOfByPredicate([this](const UGratiaMusicAnalysis* T) { return T->GetFName() == Settings->LastTrack; }) : INDEX_NONE;
        Music->Play(Music->Playlist[Last == INDEX_NONE ? 0 : Last], 1.0f, false);
    }
    else Music->Next();
    if (Settings && Music->GetCurrent()) Settings->LastTrack = Music->GetCurrent()->GetFName();
}

void UGratiaSceneDirector::PreviousTrack()
{
    if (!Music || Music->Playlist.IsEmpty()) return;
    if (IsPerformanceScene() && !bPlaylistOverride) { NextTrack(); return; }
    Music->Previous();
    if (Settings && Music->GetCurrent()) Settings->LastTrack = Music->GetCurrent()->GetFName();
}

FString UGratiaSceneDirector::GetTrackText() const
{
    const UGratiaMusicAnalysis* Track = Music && Music->IsPlaying() ? Music->GetCurrent() : nullptr;
    if (!Track && IsPerformanceScene() && !bPlaylistOverride && PerformanceTrack) Track = PerformanceTrack;
    if (!Track) return FString();
    const FString Title = Track->Title.IsEmpty() ? Track->GetName() : Track->Title.ToString();
    return Track->Artist.IsEmpty() ? Title : Title + TEXT("  \u00B7  ") + Track->Artist.ToString();
}

void UGratiaSceneDirector::CollectMovers()
{
    Movers.Reset();
    MoverTime = 0.0f;
    const ULevel* Level = Streamed ? Streamed->GetLoadedLevel() : nullptr;
    if (!Level) return;
    for (AActor* Actor : Level->Actors)
    {
        if (!Actor) continue;
        FMover Mover;
        for (const FName& Tag : Actor->Tags)
        {
            TArray<FString> Parts;
            Tag.ToString().ParseIntoArray(Parts, TEXT(":"));
            if (Parts.Num() >= 2 && Parts[0] == TEXT("GratiaSpin")) Mover.Spin = FCString::Atof(*Parts[1]);
            if (Parts.Num() >= 2 && Parts[0] == TEXT("GratiaSweep"))
            {
                // GratiaSweep:<degrees>:<period s>[:<pivot on local Z, cm>]
                Mover.Sweep = FCString::Atof(*Parts[1]);
                if (Parts.Num() >= 3) Mover.SweepPeriod = FMath::Max(0.2f, FCString::Atof(*Parts[2]));
                if (Parts.Num() >= 4) Mover.PivotOffset = FCString::Atof(*Parts[3]);
            }
            if (Parts.Num() >= 2 && Parts[0] == TEXT("GratiaPulse")) Mover.Pulse = FCString::Atof(*Parts[1]);
        }
        if (Mover.Spin == 0.0f && Mover.Sweep == 0.0f && Mover.Pulse == 0.0f) continue;
        Mover.Actor = Actor;
        Mover.BaseRotation = Actor->GetActorRotation();
        Mover.BaseLocation = Actor->GetActorLocation();
        Mover.BaseScale = Actor->GetActorScale3D();
        Mover.Phase = float(Movers.Num()) * 0.9f;
        Movers.Add(Mover);
    }
}

void UGratiaSceneDirector::UpdateMovers(float Delta)
{
    if (Movers.IsEmpty() || State != EGratiaFlowState::Playing) return;
    // Spins speed up with the bass; sweeps swing wider on the beat.
    MoverTime += Delta * (0.6f + 0.8f * MusicFrame.X);
    for (const FMover& Mover : Movers)
    {
        AActor* Actor = Mover.Actor.Get();
        if (!Actor) continue;
        FRotator Rotation = Mover.BaseRotation;
        Rotation.Yaw += Mover.Spin * MoverTime;
        Rotation.Pitch += Mover.Sweep * (0.75f + 0.25f * MusicFrame.W) * FMath::Sin(MoverTime * 2.0f * PI / Mover.SweepPeriod + Mover.Phase);
        if (Mover.PivotOffset != 0.0f)
        {
            // Turn about the pivot (a laser swings from its emitter, not its middle).
            const FVector Pivot = Mover.BaseLocation + Mover.BaseRotation.RotateVector(FVector(0, 0, Mover.PivotOffset));
            Actor->SetActorLocationAndRotation(Pivot + Rotation.RotateVector(FVector(0, 0, -Mover.PivotOffset)), Rotation);
        }
        else Actor->SetActorRotation(Rotation);
        if (Mover.Pulse != 0.0f) Actor->SetActorScale3D(Mover.BaseScale * (1.0f + Mover.Pulse * MusicFrame.W));
    }
}

void UGratiaSceneDirector::CollectReactiveLights()
{
    RestoreReactiveLights();
    const ULevel* Level = Streamed ? Streamed->GetLoadedLevel() : nullptr;
    auto Add = [this](const AActor* Actor)
    {
        if (!Actor || !Actor->ActorHasTag(GratiaAudioLightTag)) return;
        // Band from the tags: GratiaAudioMid / GratiaAudioHigh, otherwise bass.
        const int32 Band = Actor->ActorHasTag(TEXT("GratiaAudioHigh")) ? 2 : Actor->ActorHasTag(TEXT("GratiaAudioMid")) ? 1 : 0;
        Actor->ForEachComponent<ULightComponent>(false, [this, Band](ULightComponent* Light)
            { ReactiveLights.Add({Light, Light->Intensity, Band}); });
    };
    if (Level) for (const AActor* Actor : Level->Actors) Add(Actor);
    else for (const TWeakObjectPtr<AActor>& Actor : StudioActors) Add(Actor.Get());
}

void UGratiaSceneDirector::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    if (!FMath::IsFinite(Delta) || Delta < 0.0f) return;
    AGratiaStage1Runtime* Runtime = GetRuntime();
    AGratiaPreviewCharacter* Character = GetCharacter();
    if (bPendingStart && Runtime && Runtime->bPawnReady && Runtime->GetPlayerCamera() && Character)
    {
        bPendingStart = false;
        if (Character->PerformanceStage) AddTickPrerequisiteComponent(Character->PerformanceStage);
        CharacterHome = Character->GetActorTransform();
        AnchorHome = Runtime->GetActorTransform();
        bHomeSaved = true;
        ApplyUserSettings();
        EnterLobby();
        if (!PinnedScene.IsNone())
        {
            const int32 Index = Library ? Library->FindScene(PinnedScene) : INDEX_NONE;
            UE_LOG(LogGratiaScenes, Display, TEXT("SCENE_PINNED %s index=%d"), *PinnedScene.ToString(), Index);
            if (Index == INDEX_NONE || !StartScene(Index))
                UE_LOG(LogGratiaScenes, Error, TEXT("SCENE_PINNED_FAILED %s: %s"), *PinnedScene.ToString(), Index == INDEX_NONE ? TEXT("no such scene") : *LastError);
        }
    }
    if (State == EGratiaFlowState::Off) return;
    StateSeconds += Delta;
    const float FadeTime = GetFadeSeconds() + 0.05f;
    switch (State)
    {
    case EGratiaFlowState::ToLoading:
        if (StateSeconds < FadeTime) break;
        {
            if (GetStreams().ContainsByPredicate([](const ULevelStreamingDynamic* Stream) { return Stream->IsLevelVisible(); }))
            {
                SetStreamsVisible(false);
                if (StateSeconds > FadeTime + GetLoadingTimeout()) FailScene(TEXT("Previous environment could not be hidden."));
                break;
            }
            FString Reason;
            if (!CanStartScene(Pending, Reason)) { FailScene(Reason); break; }
            Current = Pending;
            const FGratiaSceneEntry& Entry = Library->Scenes[Current];
            UnloadEnvironment();
            ParkCharacter();
            SetStudioVisible(false);
            ShowLoading(Entry.Title, Entry.Description, Entry.Accent, Entry.Thumbnail.LoadSynchronous());
            Movers.Reset();
            if (Music) Music->SetSpeakers(nullptr, nullptr);
            PlayAmbient(Library->LobbyMusic, Library->LobbyMusicVolume);
            PerformanceTrack = Entry.PerformanceMusic.LoadSynchronous();
            bVisibilityRequested = false;
            if (!Entry.Environment.IsNull())
            {
                bool bOk = false;
                Streamed = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(this, Entry.Environment, FVector::ZeroVector, FRotator::ZeroRotator, bOk);
                if (!bOk || !Streamed) { Streamed = nullptr; FailScene(FString::Printf(TEXT("Environment failed to stream: %s"), *Entry.Environment.ToString())); break; }
                Streamed->SetShouldBeVisible(false);
            }
            FString BackdropError;
            for (const TSoftObjectPtr<UWorld>& Backdrop : Entry.Backdrops)
            {
                bool bOk = false;
                ULevelStreamingDynamic* Stream = Backdrop.IsNull() ? nullptr
                    : ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(this, Backdrop, FVector::ZeroVector, FRotator::ZeroRotator, bOk);
                if (!bOk || !Stream) { BackdropError = FString::Printf(TEXT("Backdrop failed to stream: %s"), *Backdrop.ToString()); break; }
                Stream->SetShouldBeVisible(false);
                Backdrops.Add(Stream);
            }
            if (!BackdropError.IsEmpty()) { FailScene(BackdropError); break; }
            Fade(1.0f, 0.0f);
            State = EGratiaFlowState::Loading;
        }
        break;
    case EGratiaFlowState::Loading:
    {
        const bool bReady = IsEnvironmentReady();
        const bool bFailed = GetStreams().ContainsByPredicate([](const ULevelStreamingDynamic* Stream)
            { return Stream->GetLevelStreamingState() == ELevelStreamingState::FailedToLoad; });
        if (bFailed || StateSeconds > GetLoadingTimeout())
        { FailScene(TEXT("Environment loading failed or exceeded its time limit.")); break; }
        const float MinLoading = FMath::IsFinite(Library->MinLoadingSeconds) ? FMath::Clamp(Library->MinLoadingSeconds, 0.0f, 10.0f) : 2.0f;
        const float Waited = FMath::Clamp(StateSeconds / FMath::Max(0.1f, MinLoading), 0.0f, 1.0f);
        Space->SetProgress((bReady ? 1.0f : 0.7f) * Waited);
        if (bReady && StateSeconds >= FMath::Max(MinLoading, FadeTime)) { Fade(0.0f, 1.0f); State = EGratiaFlowState::ToScene; }
        break;
    }
    case EGratiaFlowState::ToScene:
        if (StateSeconds < FadeTime) break;
        if (!bVisibilityRequested)
        {
            bVisibilityRequested = true;
            SetStreamsVisible(true);
        }
        if (AreStreamsVisible()) FinishScene();
        else if (StateSeconds > FadeTime + GetLoadingTimeout()) FailScene(TEXT("Environment could not become visible."));
        break;
    case EGratiaFlowState::ToLobby:
        if (StateSeconds < FadeTime) break;
        if (GetStreams().ContainsByPredicate([](const ULevelStreamingDynamic* Stream) { return Stream->IsLevelVisible(); }))
        {
            SetStreamsVisible(false);
            if (StateSeconds < FadeTime + GetLoadingTimeout()) break;
            LastError = TEXT("Environment unload exceeded its time limit.");
            for (const ULevelStreamingDynamic* Stream : GetStreams())
                if (const ULevel* Level = Stream->GetLoadedLevel())
                    for (AActor* Actor : Level->Actors)
                        if (Actor) { Actor->SetActorHiddenInGame(true); Actor->SetActorEnableCollision(false);
                            Actor->ForEachComponent<USceneComponent>(false, [](USceneComponent* Component) { Component->SetVisibility(false); }); }
            UE_LOG(LogGratiaScenes, Warning, TEXT("SCENE_FAILED %s"), *LastError);
        }
        FinishLobby();
        break;
    default:
        break;
    }
    UpdateMusic(Delta);
}

void UGratiaSceneDirector::UpdateMusic(float Delta)
{
    const AGratiaPreviewCharacter* Character = GetCharacter();
    const UGratiaPerformanceStage* Stage = Character ? Character->PerformanceStage.Get() : nullptr;
    FVector4f Frame(0, 0, 0, 0);
    if (Music) Music->SetEnabled(IsSoundEnabled());
    if (!bPlaylistOverride && PerformanceTrack && Stage && Stage->IsMusicPlaying() && IsSoundEnabled()) Frame = PerformanceTrack->Sample(Stage->GetMusicTime());
    else if (Music && IsSoundEnabled()) Frame = Music->GetFrame();
    // Fast attack, slower release: lights jump with a hit and glow out.
    const float Attack = 1.0f - FMath::Exp(-Delta * 25.0f), Release = 1.0f - FMath::Exp(-Delta * 5.0f);
    for (int32 I = 0; I < 4; ++I) Smoothed[I] = FMath::Lerp(Smoothed[I], Frame[I], Frame[I] > Smoothed[I] ? Attack : Release);
    MusicFrame = Smoothed;
    if (Space && Space->IsShown()) Space->SetMusic(MusicFrame);
    if (Library->AudioCollection)
        if (UMaterialParameterCollectionInstance* Collection = GetWorld()->GetParameterCollectionInstance(Library->AudioCollection))
        {
            Collection->SetScalarParameterValue(TEXT("Bass"), MusicFrame.X);
            Collection->SetScalarParameterValue(TEXT("Mid"), MusicFrame.Y);
            Collection->SetScalarParameterValue(TEXT("High"), MusicFrame.Z);
            Collection->SetScalarParameterValue(TEXT("Beat"), MusicFrame.W);
            Collection->SetScalarParameterValue(TEXT("Energy"), (MusicFrame.X + MusicFrame.Y + MusicFrame.Z) / 3.0f);
        }
    for (const FReactiveLight& Light : ReactiveLights)
        if (ULightComponent* Component = Light.Light.Get())
            Component->SetIntensity(Light.BaseIntensity * (0.35f + 1.3f * MusicFrame[Light.Band] + 0.5f * MusicFrame.W));
    UpdateMovers(Delta);
}

void UGratiaSceneDirector::TogglePause()
{
    AGratiaPreviewCharacter* Character = GetCharacter();
    if (IsPerformanceScene()) Character->SetPerformancePlayback(!Character->IsPerformancePaused(), Character->GetPerformanceRate());
}

void UGratiaSceneDirector::Restart()
{
    AGratiaPreviewCharacter* Character = GetCharacter();
    if (IsPerformanceScene()) Character->SeekPerformancePart(0);
}

void UGratiaSceneDirector::StepPart(int32 Direction)
{
    AGratiaPreviewCharacter* Character = GetCharacter();
    if (IsPerformanceScene()) Character->SeekPerformancePart(Character->PerformancePart + Direction);
}

void UGratiaSceneDirector::StepSpeed(int32 Direction)
{
    AGratiaPreviewCharacter* Character = GetCharacter();
    if (!IsPerformanceScene()) return;
    int32 Nearest = 2;
    for (int32 I = 0; I < 5; ++I)
        if (FMath::Abs(SpeedSteps[I] - Character->GetPerformanceRate()) < FMath::Abs(SpeedSteps[Nearest] - Character->GetPerformanceRate())) Nearest = I;
    Character->SetPerformancePlayback(Character->IsPerformancePaused(), SpeedSteps[FMath::Clamp(Nearest + Direction, 0, 4)]);
}

FString UGratiaSceneDirector::GetPlaybackText() const
{
    const AGratiaPreviewCharacter* Character = GetCharacter();
    const FGratiaPerformanceClip* Performance = IsPerformanceScene() ? Character->GetPerformance() : nullptr;
    if (!Performance) return TEXT("Free play");
    float Total = 0.0f;
    for (int32 Part = 0; Part < Performance->NumParts(); ++Part)
        if (const UAnimSequence* Clip = Performance->GetPart(Part)) Total += Clip->GetPlayLength();
    const float Time = Character->PerformanceStage ? Character->PerformanceStage->GetPerformanceTime() : 0.0f;
    return FString::Printf(TEXT("Part %d/%d   %s / %s   %.2gx%s"), Character->PerformancePart + 1, Performance->NumParts(),
        *GratiaClock(Time), *GratiaClock(Total), Character->GetPerformanceRate(), Character->IsPerformancePaused() ? TEXT("   paused") : TEXT(""));
}

FString UGratiaSceneDirector::GetDiagnostics() const
{
    static const TCHAR* Names[] = {TEXT("off"), TEXT("lobby"), TEXT("to_loading"), TEXT("loading"), TEXT("to_scene"), TEXT("playing"), TEXT("to_lobby")};
    const FGratiaSceneEntry* Entry = GetCurrentEntry();
    return FString::Printf(TEXT("state=%s scene=%s environment=%s input_blocked=%d error=%s music=%.2f/%.2f/%.2f/%.2f lights=%d"), Names[uint8(State)],
        Entry ? *Entry->Id.ToString() : TEXT("-"), Streamed ? (IsEnvironmentReady() ? TEXT("ready") : TEXT("loading")) : TEXT("studio"),
        IsSceneInputBlocked() ? 1 : 0, LastError.IsEmpty() ? TEXT("-") : *LastError,
        MusicFrame.X, MusicFrame.Y, MusicFrame.Z, MusicFrame.W, ReactiveLights.Num());
}
