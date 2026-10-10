#include "GratiaAmbience.h"
#include "GratiaBodyMotion.h"
#include "GratiaCharacterProfile.h"
#include "GratiaCustomization.h"
#include "GratiaInteraction.h"
#include "GratiaLoadingSpace.h"
#include "GratiaMirror.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaSceneControls.h"
#include "GratiaSceneDirector.h"
#include "GratiaStage1Runtime.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaAmbience, Log, All);

namespace
{
    const TCHAR* GratiaLightingPresetsPath = TEXT("/Game/Gratia/Environment/DA_GratiaLightingPresets.DA_GratiaLightingPresets");
    const FName GratiaAudioLightTagName(TEXT("GratiaAudioLight"));

    FGratiaLightingPreset MakePreset(const TCHAR* Id, const TCHAR* Label)
    {
        FGratiaLightingPreset Preset;
        Preset.Id = Id;
        Preset.Label = FText::FromString(Label);
        return Preset;
    }

    FGratiaMoodLight MakeLight(const FVector& Offset, const FLinearColor& Color, float Candela, float Radius, float Source)
    {
        FGratiaMoodLight Light;
        Light.OffsetCm = Offset; Light.Color = Color; Light.Intensity = Candela; Light.RadiusCm = Radius; Light.SourceRadiusCm = Source;
        return Light;
    }

    FLinearColor Lerp(const FLinearColor& A, const FLinearColor& B, float T) { return A + (B - A) * T; }
}

UGratiaLightingPresets::UGratiaLightingPresets()
{
    Presets.Add(MakePreset(TEXT("Authored"), TEXT("Как в сцене")));

    FGratiaLightingPreset Night = MakePreset(TEXT("NightLamp"), TEXT("Ночник"));
    Night.LightScale = 0.12f; Night.LightTint = FLinearColor(1.0f, 0.8f, 0.6f);
    Night.SunScale = 0.04f; Night.SkyScale = 0.12f; Night.SkyTint = FLinearColor(0.55f, 0.6f, 1.0f);
    Night.FogTint = FLinearColor(0.25f, 0.28f, 0.45f);
    Night.MoodLights.Add(MakeLight(FVector(55.0f, 85.0f, 65.0f), FLinearColor(1.0f, 0.6f, 0.28f), 9.0f, 420.0f, 8.0f));
    Night.MoodLights.Add(MakeLight(FVector(-80.0f, -60.0f, 190.0f), FLinearColor(0.35f, 0.45f, 1.0f), 1.5f, 500.0f, 30.0f));
    Night.bGrade = true; Night.WhiteTemperature = 5600.0f; Night.ExposureBias = 0.6f; Night.Saturation = 0.95f; Night.Vignette = 0.55f;
    Presets.Add(Night);

    FGratiaLightingPreset Sunset = MakePreset(TEXT("Sunset"), TEXT("Закат"));
    Sunset.LightScale = 0.55f; Sunset.LightTint = FLinearColor(1.0f, 0.72f, 0.5f);
    Sunset.SunScale = 0.9f; Sunset.SunTint = FLinearColor(1.0f, 0.48f, 0.22f); Sunset.SunElevationDegrees = 7.0f;
    Sunset.SkyScale = 0.55f; Sunset.SkyTint = FLinearColor(1.0f, 0.68f, 0.55f); Sunset.FogTint = FLinearColor(1.0f, 0.55f, 0.35f);
    Sunset.MoodLights.Add(MakeLight(FVector(40.0f, -160.0f, 140.0f), FLinearColor(1.0f, 0.45f, 0.18f), 28.0f, 900.0f, 40.0f));
    Sunset.bGrade = true; Sunset.WhiteTemperature = 5200.0f; Sunset.Saturation = 1.12f; Sunset.Vignette = 0.45f;
    Presets.Add(Sunset);

    FGratiaLightingPreset Neon = MakePreset(TEXT("Neon"), TEXT("Неон"));
    Neon.LightScale = 0.15f; Neon.LightTint = FLinearColor(0.7f, 0.6f, 1.0f);
    Neon.SunScale = 0.04f; Neon.SkyScale = 0.18f; Neon.SkyTint = FLinearColor(0.6f, 0.45f, 1.0f); Neon.FogTint = FLinearColor(0.6f, 0.2f, 0.8f);
    Neon.MoodLights.Add(MakeLight(FVector(35.0f, -110.0f, 165.0f), FLinearColor(1.0f, 0.1f, 0.65f), 22.0f, 600.0f, 25.0f));
    Neon.MoodLights.Add(MakeLight(FVector(35.0f, 110.0f, 165.0f), FLinearColor(0.1f, 0.75f, 1.0f), 22.0f, 600.0f, 25.0f));
    Neon.bGrade = true; Neon.WhiteTemperature = 7600.0f; Neon.ExposureBias = 0.3f; Neon.Saturation = 1.2f; Neon.Vignette = 0.5f;
    Presets.Add(Neon);

    FGratiaLightingPreset Dim = MakePreset(TEXT("Dim"), TEXT("Приглушённый"));
    Dim.LightScale = 0.35f; Dim.LightTint = FLinearColor(1.0f, 0.9f, 0.82f);
    Dim.SunScale = 0.35f; Dim.SkyScale = 0.4f; Dim.FogTint = FLinearColor(0.8f, 0.75f, 0.7f);
    Dim.MoodLights.Add(MakeLight(FVector(90.0f, 40.0f, 110.0f), FLinearColor(1.0f, 0.82f, 0.65f), 5.0f, 500.0f, 30.0f));
    Dim.bGrade = true; Dim.WhiteTemperature = 5900.0f; Dim.ExposureBias = 0.2f; Dim.Saturation = 0.92f; Dim.Vignette = 0.6f;
    Presets.Add(Dim);
}

