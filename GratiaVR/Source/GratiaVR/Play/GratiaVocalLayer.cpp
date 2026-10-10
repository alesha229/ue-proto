#include "GratiaVocalLayer.h"
#include "GratiaPlaySubsystem.h"
#include "GratiaArousal.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaReactionPresentation.h"
#if __has_include("GratiaBodyMotion.h")
#include "GratiaBodyMotion.h"
#define GRATIA_VOCAL_HAS_BODY_MOTION 1
#else
#define GRATIA_VOCAL_HAS_BODY_MOTION 0
#endif
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "GratiaSynthSound.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaVocal, Log, All);

namespace
{
    EGratiaVocalBank ToBank(GratiaPlay::EVocal Vocal)
    {
        switch (Vocal)
        {
        case GratiaPlay::EVocal::InhaleSoft: return EGratiaVocalBank::InhaleSoft;
        case GratiaPlay::EVocal::InhaleDeep: return EGratiaVocalBank::InhaleDeep;
        case GratiaPlay::EVocal::Sigh: return EGratiaVocalBank::Sigh;
        case GratiaPlay::EVocal::HeldRelease: return EGratiaVocalBank::HeldRelease;
        case GratiaPlay::EVocal::Broken: return EGratiaVocalBank::Broken;
        case GratiaPlay::EVocal::NonVerbalSoft: return EGratiaVocalBank::NonVerbalSoft;
        case GratiaPlay::EVocal::NonVerbalStrong: return EGratiaVocalBank::NonVerbalStrong;
        case GratiaPlay::EVocal::EarWhisper: return EGratiaVocalBank::EarWhisper;
        default: return EGratiaVocalBank::ExhaleSoft;
        }
    }

    /** Mouth offset in the head bone's own space, from profile axes (component space of the reference pose). */
    bool MouthLocal(const USkeletalMeshComponent* Mesh, const UGratiaCharacterProfile* Profile, FName Head, float Forward, float Up, FVector& Out)
    {
        const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
        if (!Asset || !Profile || Head.IsNone()) return false;
        const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
        const int32 Index = Ref.FindBoneIndex(Head);
        if (Index == INDEX_NONE) return false;
        // Component rotation of the head in the reference pose (root to head).
        FQuat Component = FQuat::Identity;
        TArray<int32> Chain;
        for (int32 Bone = Index; Bone != INDEX_NONE; Bone = Ref.GetParentIndex(Bone)) Chain.Insert(Bone, 0);
        for (int32 Bone : Chain) Component = Component * Ref.GetRefBonePose()[Bone].GetRotation();
        const FVector OffsetCS = Profile->ForwardAxis.GetSafeNormal() * Forward + Profile->UpAxis.GetSafeNormal() * Up;
        Out = Component.Inverse().RotateVector(OffsetCS);
        return !Out.ContainsNaN();
    }
}

UGratiaVocalLayer::UGratiaVocalLayer()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UGratiaVocalLayer::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    Breath.Seed ^= uint32(GetUniqueID());
    if (Character.IsValid() && Character->Interaction)
    {
        Character->Interaction->OnContactReaction.AddDynamic(this, &UGratiaVocalLayer::HandleReaction);
        bBound = true;
    }
}

void UGratiaVocalLayer::EndPlay(const EEndPlayReason::Type Reason)
{
    if (bBound && Character.IsValid() && Character->Interaction)
        Character->Interaction->OnContactReaction.RemoveDynamic(this, &UGratiaVocalLayer::HandleReaction);
    bBound = false;
    for (TObjectPtr<UAudioComponent>& Loop : Loops)
        if (Loop) { Loop->Stop(); Loop = nullptr; }
    Super::EndPlay(Reason);
}

