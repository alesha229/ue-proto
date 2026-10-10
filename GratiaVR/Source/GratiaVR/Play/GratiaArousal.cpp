#include "GratiaArousal.h"
#include "GratiaPlaySettings.h"
#include "GratiaPlaySubsystem.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaInteraction.h"
#include "Components/SkeletalMeshComponent.h"
#if __has_include("GratiaBodyMotion.h")
#include "GratiaBodyMotion.h"
#define GRATIA_PLAY_HAS_BODY_MOTION 1
#else
#define GRATIA_PLAY_HAS_BODY_MOTION 0
#endif

DEFINE_LOG_CATEGORY_STATIC(LogGratiaArousal, Log, All);

UGratiaArousal::UGratiaArousal()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGratiaArousal::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    if (Character.IsValid() && Character->Interaction)
    {
        Character->Interaction->OnContactReaction.AddDynamic(this, &UGratiaArousal::HandleReaction);
        bBound = true;
    }
}

void UGratiaArousal::EndPlay(const EEndPlayReason::Type Reason)
{
    if (bBound && Character.IsValid() && Character->Interaction)
        Character->Interaction->OnContactReaction.RemoveDynamic(this, &UGratiaArousal::HandleReaction);
    bBound = false;
    Super::EndPlay(Reason);
}

void UGratiaArousal::HandleReaction(FName ZoneName, int32 HandIndex, float HandSpeed, int32 Mood)
{
    // Every answered reaction (hands, props, channels) nudges the meter a little.
    if (const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Character.Get()))
        PendingBonus += Settings->ReactionBonus;
}

void UGratiaArousal::Rebuild(const UGratiaPlaySettings& Settings)
{
    CachedSettings = &Settings;
    Stages.Reset();
    for (const FGratiaArousalStageDefinition& Definition : Settings.Stages)
    {
        GratiaPlay::FArousalStage Stage;
        Stage.Threshold = Definition.Threshold;
        Stage.PaceMin = Definition.PaceMin;
        Stage.PaceMax = Definition.PaceMax;
        Stage.PaceSoft = Definition.PaceSoft;
        Stage.RoughSpeed = Definition.RoughSpeed;
        Stages.Add(Stage);
    }
    State.Stage = FMath::Clamp(State.Stage, 0, FMath::Max(0, Stages.Num() - 1));
}

FName UGratiaArousal::GetStageName() const
{
    const UGratiaPlaySettings* Settings = CachedSettings.Get();
    return Settings && Settings->Stages.IsValidIndex(State.Stage) ? Settings->Stages[State.Stage].Name : NAME_None;
}

bool UGratiaArousal::IsUnlocked(FName Tag) const
{
    const UGratiaPlaySettings* Settings = CachedSettings.Get();
    return !Settings || Settings->IsUnlocked(State.Stage, Tag);
}

void UGratiaArousal::AddStimulus(FName Zone, float Speed, float Scale)
{
    const UGratiaPlaySettings* Settings = CachedSettings.Get();
    if (!Settings || !FMath::IsFinite(Speed) || !FMath::IsFinite(Scale) || Scale <= 0.0f) return;
    const float Weight = Settings->GetZoneWeight(State.Stage, Zone) * Scale;
    if (Weight > PendingStimulus) { PendingStimulus = Weight; PendingSpeed = Speed; LastZone = Zone; }
}

void UGratiaArousal::SetArousal(float Value)
{
    State.Value = FMath::Clamp(FMath::IsFinite(Value) ? Value : 0.0f, 0.0f, 1.0f);
    // Stages follow on the next tick (with the hysteresis of the normal path).
    if (State.Value <= 0.0f) { State = GratiaPlay::FArousalState(); LastPushed = 0.0f; }
}

void UGratiaArousal::PushExcitement(float Gain)
{
#if GRATIA_PLAY_HAS_BODY_MOTION
    if (Gain > 0.0f)
        if (UGratiaBodyMotion* Body = UGratiaBodyMotion::Find(GetOwner())) Body->AddExcitement(Gain);
#endif
}

void UGratiaArousal::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Character.Get());
    UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this);
    if (!bEnabled || !Settings || !Play || !Character.IsValid() || !UGratiaPlaySubsystem::IsLayerEnabled())
    {
        PendingStimulus = PendingBonus = 0.0f;
        return;
    }
    if (CachedSettings.Get() != Settings || Stages.Num() != Settings->Stages.Num()) Rebuild(*Settings);

    // The hand on the skin: nearest zone within reach, its speed.
    for (const bool bLeft : { true, false })
    {
        const UGratiaPlaySubsystem::FHand& Hand = Play->GetHand(bLeft);
        if (Hand.bAllowed && Hand.Zone != INDEX_NONE && Hand.ZoneGap <= 1.5f) AddStimulus(Hand.ZoneName, Hand.Speed, 1.0f);
    }

    GratiaPlay::FArousalTuning Tuning;
    Tuning.Gain = Settings->Gain;
    Tuning.Decay = Settings->Decay;
    Tuning.IdleDelay = Settings->IdleDelay;
    Tuning.RoughPenalty = Settings->RoughPenalty;
    const int32 Mood = Character->Interaction ? Character->Interaction->Mood : 0;
    Tuning.MoodGain = Settings->MoodGain.IsValidIndex(Mood) ? FMath::Max(0.0f, Settings->MoodGain[Mood]) : 1.0f;

    const float Before = State.Value;
    const int32 StageBefore = State.Stage;
    State.Value = FMath::Clamp(State.Value + PendingBonus, 0.0f, 1.0f);
    GratiaPlay::StepArousal(State, Stages, Tuning, PendingSpeed, PendingStimulus, Delta);
    Stimulus = GratiaPlay::Envelope(Stimulus, FMath::Clamp(PendingStimulus * GratiaPlay::PaceMatch(PendingSpeed,
        Stages.IsValidIndex(State.Stage) ? Stages[State.Stage].PaceMin : 0.0f, Stages.IsValidIndex(State.Stage) ? Stages[State.Stage].PaceMax : 100.0f,
        Stages.IsValidIndex(State.Stage) ? Stages[State.Stage].PaceSoft : 10.0f), 0.0f, 1.0f), 0.1f, 0.8f, Delta);
    PendingStimulus = PendingSpeed = PendingBonus = 0.0f;
    PushExcitement(State.Value - Before);

    if (State.Stage != StageBefore)
    {
        UE_LOG(LogGratiaArousal, Display, TEXT("AROUSAL stage %d -> %d (%s) value=%.2f"), StageBefore, State.Stage, *GetStageName().ToString(), State.Value);
        OnStageChanged.Broadcast(State.Stage, GetStageName());
    }
    if (!Settings->ArousalMaterialParameter.IsNone() && Character->CharacterMesh && FMath::Abs(State.Value - WrittenMaterial) > 0.01f)
    {
        Character->CharacterMesh->SetScalarParameterValueOnMaterials(Settings->ArousalMaterialParameter, State.Value);
        WrittenMaterial = State.Value;
    }
}

FString UGratiaArousal::GetDiagnostics() const
{
    return FString::Printf(TEXT("arousal=%.2f stage=%d(%s) satisfaction=%.2f rough=%.2f stimulus=%.2f zone=%s body_motion=%d"),
        State.Value, State.Stage, *GetStageName().ToString(), State.Satisfaction, State.Rough, Stimulus, *LastZone.ToString(), GRATIA_PLAY_HAS_BODY_MOTION);
}