UGratiaAmbience* UGratiaAmbience::Get(const UObject* WorldContext)
{
    const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    return World ? World->GetSubsystem<UGratiaAmbience>() : nullptr;
}

bool UGratiaAmbience::IsQARun()
{
    const TCHAR* Command = FCommandLine::Get();
    for (const TCHAR* QA : {TEXT("GratiaSelfTest"), TEXT("GratiaSmokeTest"), TEXT("GratiaSoftBodyQA"), TEXT("GratiaFlowQA"),
             TEXT("GratiaChannelShots"), TEXT("GratiaMenuShots")})
        if (FParse::Param(Command, QA)) return true;
    return false;
}

TStatId UGratiaAmbience::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UGratiaAmbience, STATGROUP_Tickables);
}

bool UGratiaAmbience::IsTickable() const
{
    return bBegun;
}

void UGratiaAmbience::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    if (!InWorld.IsGameWorld()) return;
    bBegun = true;
    bQA = IsQARun();
    DefaultPresets = NewObject<UGratiaLightingPresets>(this);
    LoadedPresets = LoadObject<UGratiaLightingPresets>(nullptr, GratiaLightingPresetsPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    EnsureCharacters();
    SpawnHandle = InWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UGratiaAmbience::OnActorSpawned));
    if (GetRuntime() && !bQA)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Controls = InWorld.SpawnActor<AGratiaSceneControls>(Params);
    }
    UE_LOG(LogGratiaAmbience, Display, TEXT("Ambience: %d lighting presets (%s), panel %s"), GetLightingPresetCount(),
        LoadedPresets ? TEXT("asset") : TEXT("built-in"), Controls ? TEXT("on") : TEXT("off"));
}

void UGratiaAmbience::Deinitialize()
{
    if (UWorld* World = GetWorld()) World->RemoveOnActorSpawnedHandler(SpawnHandle);
    Super::Deinitialize();
}

void UGratiaAmbience::OnActorSpawned(AActor* Actor)
{
    if (AGratiaPreviewCharacter* Character = Cast<AGratiaPreviewCharacter>(Actor))
    {
        UGratiaBodyMotion::Ensure(Character);
        UGratiaCustomization::Ensure(Character);
    }
}

void UGratiaAmbience::EnsureCharacters()
{
    for (TActorIterator<AGratiaPreviewCharacter> It(GetWorld()); It; ++It)
    {
        UGratiaBodyMotion::Ensure(*It);
        UGratiaCustomization::Ensure(*It);
    }
}