void UGratiaVocalLayer::HandleReaction(FName ZoneName, int32 HandIndex, float HandSpeed, int32 Mood)
{
    const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Character.Get());
    const UWorld* World = GetWorld();
    if (!Settings || !World) return;
    // The reaction line is voiced by the presenter: the breath keeps quiet meanwhile.
    QuietUntil = World->GetTimeSeconds() + Settings->ReactionQuietSeconds;
    // A sudden fast touch while she is still calm: the breath catches and is let out afterwards.
    const UGratiaArousal* Arousal = GetOwner()->FindComponentByClass<UGratiaArousal>();
    if (HandSpeed > 80.0f && (!Arousal || Arousal->GetArousal() < 0.5f) && Breath.HoldSeconds <= 0.0f)
        HoldBreath(Settings->ReactionQuietSeconds + 0.3f + 0.4f * GratiaPlay::Random01(Breath.Seed));
}

void UGratiaVocalLayer::HoldBreath(float Seconds)
{
    if (!FMath::IsFinite(Seconds) || Seconds <= 0.0f) return;
    Breath.HoldSeconds = FMath::Min(Seconds, 4.0f);
    bAfterHold = true;
}

USoundBase* UGratiaVocalLayer::Pick(const FGratiaSoundBank& Bank, uint8 Key)
{
    TArray<int32, TInlineAllocator<16>> Valid;
    for (int32 I = 0; I < Bank.Sounds.Num(); ++I) if (Bank.Sounds[I]) Valid.Add(I);
    if (Valid.IsEmpty()) return nullptr;
    int32& Last = LastPick.FindOrAdd(Key, INDEX_NONE);
    int32 Choice = Valid[FMath::RandRange(0, Valid.Num() - 1)];
    // Never the same take twice in a row when the bank has more.
    if (Valid.Num() > 1 && Choice == Last) Choice = Valid[(Valid.IndexOfByKey(Choice) + 1) % Valid.Num()];
    Last = Choice;
    return Bank.Sounds[Choice];
}

bool UGratiaVocalLayer::GetMouth(FVector& Out) const
{
    const AGratiaPreviewCharacter* Owner = Character.Get();
    const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Owner);
    const USkeletalMeshComponent* Mesh = Owner ? Owner->CharacterMesh.Get() : nullptr;
    const UGratiaCharacterProfile* Profile = Owner ? Owner->CharacterProfile.Get() : nullptr;
    const FName Head = Profile ? Profile->ResolveBone(TEXT("Head")) : NAME_None;
    FVector Local;
    if (!Settings || !Mesh || !MouthLocal(Mesh, Profile, Head, Settings->MouthForwardCm, Settings->MouthUpCm, Local)) return false;
    Out = Mesh->GetSocketTransform(Head).TransformPosition(Local);
    return true;
}

void UGratiaVocalLayer::PlayVocal(EGratiaVocalBank Bank, float VolumeScale)
{
    AGratiaPreviewCharacter* Owner = Character.Get();
    const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Owner);
    if (!Owner || !Settings || !Owner->CharacterMesh || !UGratiaPlaySubsystem::IsLayerEnabled()) return;
    const FGratiaSoundBank* Found = Settings->VocalBanks.Find(Bank);
    USoundBase* Sound = Found ? Pick(*Found, uint8(Bank)) : nullptr;
    UGratiaSynthSound* Synth = nullptr;
    if (!Sound)
    {
        bool bAlready = false;
        ReportedVocal.Add(uint8(Bank), &bAlready);
        if (!bAlready) UE_LOG(LogGratiaVocal, Display, TEXT("VOCAL bank %s is empty (PlaySettings.VocalBanks): %s."),
            *UEnum::GetValueAsString(Bank), Settings->bSynthFallback ? TEXT("synth placeholder") : TEXT("silent"));
        if (!Settings->bSynthFallback) return;
        Sound = Synth = UGratiaSynthSound::MakeVocal(this, Bank);
    }
    const float BankVolume = Found ? Found->Volume : 1.0f;
    const float BankJitter = Found ? Found->PitchJitter : 0.05f;
    const UGratiaCharacterProfile* Profile = Owner->CharacterProfile;
    const FName Head = Profile ? Profile->ResolveBone(TEXT("Head")) : NAME_None;
    FVector Local = FVector::ZeroVector;
    MouthLocal(Owner->CharacterMesh, Profile, Head, Settings->MouthForwardCm, Settings->MouthUpCm, Local);
    USoundAttenuation* Attenuation = Settings->VoiceAttenuation ? Settings->VoiceAttenuation.Get()
        : Owner->ReactionPresentation ? Owner->ReactionPresentation->ReactionAttenuation.Get() : nullptr;
    const float Voice = Owner->ReactionPresentation ? Owner->ReactionPresentation->VoiceVolume : 1.0f;
    const float Volume = BankVolume * Settings->BreathVolume * Voice * FMath::Max(0.0f, VolumeScale);
    const float Pitch = 1.0f + FMath::FRandRange(-BankJitter, BankJitter);
    // Attached to the head at the mouth: the voice moves with her head (binaural path of the voice thread).
    UAudioComponent* Audio = UGameplayStatics::SpawnSoundAttached(Sound, Owner->CharacterMesh, Head, Local, EAttachLocation::KeepRelativeOffset, true,
        Volume, Pitch, 0.0f, Attenuation, nullptr, true);
    // A synthesised one-shot streams forever; stop it when its envelope has ended.
    if (Audio && Synth) Audio->StopDelayed(Synth->GetLength() / FMath::Max(Pitch, 0.1f) + 0.05f);
    ++Played;
}

