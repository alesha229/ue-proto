#include "GratiaReactionPresentation.h"

#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
    float NonnegativePresentation(float Value)
    {
        return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
    }
}

UGratiaReactionPresentation::UGratiaReactionPresentation()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGratiaReactionPresentation::BeginPlay()
{
    if (!ReactionAttenuation)
    {
        ReactionAttenuation = NewObject<USoundAttenuation>(this, TEXT("ReactionAttenuation"));
        FSoundAttenuationSettings& Settings = ReactionAttenuation->Attenuation;
        Settings.bAttenuate = true;
        Settings.bSpatialize = true;
        Settings.AttenuationShape = EAttenuationShape::Sphere;
        Settings.AttenuationShapeExtents = FVector(30.0f, 0.0f, 0.0f);
        Settings.FalloffDistance = 1200.0f;
        // HRTF when a spatialization plugin is active, otherwise the engine's stereo panner.
        Settings.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF;
    }
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    if (!Character.IsValid() || !Character->Interaction) return;
    // This is the explicitly owned sibling, not a search for an arbitrary component.
    Source = Character->Interaction;
    Source->OnContactReaction.AddUniqueDynamic(this, &UGratiaReactionPresentation::HandleContactReaction);
    Source->OnContactReset.AddUniqueDynamic(this, &UGratiaReactionPresentation::ResetPresentation);
    AddTickPrerequisiteComponent(Source.Get());

    Caption = NewObject<UTextRenderComponent>(GetOwner(), TEXT("ReactionCaption"));
    GetOwner()->AddInstanceComponent(Caption);
    Caption->SetupAttachment(GetOwner()->GetRootComponent());
    Caption->SetWorldSize(FMath::Max(0.1f, NonnegativePresentation(CaptionWorldSize)));
    Caption->SetHorizontalAlignment(EHTA_Center);
    Caption->SetTextRenderColor(CaptionColor);
    Caption->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Caption->RegisterComponent();
    PresentedProfile = Character->CharacterProfile;
    ResetPresentation();
}

void UGratiaReactionPresentation::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Source.IsValid())
    {
        Source->OnContactReaction.RemoveDynamic(this, &UGratiaReactionPresentation::HandleContactReaction);
        Source->OnContactReset.RemoveDynamic(this, &UGratiaReactionPresentation::ResetPresentation);
    }
    ResetPresentation();
    Super::EndPlay(EndPlayReason);
}

void UGratiaReactionPresentation::StopSound()
{
    if (IsValid(ActiveAudio.Get())) ActiveAudio->Stop();
    ActiveAudio = nullptr;
}

void UGratiaReactionPresentation::ResetPresentation()
{
    CaptionSeconds = 0.0f;
    if (Caption)
    {
        Caption->SetText(FText::GetEmpty());
        Caption->SetVisibility(false);
    }
    StopSound();
    // Preserve the rate limiter through a pose/profile reset, as the original presenter did.
    PresentedProfile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
}

FString UGratiaReactionPresentation::GetCaptionText() const
{
    return Caption ? Caption->Text.ToString() : FString();
}

bool UGratiaReactionPresentation::IsCaptionVisible() const
{
    return Caption && Caption->IsVisible() && CaptionSeconds > 0.0f;
}

USoundBase* UGratiaReactionPresentation::SelectSound(FName ZoneName, const UGratiaCharacterProfile& Profile) const
{
    if (const auto* Sound = Profile.ReactionSounds.Find(ZoneName))
        if (IsValid(Sound->Get())) return Sound->Get();
    if (const auto* Sound = Profile.ReactionSounds.Find(TEXT("Default")))
        if (IsValid(Sound->Get())) return Sound->Get();
    return IsValid(Profile.DefaultReactionSound.Get()) ? Profile.DefaultReactionSound.Get() : nullptr;
}

