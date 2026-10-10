#include "GratiaHapticLayers.h"
#include "GratiaPlaySettings.h"
#include "GratiaPlaySubsystem.h"
#include "GratiaArousal.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaSoftBodyInteraction.h"
#include "Engine/World.h"

UGratiaHapticLayers::UGratiaHapticLayers()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGratiaHapticLayers::SetSustain(bool bLeft, FName Source, float Amplitude, float Frequency)
{
    TArray<FSustain>& List = Sustains[bLeft ? 0 : 1];
    FSustain* Found = List.FindByPredicate([Source](const FSustain& S) { return S.Source == Source; });
    if (!Found) { Found = &List.AddDefaulted_GetRef(); Found->Source = Source; }
    Found->Value.Amplitude = FMath::IsFinite(Amplitude) ? FMath::Clamp(Amplitude, 0.0f, 1.0f) : 0.0f;
    Found->Value.Frequency = FMath::IsFinite(Frequency) ? FMath::Clamp(Frequency, 0.0f, 1.0f) : 0.0f;
    Found->Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void UGratiaHapticLayers::Pulse(bool bLeft, float Amplitude, float Frequency, float Seconds)
{
    if (!FMath::IsFinite(Amplitude) || !FMath::IsFinite(Seconds) || Seconds <= 0.0f || Amplitude <= 0.0f) return;
    FPulse& Added = Pulses[bLeft ? 0 : 1].AddDefaulted_GetRef();
    Added.Value.Amplitude = FMath::Clamp(Amplitude, 0.0f, 1.0f);
    Added.Value.Frequency = FMath::Clamp(FMath::IsFinite(Frequency) ? Frequency : 0.5f, 0.0f, 1.0f);
    // At least one runtime haptic update (90 Hz) sees the pulse.
    Added.Remaining = FMath::Max(Seconds, 0.022f);
}

void UGratiaHapticLayers::Blend(bool bLeft, float& Amplitude, float& Frequency) const
{
    if (!bEnabled || !UGratiaPlaySubsystem::IsLayerEnabled()) return;
    const GratiaPlay::FHaptic Own = Layers[bLeft ? 0 : 1];
    if (Own.Amplitude <= 0.0f) return;
    GratiaPlay::FHaptic Base;
    Base.Amplitude = Amplitude;
    Base.Frequency = Frequency;
    const GratiaPlay::FHaptic Mixed = GratiaPlay::Mix({ Base, Own });
    Amplitude = Mixed.Amplitude;
    Frequency = Mixed.Frequency;
}

void UGratiaHapticLayers::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this);
    AGratiaPreviewCharacter* Character = Play ? Play->GetCharacter() : nullptr;
    const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Character);
    const UGratiaArousal* Arousal = Character ? Character->FindComponentByClass<UGratiaArousal>() : nullptr;
    const double Now = GetWorld()->GetTimeSeconds();
    const float Dt = GratiaPlay::SafeDelta(Delta);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const bool bLeft = Index == 0;
        Layers[Index] = GratiaPlay::FHaptic();
        TArray<GratiaPlay::FHaptic, TInlineAllocator<8>> Parts;
        Sustains[Index].RemoveAll([Now](const FSustain& S) { return Now - S.Time > 0.1; });
        for (FPulse& Pending : Pulses[Index]) { Parts.Add(Pending.Value); Pending.Remaining -= Dt; }
        Pulses[Index].RemoveAll([](const FPulse& P) { return P.Remaining <= 0.0f; });
        for (const FSustain& S : Sustains[Index]) Parts.Add(S.Value);
        if (!bEnabled || !Play || !Settings || !UGratiaPlaySubsystem::IsLayerEnabled()) { Pulses[Index].Reset(); continue; }
        const UGratiaPlaySubsystem::FHand& Hand = Play->GetHand(bLeft);
        if (!Hand.bAllowed) continue;
        const bool bTouching = Hand.Zone != INDEX_NONE && Hand.ZoneGap <= 1.0f;
        // Soft parts already vibrate by press depth in the runtime; the elastic layer is for the rest of the body.
        const bool bSoftPress = Character && Character->SoftBodyInteraction && Character->SoftBodyInteraction->GetDepthCm(bLeft) > 0.0f;
        if (bSlide && bTouching)
            Parts.Add(GratiaPlay::Slide(Hand.Speed, Hand.Travel, Settings->SlideBase, Settings->SlideGain, Settings->SlideFullSpeed, Settings->SlideGrainCm));
        if (bPress && bTouching && !bSoftPress && Hand.ZoneGap < 0.0f)
            Parts.Add(GratiaPlay::Press(-Hand.ZoneGap, Settings->PressBase, Settings->PressMax, Settings->PressFullDepthCm));
        if (bHeartbeat && Arousal)
        {
            const float Level = Arousal->GetArousal();
            const bool bHeartZone = bTouching && Settings->HeartZones.Contains(Hand.ZoneName);
            const bool bHoldsLimb = Play->GetHandUse(bLeft) == EGratiaHandUse::Limb;
            const bool bEverywhere = bTouching && Level >= Settings->HeartEverywhereArousal;
            if (bHeartZone || bHoldsLimb || bEverywhere)
            {
                GratiaPlay::FHaptic Beat;
                Beat.Amplitude = GratiaPlay::Heartbeat(Now, GratiaPlay::HeartRate(Level, Settings->HeartRestBpm, Settings->HeartPeakBpm))
                    * Settings->HeartGain * (bHeartZone || bHoldsLimb ? 1.0f : 0.5f);
                Beat.Frequency = 0.1f;
                Parts.Add(Beat);
            }
        }
        Layers[Index] = GratiaPlay::Mix(Parts);
    }
}
