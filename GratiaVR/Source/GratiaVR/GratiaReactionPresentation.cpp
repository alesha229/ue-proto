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
#include "CollisionQueryParams.h"
#include "Engine/SkeletalMesh.h"
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
        // Binaural (Resonance Audio's HRTF, the Windows spatialization plugin in DefaultEngine.ini).
        Settings.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF;
        // A real voice: louder the closer it is (natural falloff from 15 cm), darker with distance (air), and drier up
        // close: the room's reverb grows from 5 % at 20 cm to 45 % at 4 m.
        Settings.AttenuationShapeExtents = FVector(15.0f, 0.0f, 0.0f);
        Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
        Settings.dBAttenuationAtMax = -40.0f;
        Settings.bAttenuateWithLPF = true;
        Settings.LPFRadiusMin = 300.0f;
        Settings.LPFRadiusMax = 1500.0f;
        Settings.LPFFrequencyAtMin = 20000.0f;
        Settings.LPFFrequencyAtMax = 7000.0f;
        Settings.bEnableReverbSend = true;
        Settings.ReverbSendMethod = EReverbSendMethod::Linear;
        Settings.ReverbDistanceMin = 20.0f;
        Settings.ReverbDistanceMax = 400.0f;
        Settings.ReverbWetLevelMin = 0.05f;
        Settings.ReverbWetLevelMax = 0.45f;
    }
    if (!NearAttenuation)
    {
        // The near-field copy: positioned by the panner (lows carry little direction), no reverb.
        NearAttenuation = NewObject<USoundAttenuation>(this, TEXT("VoiceNearAttenuation"));
        FSoundAttenuationSettings& Settings = NearAttenuation->Attenuation;
        Settings = ReactionAttenuation->Attenuation;
        Settings.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_Default;
        Settings.bAttenuateWithLPF = false;
        Settings.bEnableReverbSend = false;
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
    if (IsValid(NearAudio.Get())) NearAudio->Stop();
    ActiveAudio = nullptr;
    NearAudio = nullptr;
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
    PlayMouthSound(Sound, 1.0f);
    LastVoiceTime = Now;
}

FVector UGratiaReactionPresentation::GetMouthLocation() const
{
    const AGratiaPreviewCharacter* Owner = Character.Get();
    if (!Owner) return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
    const USkeletalMeshComponent* Mesh = Owner->CharacterMesh;
    const UGratiaCharacterProfile* Profile = Owner->CharacterProfile;
    if (!Mesh || !Profile || !Mesh->GetSkeletalMeshAsset()) return Owner->GetActorLocation();
    auto* Self = const_cast<UGratiaReactionPresentation*>(this);
    if (Self->MouthProfile.Get() != Profile)
    {
        Self->MouthProfile = const_cast<UGratiaCharacterProfile*>(Profile);
        Self->MouthHead = Profile->ResolveBone(TEXT("Head"));
        const FReferenceSkeleton& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
        const int32 Head = Ref.FindBoneIndex(Self->MouthHead);
        FQuat HeadRef = FQuat::Identity;
        for (int32 Bone = Head; Bone != INDEX_NONE; Bone = Ref.GetParentIndex(Bone)) HeadRef = Ref.GetRefBonePose()[Bone].GetRotation() * HeadRef;
        // Character frame (actor-local forward/up) -> component -> head bone (rotation only: imported bones carry a scale).
        const FVector Forward = Profile->ForwardAxis.GetSafeNormal(), Up = Profile->UpAxis.GetSafeNormal();
        const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
        const FVector InActor = Forward * Profile->VoiceMouthOffsetCm.X + Right * Profile->VoiceMouthOffsetCm.Y + Up * Profile->VoiceMouthOffsetCm.Z;
        const FVector InComponent = Mesh->GetRelativeRotation().Quaternion().UnrotateVector(InActor);
        Self->MouthInHead = Head == INDEX_NONE ? InComponent : HeadRef.UnrotateVector(InComponent);
        if (Head == INDEX_NONE) Self->MouthHead = NAME_None;
    }
    if (MouthHead.IsNone()) return Mesh->GetComponentTransform().TransformPosition(MouthInHead);
    const FTransform Head = Mesh->GetSocketTransform(MouthHead, RTS_World);
    return Head.GetLocation() + Head.GetRotation().RotateVector(MouthInHead) * Mesh->GetComponentScale().GetAbsMax();
}