AGratiaStage1Runtime* UGratiaAmbience::GetRuntime() const
{
    if (!CachedRuntime.IsValid() && GetWorld())
        for (TActorIterator<AGratiaStage1Runtime> It(GetWorld()); It; ++It) { CachedRuntime = *It; break; }
    return CachedRuntime.Get();
}

AGratiaPreviewCharacter* UGratiaAmbience::GetCharacter() const
{
    if (const AGratiaStage1Runtime* Runtime = GetRuntime())
        if (Runtime->TargetCharacter.IsValid()) return Runtime->TargetCharacter.Get();
    for (TActorIterator<AGratiaPreviewCharacter> It(GetWorld()); It; ++It) return *It;
    return nullptr;
}

const UGratiaLightingPresets* UGratiaAmbience::GetPresets() const
{
    return LoadedPresets && !LoadedPresets->Presets.IsEmpty() ? LoadedPresets.Get() : DefaultPresets.Get();
}

const FGratiaLightingPreset* UGratiaAmbience::GetPreset(int32 Index) const
{
    const UGratiaLightingPresets* Presets = GetPresets();
    return Presets && Presets->Presets.IsValidIndex(Index) ? &Presets->Presets[Index] : nullptr;
}

int32 UGratiaAmbience::GetLightingPresetCount() const
{
    const UGratiaLightingPresets* Presets = GetPresets();
    return Presets ? Presets->Presets.Num() : 0;
}

FText UGratiaAmbience::GetLightingLabel() const
{
    const FGratiaLightingPreset* Current = GetPreset(Preset);
    return Current ? Current->Label : FText::GetEmpty();
}

void UGratiaAmbience::SetLightingPreset(int32 Index)
{
    if (!GetPreset(Index)) return;
    Preset = Index;
    CollectLights(false);
    Retarget(true);
    Blend = 0.0f;
    UE_LOG(LogGratiaAmbience, Display, TEXT("Lighting: %s (%d room lights, %d static skipped, %d sky, %d fog)"),
        *GetLightingLabel().ToString(), RoomLights.Num(), StaticLightsSkipped, Skies.Num(), Fogs.Num());
}