void UGratiaReactionPresentation::HandleContactReaction(FName ZoneName, int32 HandIndex, float HandSpeed, int32 Mood)
{
    if (!Character.IsValid() || !Source.IsValid()) return;
    UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get();
    if (!Profile) return;
    if (PresentedProfile.Get() != Profile) ResetPresentation();
    const FGratiaContactSettings& Settings = Profile->ContactSettings;
    const bool bStrong = FMath::IsFinite(HandSpeed)
        && HandSpeed > NonnegativePresentation(Settings.StrongReactionSpeedCmPerSecond);
    if (Caption && bPresentCaptions)
    {
        Caption->SetText(FText::FromString(FString::Printf(TEXT("%s%s"),
            bStrong ? TEXT("Easy... ") : Mood == 2 ? TEXT("Hmm. ") : TEXT("Hey! "), *ZoneName.ToString())));
        Caption->SetWorldLocation(Character->GetActorTransform().TransformPosition(Settings.CaptionOffset));
        Caption->SetWorldSize(FMath::Max(0.1f, NonnegativePresentation(CaptionWorldSize)));
        Caption->SetTextRenderColor(CaptionColor);
        CaptionSeconds = NonnegativePresentation(Settings.CaptionSeconds);
        Caption->SetVisibility(CaptionSeconds > 0.0f);
    }

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if (!bPresentSound || !Source->bSound || !Profile->Capabilities.bSound
        || Now - LastChimeTime <= NonnegativePresentation(Settings.CooldownSeconds)) return;
    USoundBase* Sound = SelectSound(ZoneName, *Profile);
    if (!Sound)
    {
        // Keep the existing short local acknowledgement when no authored resource is configured.
        USoundWaveProcedural* Chime = NewObject<USoundWaveProcedural>(this);
        Chime->SetSampleRate(24000); Chime->NumChannels = 1; Chime->Duration = 0.12f;
        TArray<int16> Samples; Samples.SetNum(2880);
        for (int32 Index = 0; Index < Samples.Num(); ++Index)
        {
            const double Time = double(Index) / 24000.0;
            const double Envelope = FMath::Sin(PI * Index / Samples.Num());
            Samples[Index] = int16(1100.0 * Envelope
                * FMath::Sin(2.0 * PI * (bStrong ? 390.0 : 620.0) * Time));
        }
        Chime->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
        Sound = Chime;
    }
    FVector Location = Character->GetActorLocation();
    const int32 ZoneIndex = Source->Zones.IndexOfByPredicate(
        [ZoneName](const FGratiaContactZone& Zone) { return Zone.Name == ZoneName; });
    if (ZoneIndex != INDEX_NONE) Location = Source->GetZoneWorldPosition(ZoneIndex);
    StopSound();
    ActiveAudio = UGameplayStatics::SpawnSoundAtLocation(this, Sound, Location, FRotator::ZeroRotator,
        FMath::Clamp(NonnegativePresentation(SoundVolume), 0.0f, 1.0f), 1.0f, 0.0f, ReactionAttenuation);
    LastChimeTime = Now;
}

void UGratiaReactionPresentation::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);
    if (!Character.IsValid() || !Source.IsValid()) return;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get();
    if (PresentedProfile.Get() != Profile) ResetPresentation();
    if (!Profile)
    {
        ResetPresentation();
        return;
    }
    if (!bPresentSound || !Source->bSound || !Profile->Capabilities.bSound) StopSound();
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
    CaptionSeconds = FMath::Max(0.0f, CaptionSeconds - FMath::Min(DeltaSeconds, 0.05f));
    if (!Caption) return;
    Caption->SetVisibility(bPresentCaptions && CaptionSeconds > 0.0f);
    if (Caption->IsVisible())
    {
        APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
        UCameraComponent* View = Player ? Player->FindComponentByClass<UCameraComponent>() : nullptr;
        if (View) Caption->SetWorldRotation((View->GetComponentLocation() - Caption->GetComponentLocation()).Rotation());
    }
}
