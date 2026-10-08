#include "GratiaReactionPresentation.h"

#include "GratiaBubbleWidget.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    float NonnegativePresentation(float Value)
    {
        return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
    }
    // The bubble pops in (85 % -> 100 % size) and fades out over the end of its time.
    constexpr float BubblePopSeconds = 0.12f;
    constexpr float BubbleFadeSeconds = 0.3f;
}

UGratiaReactionPresentation::UGratiaReactionPresentation()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    static ConstructorHelpers::FObjectFinder<UFont> Font(TEXT("/Game/Gratia/Experience/Font/F_Nunito.F_Nunito"));
    BubbleFont = Font.Object;
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
    CaptionText = FText::GetEmpty();
    PresentedZone = NAME_None;
    if (Bubble) Bubble->SetVisibility(false);
    StopSound();
    // Preserve the rate limiter through a pose/profile reset, as the original presenter did.
    UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    if (PresentedProfile.Get() != Profile) LastLineIndex = INDEX_NONE;
    PresentedProfile = Profile;
}

FString UGratiaReactionPresentation::GetCaptionText() const
{
    return CaptionText.ToString();
}

bool UGratiaReactionPresentation::IsCaptionVisible() const
{
    return bPresentCaptions && CaptionSeconds > 0.0f && !CaptionText.IsEmpty();
}

const FGratiaReactionLine* UGratiaReactionPresentation::SelectLine(FName ZoneName, int32 Mood, bool bStrong, const UGratiaCharacterProfile& Profile)
{
    // Most specific first: a strong-touch line, then one for this zone or channel, then for this mood.
    int32 BestScore = -1;
    TArray<int32, TInlineAllocator<16>> Best;
    for (int32 Index = 0; Index < Profile.ReactionLines.Num(); ++Index)
    {
        const FGratiaReactionLine& Line = Profile.ReactionLines[Index];
        if (Line.Mood >= 0 && Line.Mood != Mood) continue;
        if (!Line.Zones.IsEmpty() && !Line.Zones.Contains(ZoneName)) continue;
        if (Line.Force != EGratiaReactionForce::Any && (Line.Force == EGratiaReactionForce::Strong) != bStrong) continue;
        const int32 Score = (Line.Force == EGratiaReactionForce::Strong ? 8 : Line.Force == EGratiaReactionForce::Gentle ? 1 : 0)
            + (Line.Zones.IsEmpty() ? 0 : 4) + (Line.Mood >= 0 ? 2 : 0);
        if (Score > BestScore)
        {
            BestScore = Score;
            Best.Reset();
        }
        if (Score == BestScore) Best.Add(Index);
    }
    if (Best.IsEmpty()) return nullptr;
    if (Best.Num() > 1) Best.Remove(LastLineIndex);
    LastLineIndex = Best[FMath::RandRange(0, Best.Num() - 1)];
    return &Profile.ReactionLines[LastLineIndex];
}

USoundBase* UGratiaReactionPresentation::SelectSound(FName ZoneName, const UGratiaCharacterProfile& Profile) const
{
    if (const auto* Sound = Profile.ReactionSounds.Find(ZoneName))
        if (IsValid(Sound->Get())) return Sound->Get();
    if (const auto* Sound = Profile.ReactionSounds.Find(TEXT("Default")))
        if (IsValid(Sound->Get())) return Sound->Get();
    return IsValid(Profile.DefaultReactionSound.Get()) ? Profile.DefaultReactionSound.Get() : nullptr;
}

bool UGratiaReactionPresentation::EnsureBubble()
{
    if (Bubble && BubbleWidget) return true;
    APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0);
    if (!Player || !GetOwner()) return false;
    if (!Bubble)
    {
        Bubble = NewObject<UWidgetComponent>(GetOwner(), TEXT("ReactionBubble"));
        GetOwner()->AddInstanceComponent(Bubble);
        Bubble->SetupAttachment(GetOwner()->GetRootComponent());
        Bubble->SetUsingAbsoluteLocation(true);
        Bubble->SetUsingAbsoluteRotation(true);
        Bubble->SetUsingAbsoluteScale(true);
        Bubble->SetWidgetSpace(EWidgetSpace::World);
        Bubble->SetDrawAtDesiredSize(true);
        // Left edge (as the player sees it) at the anchor beside the head: the bubble grows away from her.
        Bubble->SetPivot(FVector2D(0.0, 1.0));
        Bubble->SetTwoSided(false);
        Bubble->SetWindowFocusable(false);
        Bubble->SetBlendMode(EWidgetBlendMode::Transparent);
        Bubble->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Bubble->SetCastShadow(false);
        Bubble->RegisterComponent();
        Bubble->SetVisibility(false);
    }
    BubbleWidget = CreateWidget<UGratiaBubbleWidget>(Player, UGratiaBubbleWidget::StaticClass());
    if (!BubbleWidget) return false;
    BubbleWidget->Font = BubbleFont;
    BubbleWidget->Accent = BubbleAccent;
    Bubble->SetOwnerPlayer(Player->GetLocalPlayer());
    Bubble->SetWidget(BubbleWidget);
    return true;
}