void UGratiaAmbience::CollectLights(bool bApplyNow)
{
    UWorld* World = GetWorld();
    if (!World) return;
    RoomLights.RemoveAll([](const FRoomLight& Light) { return !Light.Light.IsValid(); });
    Skies.RemoveAll([](const FSky& Sky) { return !Sky.Light.IsValid(); });
    Fogs.RemoveAll([](const FFog& Fog) { return !Fog.Fog.IsValid(); });
    const AGratiaStage1Runtime* Runtime = GetRuntime();
    const APawn* Pawn = Runtime ? Runtime->GetPlayerPawn() : nullptr;
    StaticLightsSkipped = 0;
    const int32 FirstNew = RoomLights.Num(), FirstSky = Skies.Num(), FirstFog = Fogs.Num();
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        // The character, the player rig, the loading space, our own lights and music-driven lights stay as they are.
        if (!Actor || Actor == MoodActor || Actor == Runtime || Actor == Pawn || Actor->IsA<AGratiaPreviewCharacter>()
            || Actor->IsA<AGratiaLoadingSpace>() || Actor->IsA<AGratiaMirror>() || Actor->IsA<AGratiaSceneControls>()
            || Actor->ActorHasTag(GratiaAudioLightTagName)) continue;
        TInlineComponentArray<UActorComponent*> Components(Actor);
        for (UActorComponent* Component : Components)
        {
            if (ULightComponent* Light = Cast<ULightComponent>(Component))
            {
                if (RoomLights.ContainsByPredicate([Light](const FRoomLight& Known) { return Known.Light.Get() == Light; })) continue;
                if (Light->Mobility == EComponentMobility::Static) { ++StaticLightsSkipped; continue; }
                FRoomLight& Entry = RoomLights.AddDefaulted_GetRef();
                Entry.Light = Light;
                Entry.BaseIntensity = Light->Intensity;
                Entry.BaseColor = Light->GetLightColor();
                Entry.BaseRotation = Light->GetComponentRotation();
                Entry.bSun = Light->IsA<UDirectionalLightComponent>();
                Entry.bCanRotate = Entry.bSun && Light->Mobility == EComponentMobility::Movable;
                Entry.FromIntensity = Entry.ToIntensity = Entry.BaseIntensity;
                Entry.FromColor = Entry.ToColor = Entry.BaseColor;
                Entry.FromRotation = Entry.ToRotation = Entry.BaseRotation;
            }
            else if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
            {
                if (Skies.ContainsByPredicate([Sky](const FSky& Known) { return Known.Light.Get() == Sky; })) continue;
                if (Sky->Mobility == EComponentMobility::Static) { ++StaticLightsSkipped; continue; }
                FSky& Entry = Skies.AddDefaulted_GetRef();
                Entry.Light = Sky;
                Entry.BaseIntensity = Entry.FromIntensity = Entry.ToIntensity = Sky->Intensity;
                Entry.BaseColor = Entry.FromColor = Entry.ToColor = Sky->GetLightColor();
            }
            else if (UExponentialHeightFogComponent* Fog = Cast<UExponentialHeightFogComponent>(Component))
            {
                if (Fogs.ContainsByPredicate([Fog](const FFog& Known) { return Known.Fog.Get() == Fog; })) continue;
                FFog& Entry = Fogs.AddDefaulted_GetRef();
                Entry.Fog = Fog;
                Entry.BaseColor = Entry.FromColor = Entry.ToColor = Fog->FogInscatteringLuminance;
            }
        }
    }
    if (!bApplyNow || Preset == 0) return;
    // Lights of a newly streamed environment take the current mood at once.
    const FGratiaLightingPreset* Current = GetPreset(Preset);
    if (!Current) return;
    for (int32 I = FirstNew; I < RoomLights.Num(); ++I)
    {
        FRoomLight& L = RoomLights[I];
        L.ToIntensity = L.FromIntensity = L.BaseIntensity * (L.bSun ? Current->SunScale : Current->LightScale);
        L.ToColor = L.FromColor = L.BaseColor * (L.bSun ? Current->SunTint : Current->LightTint);
        L.Light->SetIntensity(L.ToIntensity);
        L.Light->SetLightColor(L.ToColor);
    }
    for (int32 I = FirstSky; I < Skies.Num(); ++I)
    {
        Skies[I].ToIntensity = Skies[I].FromIntensity = Skies[I].BaseIntensity * Current->SkyScale;
        Skies[I].ToColor = Skies[I].FromColor = Skies[I].BaseColor * Current->SkyTint;
        Skies[I].Light->SetIntensity(Skies[I].ToIntensity);
        Skies[I].Light->SetLightColor(Skies[I].ToColor);
    }
    for (int32 I = FirstFog; I < Fogs.Num(); ++I)
    {
        Fogs[I].ToColor = Fogs[I].FromColor = Fogs[I].BaseColor * Current->FogTint;
        Fogs[I].Fog->SetFogInscatteringColor(Fogs[I].ToColor);
    }
}