UAudioComponent* UGratiaReactionPresentation::PlayMouthSound(USoundBase* Sound, float Volume)
{
    if (!Sound) return nullptr;
    StopSound();
    const FVector Mouth = GetMouthLocation();
    ActiveVolume = FMath::Clamp(NonnegativePresentation(SoundVolume) * FMath::Clamp(NonnegativePresentation(VoiceVolume), 0.0f, 1.0f)
        * NonnegativePresentation(Volume), 0.0f, 1.0f);
    ActiveAudio = UGameplayStatics::SpawnSoundAtLocation(this, Sound, Mouth, FRotator::ZeroRotator, ActiveVolume, 1.0f, 0.0f, ReactionAttenuation);
    // The near-field layer starts silent and with the same sample: the two stay in phase.
    NearAudio = UGameplayStatics::SpawnSoundAtLocation(this, Sound, Mouth, FRotator::ZeroRotator, ActiveVolume, 1.0f, 0.0f, NearAttenuation);
    if (NearAudio)
    {
        NearAudio->SetLowPassFilterEnabled(true);
        NearAudio->SetLowPassFilterFrequency(NearFieldBassHz);
        NearAudio->SetVolumeMultiplier(0.0f);
    }
    NextOcclusionTrace = 0.0;
    UpdateVoice(0.0f);
    return ActiveAudio;
}

void UGratiaReactionPresentation::UpdateVoice(float DeltaSeconds)
{
    if (!IsValid(ActiveAudio.Get()) || !ActiveAudio->IsPlaying())
    {
        if (IsValid(NearAudio.Get()) && (!IsValid(ActiveAudio.Get()) || !ActiveAudio->IsPlaying())) NearAudio->Stop();
        return;
    }
    const FVector Mouth = GetMouthLocation();
    ActiveAudio->SetWorldLocation(Mouth);
    if (IsValid(NearAudio.Get())) NearAudio->SetWorldLocation(Mouth);
    const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    FVector Ear = Mouth + FVector(1000.0, 0.0, 0.0);
    FVector Front, Right;
    if (Player) Player->GetAudioListenerPosition(Ear, Front, Right);
    const double Distance = FVector::Distance(Ear, Mouth);
    // Occlusion: a trace at 15 Hz from the ear to the mouth, ignoring the character and the player's pawn.
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now >= NextOcclusionTrace)
    {
        NextOcclusionTrace = Now + 1.0 / 15.0;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(GratiaVoiceOcclusion), false);
        Query.AddIgnoredActor(GetOwner());
        if (Player && Player->GetPawn()) Query.AddIgnoredActor(Player->GetPawn());
        FHitResult Hit;
        OcclusionTarget = GetWorld()->LineTraceSingleByChannel(Hit, Ear, Mouth, ECC_Visibility, Query) ? 1.0f : 0.0f;
    }
    Occlusion = FMath::FInterpConstantTo(Occlusion, OcclusionTarget, FMath::Max(0.0f, DeltaSeconds), 6.0f);
    ActiveAudio->SetLowPassFilterEnabled(Occlusion > 0.01f);
    ActiveAudio->SetLowPassFilterFrequency(FMath::Lerp(20000.0f, OccludedLowPassHz, Occlusion));
    ActiveAudio->SetVolumeMultiplier(ActiveVolume * FMath::Lerp(1.0f, OccludedVolume, Occlusion));
    if (IsValid(NearAudio.Get()))
    {
        const float Near = 1.0f - FMath::SmoothStep(NearFieldFullCm, FMath::Max(NearFieldFullCm + 1.0f, NearFieldCm), float(Distance));
        NearAudio->SetVolumeMultiplier(ActiveVolume * NearFieldBassGain * Near * (1.0f - Occlusion));
    }
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
    UpdateVoice(DeltaSeconds);
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