void UGratiaVocalLayer::PlayFoley(EGratiaFoleyBank Bank, FVector Location, float VolumeScale)
{
    AGratiaPreviewCharacter* Owner = Character.Get();
    const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Owner);
    if (!Owner || !Settings || !bFoley || !UGratiaPlaySubsystem::IsLayerEnabled() || Location.ContainsNaN()) return;
    const FGratiaSoundBank* Found = Settings->FoleyBanks.Find(Bank);
    USoundBase* Sound = Found ? Pick(*Found, uint8(100 + uint8(Bank))) : nullptr;
    UGratiaSynthSound* Synth = nullptr;
    if (!Sound)
    {
        bool bAlready = false;
        ReportedFoley.Add(uint8(Bank), &bAlready);
        if (!bAlready) UE_LOG(LogGratiaVocal, Display, TEXT("FOLEY bank %s is empty (PlaySettings.FoleyBanks): %s."),
            *UEnum::GetValueAsString(Bank), Settings->bSynthFallback ? TEXT("synth placeholder") : TEXT("silent"));
        if (!Settings->bSynthFallback) return;
        Sound = Synth = UGratiaSynthSound::MakeFoley(this, Bank);
    }
    const float BankVolume = Found ? Found->Volume : 1.0f;
    const float BankJitter = Found ? Found->PitchJitter : 0.05f;
    USoundAttenuation* Attenuation = Settings->VoiceAttenuation ? Settings->VoiceAttenuation.Get()
        : Owner->ReactionPresentation ? Owner->ReactionPresentation->ReactionAttenuation.Get() : nullptr;
    const float Pitch = 1.0f + FMath::FRandRange(-BankJitter, BankJitter);
    UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(this, Sound, Location, FRotator::ZeroRotator,
        BankVolume * Settings->FoleyVolume * FMath::Max(0.0f, VolumeScale), Pitch, 0.0f, Attenuation);
    if (Audio && Synth) Audio->StopDelayed(Synth->GetLength() / FMath::Max(Pitch, 0.1f) + 0.05f);
}

float UGratiaVocalLayer::GetWetness() const
{
    const AGratiaPreviewCharacter* Owner = Character.Get();
    const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Owner);
    const USkeletalMeshComponent* Mesh = Owner ? Owner->CharacterMesh.Get() : nullptr;
    if (!Settings || !Mesh) return 0.0f;
    float Wet = 0.0f;
    for (int32 I = 0; I < Mesh->GetNumMaterials(); ++I)
    {
        const UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(I));
        if (!Material) continue;
        float Value = 0.0f;
        for (const FName Parameter : { Settings->WetnessParameter, Settings->OilParameter })
            if (!Parameter.IsNone() && Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Parameter), Value, true)) Wet = FMath::Max(Wet, Value);
    }
    return Wet;
}