void UGratiaAmbience::Retarget(bool bSnapshot)
{
    const FGratiaLightingPreset* Target = GetPreset(Preset);
    if (!Target) return;
    for (FRoomLight& L : RoomLights)
    {
        if (!L.Light.IsValid()) continue;
        if (bSnapshot) { L.FromIntensity = L.Light->Intensity; L.FromColor = L.Light->GetLightColor(); L.FromRotation = L.Light->GetComponentRotation(); }
        L.ToIntensity = L.BaseIntensity * (L.bSun ? Target->SunScale : Target->LightScale);
        L.ToColor = L.BaseColor * (L.bSun ? Target->SunTint : Target->LightTint);
        L.ToRotation = L.BaseRotation;
        if (L.bCanRotate && Target->SunElevationDegrees > 0.0f) L.ToRotation.Pitch = -Target->SunElevationDegrees;
    }
    for (FSky& Sky : Skies)
    {
        if (!Sky.Light.IsValid()) continue;
        if (bSnapshot) { Sky.FromIntensity = Sky.Light->Intensity; Sky.FromColor = Sky.Light->GetLightColor(); }
        Sky.ToIntensity = Sky.BaseIntensity * Target->SkyScale;
        Sky.ToColor = Sky.BaseColor * Target->SkyTint;
    }
    for (FFog& Fog : Fogs)
    {
        if (!Fog.Fog.IsValid()) continue;
        if (bSnapshot) Fog.FromColor = Fog.Fog->FogInscatteringLuminance;
        Fog.ToColor = Fog.BaseColor * Target->FogTint;
    }
    // Mood lights: one point light per entry of every preset, created on first use; only the current preset's are lit.
    UWorld* World = GetWorld();
    if (!MoodActor && World)
    {
        FActorSpawnParameters Params;
        Params.Name = MakeUniqueObjectName(World->GetCurrentLevel(), AActor::StaticClass(), TEXT("GratiaMoodLights"));
        MoodActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
        if (MoodActor)
        {
            USceneComponent* Root = NewObject<USceneComponent>(MoodActor, TEXT("Root"));
            Root->SetMobility(EComponentMobility::Movable);
            MoodActor->SetRootComponent(Root);
            Root->RegisterComponent();
        }
    }
    for (int32 L = 0; MoodActor && L < Target->MoodLights.Num(); ++L)
    {
        if (Moods.ContainsByPredicate([this, L](const FMood& Mood) { return Mood.PresetIndex == Preset && Mood.LightIndex == L && Mood.Light.IsValid(); })) continue;
        const FGratiaMoodLight& Definition = Target->MoodLights[L];
        UPointLightComponent* Light = NewObject<UPointLightComponent>(MoodActor);
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetupAttachment(MoodActor->GetRootComponent());
        Light->SetCastShadows(false);
        Light->bUseInverseSquaredFalloff = true;
        Light->IntensityUnits = ELightUnits::Candelas;
        Light->SetIntensity(0.0f);
        Light->SetLightColor(Definition.Color);
        Light->SetAttenuationRadius(Definition.RadiusCm);
        Light->SetSourceRadius(Definition.SourceRadiusCm);
        Light->RegisterComponent();
        FMood& Mood = Moods.AddDefaulted_GetRef();
        Mood.Light = Light; Mood.PresetIndex = Preset; Mood.LightIndex = L;
    }
    for (FMood& Mood : Moods)
    {
        if (!Mood.Light.IsValid()) continue;
        if (bSnapshot) Mood.FromIntensity = Mood.Light->Intensity;
        const FGratiaLightingPreset* Owner = GetPreset(Mood.PresetIndex);
        Mood.ToIntensity = Mood.PresetIndex == Preset && Owner && Owner->MoodLights.IsValidIndex(Mood.LightIndex) ? Owner->MoodLights[Mood.LightIndex].Intensity : 0.0f;
    }
    // Grade.
    if (bSnapshot)
    {
        FromGrade = ToGrade;
        if (GradeVolume) FromGrade.Weight = GradeVolume->BlendWeight;
    }
    ToGrade.Weight = Target->bGrade ? 1.0f : 0.0f;
    if (Target->bGrade)
    {
        ToGrade.WhiteTemperature = Target->WhiteTemperature; ToGrade.ExposureBias = Target->ExposureBias;
        ToGrade.Saturation = Target->Saturation; ToGrade.Vignette = Target->Vignette;
        if (FromGrade.Weight <= 0.0f) { const float W = FromGrade.Weight; FromGrade = ToGrade; FromGrade.Weight = W; }
    }
    if (Target->bGrade && !GradeVolume && World)
    {
        GradeVolume = World->SpawnActor<APostProcessVolume>();
        if (GradeVolume)
        {
            GradeVolume->bUnbound = true;
            GradeVolume->Priority = 50.0f;
            GradeVolume->BlendWeight = 0.0f;
        }
    }
}

