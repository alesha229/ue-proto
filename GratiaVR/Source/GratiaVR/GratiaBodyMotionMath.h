#pragma once
#include "CoreMinimal.h"

/** Pure math of the procedural body layer (breathing, idle noise, playhead rate, springs, mocap cutting).
 *  Frame-rate independent, NaN-safe; covered by Gratia.Math.BodyMotion. */
namespace GratiaBodyMotionMath
{
    inline float Saturate(float X) { return FMath::IsFinite(X) ? FMath::Clamp(X, 0.0f, 1.0f) : 0.0f; }

    /** Second-order spring toward a target; substepped at 240 Hz so it does not depend on the frame rate. */
    struct FSpring
    {
        float X = 0.0f, V = 0.0f;
        void Step(float Target, float FrequencyHz, float DampingRatio, float DeltaSeconds)
        {
            if (!FMath::IsFinite(Target) || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
            const float Omega = 2.0f * PI * FMath::Clamp(FrequencyHz, 0.05f, 30.0f);
            const float Zeta = FMath::Clamp(DampingRatio, 0.05f, 2.0f);
            const float Time = FMath::Min(DeltaSeconds, 0.1f);
            const int32 Steps = FMath::Max(1, FMath::CeilToInt(Time * 240.0f));
            const float Dt = Time / Steps;
            for (int32 I = 0; I < Steps; ++I)
            {
                // Semi-implicit Euler: velocity first, stable for these stiffnesses at 240 Hz.
                V += (Omega * Omega * (Target - X) - 2.0f * Zeta * Omega * V) * Dt;
                X += V * Dt;
            }
            if (!FMath::IsFinite(X) || !FMath::IsFinite(V)) { X = Target; V = 0.0f; }
        }
        void Reset(float Value = 0.0f) { X = Value; V = 0.0f; }
    };

    /** Exponential smoothing with a time constant (seconds); exact for any frame time. */
    inline float Smooth(float Current, float Target, float TimeConstant, float DeltaSeconds)
    {
        if (!FMath::IsFinite(Target)) return Current;
        if (!FMath::IsFinite(Current)) return Target;
        if (TimeConstant <= 1.0e-4f || !FMath::IsFinite(DeltaSeconds)) return Target;
        return Target + (Current - Target) * FMath::Exp(-FMath::Max(0.0f, DeltaSeconds) / TimeConstant);
    }

    /** Smooth low-frequency noise in [-1, 1]; each Seed gives an independent track. */
    inline float Noise(float Seconds, float FrequencyHz, float Seed)
    {
        if (!FMath::IsFinite(Seconds) || !FMath::IsFinite(FrequencyHz)) return 0.0f;
        // Wrap the time so float precision holds for long sessions (the track repeats after ~1000 cycles).
        const float Cycles = FMath::Fmod(Seconds * FrequencyHz, 1024.0f);
        return FMath::Clamp(FMath::PerlinNoise1D(Cycles + Seed * 37.137f), -1.0f, 1.0f);
    }

    /** Breath shape over one cycle (0..1): inhale over the first 40%, slower exhale, short rest. Returns 0..1. */
    inline float BreathCurve(float Phase)
    {
        if (!FMath::IsFinite(Phase)) return 0.0f;
        const float P = Phase - FMath::FloorToFloat(Phase);
        if (P < 0.4f) return FMath::SmoothStep(0.0f, 1.0f, P / 0.4f);
        if (P < 0.9f) { const float T = (P - 0.4f) / 0.5f; return 1.0f - FMath::SmoothStep(0.0f, 1.0f, T); }
        return 0.0f;
    }

    struct FBreathSettings
    {
        float RestPerMinute = 14.0f;
        float ExcitedPerMinute = 38.0f;
        /** Extra rate when out of stamina, as a fraction of the current rate. */
        float FatigueBoost = 0.35f;
        float RestDepth = 0.35f;
        float MaxDepth = 1.0f;
        /** Seconds for the rate to follow a change (the phase stays continuous). */
        float RateTimeConstant = 1.5f;
    };

    struct FBreathState
    {
        float Phase = 0.0f;
        float PerMinute = 14.0f;
        float Depth = 0.35f;
    };

    inline float TargetBreathRate(float Excitement, float Stamina, const FBreathSettings& S)
    {
        const float E = Saturate(Excitement), Fatigue = 1.0f - Saturate(Stamina);
        return FMath::Lerp(S.RestPerMinute, S.ExcitedPerMinute, E) * (1.0f + S.FatigueBoost * Fatigue);
    }

    inline float TargetBreathDepth(float Excitement, float Stamina, const FBreathSettings& S)
    {
        return FMath::Lerp(S.RestDepth, S.MaxDepth, FMath::Max(Saturate(Excitement), 1.0f - Saturate(Stamina)));
    }

    /** Advances the phase with a smoothly changing rate, so a faster breath never jumps in the cycle. */
    inline void AdvanceBreath(FBreathState& State, float Excitement, float Stamina, float DeltaSeconds, const FBreathSettings& S)
    {
        if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
        const float Dt = FMath::Min(DeltaSeconds, 0.1f);
        State.PerMinute = FMath::Clamp(Smooth(State.PerMinute, TargetBreathRate(Excitement, Stamina, S), S.RateTimeConstant, Dt), 4.0f, 90.0f);
        State.Depth = Saturate(Smooth(State.Depth, TargetBreathDepth(Excitement, Stamina, S), S.RateTimeConstant, Dt));
        State.Phase += State.PerMinute / 60.0f * Dt;
        State.Phase -= FMath::FloorToFloat(State.Phase);
        if (!FMath::IsFinite(State.Phase)) State.Phase = 0.0f;
    }