void UGratiaVocalLayer::UpdateLoops(const UGratiaPlaySettings& Settings, float Delta)
{
    UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this);
    if (!Play) return;
    const float Wet = GetWetness();
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const UGratiaPlaySubsystem::FHand& Hand = Play->GetHand(Index == 0);
        const bool bSliding = bFoley && Hand.bAllowed && Hand.Zone != INDEX_NONE && Hand.ZoneGap <= 1.5f && Hand.Speed > 3.0f;
        const EGratiaFoleyBank Bank = Settings.ClothedZones.Contains(Hand.ZoneName) ? EGratiaFoleyBank::ClothRustle
            : Wet >= Settings.WetFoleyThreshold ? EGratiaFoleyBank::WetSlide : EGratiaFoleyBank::SkinSlide;
        const FGratiaSoundBank* Found = Settings.FoleyBanks.Find(Bank);
        const bool bHasBank = Found || Settings.bSynthFallback;
        const float Target = bSliding && bHasBank ? (Found ? Found->Volume : 1.0f) * Settings.FoleyVolume * FMath::Clamp(Hand.Speed / 40.0f, 0.0f, 1.0f) : 0.0f;
        LoopVolume[Index] = GratiaPlay::Envelope(LoopVolume[Index], Target, 0.05f, 0.25f, Delta);
        UAudioComponent* Loop = Loops[Index];
        if (Loop && (LoopBank[Index] != Bank && Target > 0.0f))
        {
            Loop->Stop();
            Loops[Index] = Loop = nullptr;
        }
        if (!Loop && Target > 0.0f && bHasBank)
        {
            USoundBase* Sound = Found ? Pick(*Found, uint8(200 + uint8(Bank))) : nullptr;
            if (!Sound && Settings.bSynthFallback) Sound = UGratiaSynthSound::MakeFoley(this, Bank);
            const AGratiaPreviewCharacter* Owner = Character.Get();
            USoundAttenuation* Attenuation = Settings.VoiceAttenuation ? Settings.VoiceAttenuation.Get()
                : Owner && Owner->ReactionPresentation ? Owner->ReactionPresentation->ReactionAttenuation.Get() : nullptr;
            if (Sound)
            {
                Loop = UGameplayStatics::SpawnSoundAtLocation(this, Sound, Hand.World.GetLocation(), FRotator::ZeroRotator, 0.01f, 1.0f, 0.0f, Attenuation, nullptr, false);
                Loops[Index] = Loop;
                LoopBank[Index] = Bank;
            }
        }
        if (!Loop) continue;
        if (LoopVolume[Index] < 0.01f && Target <= 0.0f)
        {
            Loop->Stop();
            Loops[Index] = nullptr;
            continue;
        }
        Loop->SetWorldLocation(Hand.World.GetLocation());
        Loop->SetVolumeMultiplier(FMath::Max(0.001f, LoopVolume[Index]));
        // A one-shot take in a slide bank restarts while the hand keeps moving.
        if (!Loop->IsPlaying()) Loop->Play();
    }
}

void UGratiaVocalLayer::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Character.Get());
    UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this);
    if (!Settings || !Play || !Character.IsValid()) return;
    const UGratiaArousal* Arousal = GetOwner()->FindComponentByClass<UGratiaArousal>();
    const float Level = Arousal ? Arousal->GetArousal() : 0.0f;
    const float Stimulus = Arousal ? Arousal->GetStimulus() : 0.0f;
    const float Rough = Arousal ? Arousal->GetRoughness() : 0.0f;
    BreathRate = GratiaPlay::BreathRate(Level, 0.6f * Stimulus);
    const float Irregularity = FMath::Clamp((Level - 0.45f) * 1.8f + 0.5f * Rough, 0.0f, 1.0f);
    GratiaPlay::EBreathEvent Event = GratiaPlay::EBreathEvent::None;