void UGratiaAmbience::ApplyLights(float Alpha)
{
    const float T = FMath::SmoothStep(0.0f, 1.0f, Alpha);
    for (const FRoomLight& L : RoomLights)
    {
        if (!L.Light.IsValid()) continue;
        L.Light->SetIntensity(FMath::Lerp(L.FromIntensity, L.ToIntensity, T));
        L.Light->SetLightColor(Lerp(L.FromColor, L.ToColor, T));
        if (L.bCanRotate) L.Light->SetWorldRotation(FMath::Lerp(L.FromRotation, L.ToRotation, T));
    }
    for (const FSky& Sky : Skies)
    {
        if (!Sky.Light.IsValid()) continue;
        Sky.Light->SetIntensity(FMath::Lerp(Sky.FromIntensity, Sky.ToIntensity, T));
        Sky.Light->SetLightColor(Lerp(Sky.FromColor, Sky.ToColor, T));
    }
    for (const FFog& Fog : Fogs)
        if (Fog.Fog.IsValid()) Fog.Fog->SetFogInscatteringColor(Lerp(Fog.FromColor, Fog.ToColor, T));
    for (const FMood& Mood : Moods)
        if (Mood.Light.IsValid()) Mood.Light->SetIntensity(FMath::Lerp(Mood.FromIntensity, Mood.ToIntensity, T));
    if (GradeVolume)
    {
        FPostProcessSettings& Settings = GradeVolume->Settings;
        Settings.bOverride_WhiteTemp = true;
        Settings.WhiteTemp = FMath::Lerp(FromGrade.WhiteTemperature, ToGrade.WhiteTemperature, T);
        Settings.bOverride_AutoExposureBias = true;
        Settings.AutoExposureBias = FMath::Lerp(FromGrade.ExposureBias, ToGrade.ExposureBias, T);
        Settings.bOverride_ColorSaturation = true;
        const float Saturation = FMath::Lerp(FromGrade.Saturation, ToGrade.Saturation, T);
        Settings.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.0f);
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = FMath::Lerp(FromGrade.Vignette, ToGrade.Vignette, T);
        GradeVolume->BlendWeight = FMath::Lerp(FromGrade.Weight, ToGrade.Weight, T);
    }
}

void UGratiaAmbience::UpdateMoodLightPlacement()
{
    const AGratiaPreviewCharacter* Character = GetCharacter();
    if (!MoodActor || !Character) return;
    // Character frame: forward from the profile, up is world up, origin at the feet.
    FVector Forward = Character->GetActorRightVector();
    if (const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get())
        Forward = Character->GetActorTransform().TransformVectorNoScale(Profile->ForwardAxis.GetSafeNormal());
    Forward = FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
    if (Forward.IsNearlyZero()) Forward = FVector::ForwardVector;
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    const FVector Origin = Character->GetActorLocation();
    for (const FMood& Mood : Moods)
    {
        const FGratiaLightingPreset* Owner = GetPreset(Mood.PresetIndex);
        if (!Mood.Light.IsValid() || !Owner || !Owner->MoodLights.IsValidIndex(Mood.LightIndex)) continue;
        const FVector Offset = Owner->MoodLights[Mood.LightIndex].OffsetCm;
        Mood.Light->SetWorldLocation(Origin + Forward * Offset.X + Right * Offset.Y + FVector::UpVector * Offset.Z);
    }
}

FText UGratiaAmbience::GetMirrorLabel() const
{
    return FText::FromString(MirrorMode == 1 ? TEXT("В рост") : MirrorMode == 2 ? TEXT("На стене") : TEXT("Нет"));
}

void UGratiaAmbience::SetMirrorMode(int32 Mode)
{
    MirrorMode = FMath::Clamp(Mode, 0, 2);
    UWorld* World = GetWorld();
    if (MirrorMode == 0)
    {
        if (Mirror) Mirror->SetActorHiddenInGame(true);
        if (Mirror) Mirror->SetReflectionEnabled(false);
        UE_LOG(LogGratiaAmbience, Display, TEXT("Mirror off"));
        return;
    }
    if (!Mirror && World)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Mirror = World->SpawnActor<AGratiaMirror>(Params);
    }
    if (!Mirror) return;
    const AGratiaPreviewCharacter* Character = GetCharacter();
    const AGratiaStage1Runtime* Runtime = GetRuntime();
    const APawn* Pawn = Runtime ? Runtime->GetPlayerPawn() : nullptr;
    FVector Viewer = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
    if (const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr)
        if (Controller->PlayerCameraManager) Viewer = Controller->PlayerCameraManager->GetCameraLocation();
    Mirror->PlaceFor(Character, Viewer, MirrorMode == 2);
    Mirror->SetActorHiddenInGame(false);
    Mirror->SetReflectionEnabled(true);
    UE_LOG(LogGratiaAmbience, Display, TEXT("Mirror: %s at %s"), *GetMirrorLabel().ToString(), *Mirror->GetActorLocation().ToCompactString());
}