    /** Playhead rate from the player's hand speed: logarithmic between SlowSpeed (MinRate) and FastSpeed (MaxRate). */
    inline float PlayRateForSpeed(float SpeedCmPerSecond, float MinRate, float MaxRate, float SlowSpeed, float FastSpeed)
    {
        if (!FMath::IsFinite(SpeedCmPerSecond) || SlowSpeed <= 0.0f || FastSpeed <= SlowSpeed || MinRate <= 0.0f || MaxRate < MinRate) return 1.0f;
        const float Speed = FMath::Clamp(SpeedCmPerSecond, SlowSpeed, FastSpeed);
        const float T = FMath::Loge(Speed / SlowSpeed) / FMath::Loge(FastSpeed / SlowSpeed);
        return FMath::Lerp(MinRate, MaxRate, T);
    }

    /** How strongly to shy away from an approaching hand (0..1): close and closing in. */
    inline float DodgeWeight(float DistanceCm, float ClosingSpeedCmPerSecond, float NearCm, float FarCm, float FullClosingSpeed)
    {
        if (!FMath::IsFinite(DistanceCm) || !FMath::IsFinite(ClosingSpeedCmPerSecond) || FarCm <= NearCm || FullClosingSpeed <= 0.0f) return 0.0f;
        const float Proximity = 1.0f - FMath::SmoothStep(NearCm, FarCm, DistanceCm);
        return Proximity * Saturate(ClosingSpeedCmPerSecond / FullClosingSpeed);
    }

    /** "Right pace" of a caress: 1 inside [Low, High] cm/s, falling to 0.25 at 0 and at 3 x High. */
    inline float PaceQuality(float SpeedCmPerSecond, float Low, float High)
    {
        if (!FMath::IsFinite(SpeedCmPerSecond) || Low <= 0.0f || High <= Low) return 0.0f;
        const float S = FMath::Max(0.0f, SpeedCmPerSecond);
        if (S < Low) return FMath::Lerp(0.25f, 1.0f, S / Low);
        if (S <= High) return 1.0f;
        return FMath::Lerp(1.0f, 0.25f, Saturate((S - High) / (2.0f * High)));
    }

    /** Excitement rises with stimulation toward 1 (saturating) and decays toward zero without it. */
    inline float UpdateExcitement(float Excitement, float Stimulus, float GainPerSecond, float DecayPerSecond, float DeltaSeconds)
    {
        if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return Saturate(Excitement);
        const float E = Saturate(Excitement);
        const float Rise = Saturate(Stimulus) * FMath::Max(0.0f, GainPerSecond) * (1.0f - E);
        const float Fall = FMath::Max(0.0f, DecayPerSecond) * E * (1.0f - Saturate(Stimulus));
        return Saturate(E + (Rise - Fall) * FMath::Min(DeltaSeconds, 0.1f));
    }

    /** Stamina drains with effort (excitement x activity) and recovers at rest. */
    inline float UpdateStamina(float Stamina, float Effort, float DrainPerSecond, float RecoverPerSecond, float DeltaSeconds)
    {
        if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return Saturate(Stamina);
        const float E = Saturate(Effort);
        const float Delta = (RecoverPerSecond * (1.0f - E) - DrainPerSecond * E) * FMath::Min(DeltaSeconds, 0.1f);
        return Saturate(Saturate(Stamina) + Delta);
    }

    /** Pose distance between two feature sets (cm): mean distance of corresponding points. */
    inline float PoseDistance(const TArray<FVector>& A, const TArray<FVector>& B)
    {
        if (A.Num() != B.Num() || A.IsEmpty()) return FLT_MAX;
        double Sum = 0.0;
        for (int32 I = 0; I < A.Num(); ++I) Sum += FVector::Distance(A[I], B[I]);
        const float Result = float(Sum / A.Num());
        return FMath::IsFinite(Result) ? Result : FLT_MAX;
    }

    /** Cut points for slicing a mocap clip into short fragments: the calmest sample (lowest motion
     *  energy) in each window [Last + MinGap, Last + MaxGap]. Always includes the first and last sample. */
    inline TArray<int32> FindCutSamples(const TArray<float>& Energy, int32 MinGap, int32 MaxGap)
    {
        TArray<int32> Cuts;
        const int32 Count = Energy.Num();
        if (Count < 2 || MinGap < 1 || MaxGap < MinGap) return Cuts;
        Cuts.Add(0);
        int32 Last = 0;
        while (Count - 1 - Last > MaxGap)
        {
            int32 Best = Last + MinGap;
            float BestEnergy = FLT_MAX;
            for (int32 I = Last + MinGap; I <= FMath::Min(Last + MaxGap, Count - 1); ++I)
            {
                const float Value = FMath::IsFinite(Energy[I]) ? Energy[I] : FLT_MAX;
                if (Value < BestEnergy) { BestEnergy = Value; Best = I; }
            }
            // The remainder must still hold a whole fragment.
            if (Count - 1 - Best < MinGap) break;
            Cuts.Add(Best);
            Last = Best;
        }
        if (Count - 1 - Last >= MinGap || Cuts.Num() == 1) Cuts.Add(Count - 1);
        else Cuts.Last() = Count - 1;
        return Cuts;
    }
}