void UGratiaReactionPresentation::PlaceBubble()
{
    if (!Bubble || !Character.IsValid()) return;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get();
    FVector Anchor = Character->GetActorTransform().TransformPosition(Profile ? Profile->ContactSettings.CaptionOffset : FVector(0.0, 0.0, 200.0));
    const FName Head = Profile ? Profile->ResolveBone(TEXT("Head")) : NAME_None;
    const bool bHead = !Head.IsNone() && Character->CharacterMesh && Character->CharacterMesh->GetBoneIndex(Head) != INDEX_NONE;
    if (bHead) Anchor = Character->CharacterMesh->GetSocketLocation(Head) + FVector(0.0, 0.0, BubbleAboveHead);
    const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    const UCameraComponent* View = Player ? Player->FindComponentByClass<UCameraComponent>() : nullptr;
    if (View)
    {
        FVector Towards = View->GetComponentLocation() - Anchor;
        Towards.Z = 0.0;
        if (bHead && Towards.Normalize())
        {
            // The player's right: up x view direction (the view looks back along Towards).
            const FVector Right = FVector::UpVector ^ -Towards;
            Anchor += Towards * BubbleTowardsPlayer + Right * BubbleBeside;
        }
        Bubble->SetWorldRotation((View->GetComponentLocation() - Anchor).Rotation());
    }
    Bubble->SetWorldLocation(Anchor);
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
    const FGratiaReactionLine* Line = SelectLine(ZoneName, Mood, bStrong, *Profile);
    PresentedZone = ZoneName;
    CaptionText = Line ? Line->Text : FText::GetEmpty();
    CaptionSeconds = bPresentCaptions && !CaptionText.IsEmpty() ? NonnegativePresentation(Settings.CaptionSeconds) : 0.0f;
    CaptionAge = 0.0f;
    if (CaptionSeconds > 0.0f && EnsureBubble())
    {
        BubbleWidget->SetLine(CaptionText);
        BubbleWidget->SetRenderOpacity(1.0f);
        Bubble->SetWorldScale3D(FVector(BubbleScale * 0.85f));
        PlaceBubble();
        Bubble->SetVisibility(true);
    }
    else if (Bubble) Bubble->SetVisibility(false);

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if (!bPresentSound || !Source->bSound || !Profile->Capabilities.bSound
        || Now - LastVoiceTime <= NonnegativePresentation(Settings.CooldownSeconds)) return;
    USoundBase* Sound = nullptr;
    if (Line)
    {
        TArray<USoundBase*> Takes;
        if (IsValid(Line->Sound.Get())) Takes.Add(Line->Sound.Get());
        for (USoundBase* Take : Line->Variants) if (IsValid(Take)) Takes.AddUnique(Take);
        if (Takes.Num() > 1) Takes.Remove(LastVoice.Get());
        if (!Takes.IsEmpty()) Sound = Takes[FMath::RandRange(0, Takes.Num() - 1)];
    }
    if (!Sound) Sound = SelectSound(ZoneName, *Profile);
    LastVoice = Sound;
    if (!Sound && Profile->ReactionLines.IsEmpty())
    {
        // A profile without lines keeps the short local acknowledgement.
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
    if (!Sound) return;
    FVector Location = Character->GetActorLocation();
    const int32 ZoneIndex = Source->Zones.IndexOfByPredicate(
        [ZoneName](const FGratiaContactZone& Zone) { return Zone.Name == ZoneName; });
    if (ZoneIndex != INDEX_NONE) Location = Source->GetZoneWorldPosition(ZoneIndex);
    StopSound();
    ActiveAudio = UGameplayStatics::SpawnSoundAtLocation(this, Sound, Location, FRotator::ZeroRotator,
        FMath::Clamp(NonnegativePresentation(SoundVolume) * FMath::Clamp(NonnegativePresentation(VoiceVolume), 0.0f, 1.0f), 0.0f, 1.0f),
        1.0f, 0.0f, ReactionAttenuation);
    LastVoiceTime = Now;
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
    CaptionAge += FMath::Min(DeltaSeconds, 0.05f);
    if (CaptionSeconds <= 0.0f) CaptionText = FText::GetEmpty();
    if (!Bubble) return;
    const bool bShow = IsCaptionVisible() && BubbleWidget;
    Bubble->SetVisibility(bShow);
    if (!bShow) return;
    PlaceBubble();
    Bubble->SetWorldScale3D(FVector(BubbleScale * (0.85f + 0.15f * FMath::SmoothStep(0.0f, BubblePopSeconds, CaptionAge))));
    BubbleWidget->SetRenderOpacity(FMath::Clamp(CaptionSeconds / BubbleFadeSeconds, 0.0f, 1.0f));
}