void UGratiaAmbience::NextPose()
{
    if (AGratiaPreviewCharacter* Character = GetCharacter())
    {
        // Only free-play stances: scripted performances belong to their scenes.
        if (Character->PreviewPose == EGratiaPreviewPose::Performance && !Character->IsStance(Character->PerformanceIndex)) return;
        Character->CycleStance();
    }
}

FText UGratiaAmbience::GetPoseLabel() const
{
    const AGratiaPreviewCharacter* Character = GetCharacter();
    return Character ? FText::FromString(Character->GetPoseMenuLabel()) : FText::GetEmpty();
}

void UGratiaAmbience::NextArchetype()
{
    AGratiaPreviewCharacter* Character = GetCharacter();
    if (!Character || !Character->Interaction) return;
    Character->Interaction->Mood = (Character->Interaction->Mood + 1) % 3;
    if (AGratiaStage1Runtime* Runtime = GetRuntime())
        if (Runtime->SceneDirector) Runtime->SceneDirector->SaveUserSettings();
    UE_LOG(LogGratiaAmbience, Display, TEXT("Archetype: %s"), *GetArchetypeLabel().ToString());
}

FText UGratiaAmbience::GetArchetypeLabel() const
{
    const AGratiaPreviewCharacter* Character = GetCharacter();
    const UGratiaBodyMotion* Body = UGratiaBodyMotion::Find(Character);
    return Body && Character && Character->Interaction ? Body->GetArchetypeLabel(Character->Interaction->Mood) : FText::GetEmpty();
}

bool UGratiaAmbience::NextOutfit(int32 Slot)
{
    UGratiaCustomization* Customization = GetCharacter() ? GetCharacter()->FindComponentByClass<UGratiaCustomization>() : nullptr;
    return Customization && Customization->CycleSlot(Slot);
}

FText UGratiaAmbience::GetOutfitLabel(int32 Slot) const
{
    const UGratiaCustomization* Customization = GetCharacter() ? GetCharacter()->FindComponentByClass<UGratiaCustomization>() : nullptr;
    return Customization ? Customization->GetOptionLabel(Slot) : FText::GetEmpty();
}

int32 UGratiaAmbience::GetOutfitSlotCount() const
{
    const UGratiaCustomization* Customization = GetCharacter() ? GetCharacter()->FindComponentByClass<UGratiaCustomization>() : nullptr;
    return Customization ? Customization->GetSlotCount() : 0;
}

void UGratiaAmbience::HandleKeys()
{
    APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (!Controller) return;
    if (Controller->WasInputKeyJustPressed(EKeys::L)) NextLightingPreset();
    if (Controller->WasInputKeyJustPressed(EKeys::M)) NextMirrorMode();
    if (Controller->WasInputKeyJustPressed(EKeys::N)) NextPose();
    if (Controller->WasInputKeyJustPressed(EKeys::K)) NextOutfit(0);
    if (Controller->WasInputKeyJustPressed(EKeys::J)) NextArchetype();
}

void UGratiaAmbience::Tick(float DeltaTime)
{
    if (!bBegun || !FMath::IsFinite(DeltaTime)) return;
    HandleKeys();
    // Environments stream in and out: pick up their lights.
    CollectTimer -= DeltaTime;
    if (CollectTimer <= 0.0f)
    {
        CollectTimer = 2.0f;
        CollectLights(true);
    }
    if (Blend < 1.0f)
    {
        const UGratiaLightingPresets* Presets = GetPresets();
        const float Seconds = Presets ? Presets->TransitionSeconds : 1.5f;
        Blend = Seconds > 0.0f ? FMath::Min(1.0f, Blend + DeltaTime / Seconds) : 1.0f;
        ApplyLights(Blend);
    }
    UpdateMoodLightPlacement();
}