#if GRATIA_VOCAL_HAS_BODY_MOTION
    // One rhythm for chest and sound: the body layer breathes, the voice follows its phase (inhale in the first 40%).
    if (const UGratiaBodyMotion* Body = UGratiaBodyMotion::Find(GetOwner()))
    {
        BreathRate = Body->BreathsPerMinute;
        const bool bInhale = Body->BreathPhase < 0.4f;
        if (Breath.HoldSeconds > 0.0f)
        {
            Breath.HoldSeconds -= GratiaPlay::SafeDelta(Delta);
            if (Breath.HoldSeconds <= 0.0f) { Breath.HoldSeconds = 0.0f; Event = GratiaPlay::EBreathEvent::Exhale; }
        }
        else if (bInhale != Breath.bInhale) Event = bInhale ? GratiaPlay::EBreathEvent::Inhale : GratiaPlay::EBreathEvent::Exhale;
        Breath.bInhale = bInhale;
        Breath.Phase = Body->BreathPhase;
        bFollowsBody = true;
    }
    else
#endif
    {
        Event = GratiaPlay::StepBreath(Breath, BreathRate, Irregularity, Delta);
        bFollowsBody = false;
    }
    if (!UGratiaPlaySubsystem::IsLayerEnabled()) return;
    UpdateLoops(*Settings, Delta);
    if (!bBreath || Event == GratiaPlay::EBreathEvent::None) return;

    const bool bQuiet = GetWorld()->GetTimeSeconds() < QuietUntil;
    FVector Head, Mouth;
    const bool bEar = Play->GetHead(Head) && GetMouth(Mouth) && FVector::Distance(Head, Mouth) <= Settings->EarCloseCm;
    const bool bAudible = Level >= Settings->AudibleBreathArousal || bEar;
    const float Volume = FMath::Lerp(0.35f, 1.0f, Level) * (bEar ? 1.4f : 1.0f);
    if (bQuiet) { bAfterHold = bAfterHold && Breath.HoldSeconds > 0.0f; return; }
    if (Event == GratiaPlay::EBreathEvent::Inhale)
    {
        if (!bAudible) return;
        const bool bWhisper = bEar && (Settings->VocalBanks.Contains(EGratiaVocalBank::EarWhisper) || Settings->bSynthFallback) && GratiaPlay::Random01(Breath.Seed) < 0.5f;
        PlayVocal(bWhisper ? EGratiaVocalBank::EarWhisper : Level > 0.6f ? EGratiaVocalBank::InhaleDeep : EGratiaVocalBank::InhaleSoft, Volume);
        return;
    }
    const GratiaPlay::EVocal Choice = GratiaPlay::ChooseExhale(Level, Stimulus, bAfterHold, GratiaPlay::Random01(Breath.Seed));
    bAfterHold = false;
    const bool bVoiced = Choice == GratiaPlay::EVocal::NonVerbalSoft || Choice == GratiaPlay::EVocal::NonVerbalStrong || Choice == GratiaPlay::EVocal::HeldRelease;
    if (!bAudible && !bVoiced) return;
    PlayVocal(bEar && !bVoiced && (Settings->VocalBanks.Contains(EGratiaVocalBank::EarWhisper) || Settings->bSynthFallback) ? EGratiaVocalBank::EarWhisper : ToBank(Choice), Volume);
}

FString UGratiaVocalLayer::GetDiagnostics() const
{
    return FString::Printf(TEXT("breath rate=%.0f/min follows_body=%d curve=%.2f hold=%.1fs played=%d vocal_banks=%d foley_banks=%d loops=%d/%d"),
        BreathRate, bFollowsBody, GratiaPlay::BreathCurve(Breath), Breath.HoldSeconds, Played,
        UGratiaPlaySubsystem::GetSettings(Character.Get()) ? UGratiaPlaySubsystem::GetSettings(Character.Get())->VocalBanks.Num() : 0,
        UGratiaPlaySubsystem::GetSettings(Character.Get()) ? UGratiaPlaySubsystem::GetSettings(Character.Get())->FoleyBanks.Num() : 0,
        Loops[0] != nullptr, Loops[1] != nullptr);
}