FString UGratiaAmbience::GetDiagnostics() const
{
    const AGratiaPreviewCharacter* Character = GetCharacter();
    const UGratiaBodyMotion* Body = UGratiaBodyMotion::Find(Character);
    const UGratiaCustomization* Customization = Character ? Character->FindComponentByClass<UGratiaCustomization>() : nullptr;
    return FString::Printf(TEXT("ambience: light=%s (%d room, %d static skipped, %d sky, %d fog, %d mood) mirror=%s%s pose=%s archetype=%s\n%s\n%s"),
        *GetLightingLabel().ToString(), RoomLights.Num(), StaticLightsSkipped, Skies.Num(), Fogs.Num(), Moods.Num(),
        *GetMirrorLabel().ToString(), Mirror ? *(TEXT(" ") + Mirror->GetDiagnostics()) : TEXT(""),
        *GetPoseLabel().ToString(), *GetArchetypeLabel().ToString(),
        Body ? *Body->GetDiagnostics() : TEXT("body motion: none"), Customization ? *Customization->GetDiagnostics() : TEXT("customization: none"));
}

namespace
{
    int32 FirstArgument(const TArray<FString>& Args, int32 Default)
    {
        return Args.Num() > 0 && Args[0].IsNumeric() ? FCString::Atoi(*Args[0]) : Default;
    }

    FAutoConsoleCommandWithWorldAndArgs GratiaLightCommand(TEXT("Gratia.Light"), TEXT("Lighting mood: Gratia.Light [index]; without an index the next one."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UGratiaAmbience* Ambience = UGratiaAmbience::Get(World))
                Ambience->SetLightingPreset(FirstArgument(Args, (Ambience->GetLightingPreset() + 1) % FMath::Max(1, Ambience->GetLightingPresetCount())));
        }));
    FAutoConsoleCommandWithWorldAndArgs GratiaMirrorCommand(TEXT("Gratia.Mirror"), TEXT("Mirror: Gratia.Mirror [0 off | 1 full-length | 2 wall]."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UGratiaAmbience* Ambience = UGratiaAmbience::Get(World)) Ambience->SetMirrorMode(FirstArgument(Args, (Ambience->GetMirrorMode() + 1) % 3));
        }));
    FAutoConsoleCommandWithWorldAndArgs GratiaPoseCommand(TEXT("Gratia.Pose"), TEXT("Next free-play stance."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
        {
            if (UGratiaAmbience* Ambience = UGratiaAmbience::Get(World)) Ambience->NextPose();
        }));
    FAutoConsoleCommandWithWorldAndArgs GratiaOutfitCommand(TEXT("Gratia.Outfit"), TEXT("Next option of a customization slot: Gratia.Outfit [slot]."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UGratiaAmbience* Ambience = UGratiaAmbience::Get(World)) Ambience->NextOutfit(FirstArgument(Args, 0));
        }));
    FAutoConsoleCommandWithWorldAndArgs GratiaArchetypeCommand(TEXT("Gratia.Archetype"), TEXT("Character archetype: Gratia.Archetype [0 kuudere | 1 deredere | 2 tsundere]."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            UGratiaAmbience* Ambience = UGratiaAmbience::Get(World);
            AGratiaPreviewCharacter* Character = Ambience ? Ambience->GetCharacter() : nullptr;
            if (!Character || !Character->Interaction) return;
            if (Args.Num() > 0 && Args[0].IsNumeric()) Character->Interaction->Mood = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 2);
            else Ambience->NextArchetype();
        }));
    FAutoConsoleCommandWithWorldAndArgs GratiaAmbienceCommand(TEXT("Gratia.Ambience"), TEXT("Prints lighting, mirror, body motion and customization state."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
        {
            if (UGratiaAmbience* Ambience = UGratiaAmbience::Get(World)) UE_LOG(LogGratiaAmbience, Display, TEXT("%s"), *Ambience->GetDiagnostics());
        }));
}
