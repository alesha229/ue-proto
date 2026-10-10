#pragma once
#include "CoreMinimal.h"

/**
 * Model-independent math of the interaction layer (spec section 4/5): springs, two-bone IK, body pull,
 * pace/arousal progression, haptic patterns, garment progress and breathing rhythm.
 * Pure functions and small state structs only, covered by Gratia.Math.Play.
 */
namespace GratiaPlay
{
    inline float SafeDelta(float Delta) { return FMath::IsFinite(Delta) ? FMath::Clamp(Delta, 0.0f, 0.1f) : 0.0f; }

    /** Damped spring, frame-rate independent (substeps of at most 1/240 s). Zeta < 1 overshoots. */
    struct FSpring
    {
        float Value = 0.0f;
        float Velocity = 0.0f;
        void Reset(float To = 0.0f) { Value = To; Velocity = 0.0f; }
        void Step(float Target, float FrequencyHz, float Zeta, float Delta)
        {
            const float Dt = SafeDelta(Delta);
            if (Dt <= 0.0f || !FMath::IsFinite(Target)) return;
            const float Omega = 2.0f * PI * FMath::Max(0.01f, FrequencyHz);
            const int32 Steps = FMath::Max(1, FMath::CeilToInt(Dt * 240.0f));
            const float H = Dt / Steps;
            for (int32 I = 0; I < Steps; ++I)
            {
                Velocity += (Omega * Omega * (Target - Value) - 2.0f * FMath::Max(0.0f, Zeta) * Omega * Velocity) * H;
                Value += Velocity * H;
            }
            if (!FMath::IsFinite(Value) || !FMath::IsFinite(Velocity)) Reset(Target);
        }
    };

    /** Asymmetric exponential envelope: time constant Attack while rising, Release while falling. */
    inline float Envelope(float Current, float Target, float Attack, float Release, float Delta)
    {
        if (!FMath::IsFinite(Target)) return 0.0f;
        if (!FMath::IsFinite(Current)) return Target;
        const float Tau = Target > Current ? Attack : Release;
        if (Tau <= 0.0f) return Target;
        return Target + (Current - Target) * FMath::Exp(-SafeDelta(Delta) / Tau);
    }

    /** Physical-animation drive scale while a hand presses: 1 free, Softness at full press. */
    inline float DriveScale(float PressAmount, float Softness)
    {
        if (!FMath::IsFinite(PressAmount) || !FMath::IsFinite(Softness)) return 1.0f;
        return FMath::Lerp(1.0f, FMath::Clamp(Softness, 0.0f, 1.0f), FMath::Clamp(PressAmount, 0.0f, 1.0f));
    }

    /**
     * Analytic two-bone IK. Root stays; Joint/End are moved so End reaches Goal (clamped to the reachable shell).
     * The bend keeps the current bend plane; PoleHint is used when the limb is nearly straight. Bone lengths are kept.
     */
    inline bool SolveTwoBone(const FVector& Root, const FVector& Joint, const FVector& End, const FVector& Goal,
        const FVector& PoleHint, FVector& OutJoint, FVector& OutEnd, bool* bOutReached = nullptr)
    {
        const double A = FVector::Distance(Root, Joint), B = FVector::Distance(Joint, End);
        if (A < 1e-3 || B < 1e-3 || Root.ContainsNaN() || Joint.ContainsNaN() || End.ContainsNaN() || Goal.ContainsNaN()) return false;
        FVector ToGoal = Goal - Root;
        double D = ToGoal.Size();
        FVector Dir = D > 1e-4 ? ToGoal / D : (End - Root).GetSafeNormal();
        if (Dir.IsNearlyZero()) return false;
        if (bOutReached) *bOutReached = D <= A + B + 1e-3;
        D = FMath::Clamp(D, FMath::Abs(A - B) + 1e-3, A + B - 1e-3);
        // Bend direction: the current knee/elbow offset from the root-end line, or the hint when straight.
        const FVector Line = (End - Root).GetSafeNormal();
        FVector Bend = (Joint - Root) - Line * FVector::DotProduct(Joint - Root, Line);
        if (Bend.Size() < 0.5) Bend = PoleHint;
        Bend = Bend - Dir * FVector::DotProduct(Bend, Dir);
        if (!Bend.Normalize())
        {
            Bend = FVector::CrossProduct(Dir, FMath::Abs(Dir.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector);
            if (!Bend.Normalize()) return false;
        }
        const double CosA = FMath::Clamp((A * A + D * D - B * B) / (2.0 * A * D), -1.0, 1.0);
        const double SinA = FMath::Sqrt(FMath::Max(0.0, 1.0 - CosA * CosA));
        OutJoint = Root + Dir * (A * CosA) + Bend * (A * SinA);
        OutEnd = Root + Dir * D;
        return !OutJoint.ContainsNaN() && !OutEnd.ContainsNaN();
    }

    /** A limb pulled beyond its reach drags the body: the excess times Follow, at most MaxCm, toward the goal. */
    inline FVector BodyPull(const FVector& Root, const FVector& Goal, double Reach, float Follow, float MaxCm)
    {
        const FVector To = Goal - Root;
        const double D = To.Size();
        if (!FMath::IsFinite(D) || D <= Reach || D < 1e-3 || Follow <= 0.0f || MaxCm <= 0.0f) return FVector::ZeroVector;
        return To / D * FMath::Min((D - Reach) * Follow, double(MaxCm));
    }

    /** Lift that keeps a sphere (Radius) at PointZ above SurfaceZ; bounded by MaxLift. */
    inline double GroundLift(double PointZ, double SurfaceZ, double Radius, double MaxLift)
    {
        if (!FMath::IsFinite(PointZ) || !FMath::IsFinite(SurfaceZ)) return 0.0;
        return FMath::Clamp(SurfaceZ + Radius - PointZ, 0.0, FMath::Max(0.0, MaxLift));
    }

    // ---------------------------------------------------------------- arousal / mood progression

    /** 1 inside [Min, Max], a Gaussian falloff of width Soft outside. */
    inline float PaceMatch(float Speed, float Min, float Max, float Soft)
    {
        if (!FMath::IsFinite(Speed) || Speed < 0.0f) return 0.0f;
        if (Speed >= Min && Speed <= Max) return 1.0f;
        const float Out = Speed < Min ? Min - Speed : Speed - Max;
        return FMath::Exp(-FMath::Square(Out / FMath::Max(1.0f, Soft)));
    }

    struct FArousalStage
    {
        float Threshold = 0.0f;
        float PaceMin = 5.0f, PaceMax = 25.0f, PaceSoft = 12.0f;
        /** Faster than this (cm/s) is rough: the meter drops and the character protests. */
        float RoughSpeed = 90.0f;
    };

    struct FArousalTuning
    {
        /** Meter per second at a perfect pace on a weight-1 zone. */
        float Gain = 0.035f;
        /** Meter lost per second without stimulus (after IdleDelay). */
        float Decay = 0.012f;
        float IdleDelay = 4.0f;
        float RoughPenalty = 0.06f;
        /** A stage drops only below its threshold minus this. */
        float Hysteresis = 0.04f;
        /** Archetype multiplier of Gain (Mood 0/1/2). */
        float MoodGain = 1.0f;
    };

    struct FArousalState
    {
        float Value = 0.0f;
        int32 Stage = 0;
        /** 0..1 how rough the recent touch was (envelope). */
        float Rough = 0.0f;
        /** 0..1 how well the recent touch matched the wanted pace (envelope). */
        float Satisfaction = 0.0f;
        float SinceStimulus = 100.0f;
    };

    /** Stimulus: 0..1 weight of the touched zone/prop (0 = nothing touched); Speed in cm/s. */
    inline void StepArousal(FArousalState& S, const TArray<FArousalStage>& Stages, const FArousalTuning& T,
        float Speed, float Stimulus, float Delta)
    {
        const float Dt = SafeDelta(Delta);
        if (Dt <= 0.0f || Stages.IsEmpty()) return;
        S.Stage = FMath::Clamp(S.Stage, 0, Stages.Num() - 1);
        const FArousalStage& Want = Stages[S.Stage];
        const bool bTouch = FMath::IsFinite(Stimulus) && Stimulus > 0.0f;
        float Match = 0.0f, Rough = 0.0f;
        if (bTouch)
        {
            S.SinceStimulus = 0.0f;
            Match = PaceMatch(Speed, Want.PaceMin, Want.PaceMax, Want.PaceSoft);
            Rough = FMath::IsFinite(Speed) && Speed > Want.RoughSpeed ? FMath::Clamp((Speed - Want.RoughSpeed) / FMath::Max(1.0f, Want.RoughSpeed), 0.0f, 1.0f) : 0.0f;
            S.Value += (T.Gain * T.MoodGain * Match * FMath::Clamp(Stimulus, 0.0f, 2.0f) - T.RoughPenalty * Rough) * Dt;
        }
        else
        {
            S.SinceStimulus += Dt;
            if (S.SinceStimulus > T.IdleDelay) S.Value -= T.Decay * Dt;
        }
        S.Value = FMath::Clamp(FMath::IsFinite(S.Value) ? S.Value : 0.0f, 0.0f, 1.0f);
        S.Satisfaction = Envelope(S.Satisfaction, bTouch ? Match : 0.0f, 0.3f, 1.5f, Dt);
        S.Rough = Envelope(S.Rough, Rough, 0.1f, 1.0f, Dt);
        while (S.Stage + 1 < Stages.Num() && S.Value >= Stages[S.Stage + 1].Threshold) ++S.Stage;
        while (S.Stage > 0 && S.Value < Stages[S.Stage].Threshold - T.Hysteresis) --S.Stage;
    }

    inline float HeartRate(float Arousal, float RestBpm, float PeakBpm)
    {
        return FMath::Lerp(RestBpm, PeakBpm, FMath::Clamp(FMath::IsFinite(Arousal) ? Arousal : 0.0f, 0.0f, 1.0f));
    }

    // ---------------------------------------------------------------- haptics

    struct FHaptic
    {
        float Amplitude = 0.0f;
        float Frequency = 0.0f;
    };

    /** Lub-dub: a strong pulse at the beat and a weaker one shortly after (sine-shaped pulses). */
    inline float Heartbeat(double Time, float Bpm, float PulseSeconds = 0.07f)
    {
        if (!FMath::IsFinite(Time) || !FMath::IsFinite(Bpm)) return 0.0f;
        const double Beat = 60.0 / FMath::Clamp(Bpm, 30.0f, 220.0f);
        const double P = FMath::Fmod(FMath::Max(0.0, Time), Beat);
        const double Width = FMath::Max(0.01f, PulseSeconds);
        const double Second = FMath::Min(0.22, Beat * 0.35);
        auto Pulse = [P, Width](double At, double Gain) { const double X = (P - At) / Width; return X >= 0.0 && X < 1.0 ? Gain * FMath::Sin(PI * X) : 0.0; };
        return float(FMath::Max(Pulse(0.0, 1.0), Pulse(Second, 0.6)));
    }

    /** Skin slide: grows with tangential speed, grained by the distance travelled (skin texture). */
    inline FHaptic Slide(float Speed, double Travel, float Base, float Gain, float FullSpeed, float GrainCm)
    {
        FHaptic H;
        if (!FMath::IsFinite(Speed) || Speed <= 0.5f || !FMath::IsFinite(Travel)) return H;
        const float S = FMath::Clamp(Speed / FMath::Max(1.0f, FullSpeed), 0.0f, 1.0f);
        const float Grain = 0.5f + 0.5f * FMath::PerlinNoise1D(float(Travel / FMath::Max(0.1f, GrainCm)));
        H.Amplitude = FMath::Clamp((Base + Gain * S) * (0.7f + 0.3f * Grain), 0.0f, 1.0f);
        H.Frequency = FMath::Lerp(0.25f, 0.85f, S);
        return H;
    }

    /** Elastic resistance: amplitude and pitch rise with press depth. */
    inline FHaptic Press(float DepthCm, float Base, float Max, float FullDepthCm, float Exponent = 1.5f)
    {
        FHaptic H;
        if (!FMath::IsFinite(DepthCm) || DepthCm <= 0.0f) return H;
        const float T = FMath::Pow(FMath::Clamp(DepthCm / FMath::Max(0.1f, FullDepthCm), 0.0f, 1.0f), FMath::Max(0.1f, Exponent));
        H.Amplitude = FMath::Lerp(Base, Max, T);
        H.Frequency = FMath::Lerp(0.15f, 0.6f, T);
        return H;
    }

    /** Layers combine like independent events (1 - prod(1 - a)); frequency is the amplitude-weighted mean. */
    inline FHaptic Mix(TConstArrayView<FHaptic> Layers)
    {
        double Keep = 1.0, Weight = 0.0, Freq = 0.0;
        for (const FHaptic& L : Layers)
        {
            const double A = FMath::IsFinite(L.Amplitude) ? FMath::Clamp(L.Amplitude, 0.0f, 1.0f) : 0.0;
            if (A <= 0.0) continue;
            Keep *= 1.0 - A;
            Weight += A;
            Freq += A * (FMath::IsFinite(L.Frequency) ? FMath::Clamp(L.Frequency, 0.0f, 1.0f) : 0.0f);
        }
        FHaptic H;
        H.Amplitude = float(1.0 - Keep);
        H.Frequency = Weight > 0.0 ? float(Freq / Weight) : 0.0f;
        return H;
    }

    // ---------------------------------------------------------------- clothing

    struct FGarmentState
    {
        float Progress = 0.0f;
        bool bHeld = false;
        bool bComplete = false;
        /** Progress when the hand took hold. */
        float GrabProgress = 0.0f;
    };

    /**
     * Held: progress follows the drag (in units of the piece's travel), the fabric stays where the hand leaves it.
     * Released below SnapBack it springs home; at or above Complete it finishes and locks (the piece is undone).
     * A two-hand piece without the anchoring hand sticks at StuckLimit (zipper needs the fabric held).
     */
    inline void StepGarment(FGarmentState& S, bool bHeld, float Drag, bool bAnchored, bool bTwoHands,
        float SnapBack, float Complete, float StuckLimit, float Delta)
    {
        const float Dt = SafeDelta(Delta);
        if (S.bComplete) { S.Progress = 1.0f; S.bHeld = false; return; }
        if (bHeld && !S.bHeld) S.GrabProgress = S.Progress;
        S.bHeld = bHeld;
        if (bHeld && FMath::IsFinite(Drag))
        {
            float Wanted = FMath::Clamp(S.GrabProgress + Drag, 0.0f, 1.0f);
            if (bTwoHands && !bAnchored) Wanted = FMath::Min(Wanted, FMath::Max(S.GrabProgress, StuckLimit));
            S.Progress = Wanted;
            if (S.Progress >= Complete) { S.bComplete = true; S.Progress = 1.0f; S.bHeld = false; }
            return;
        }
        if (S.Progress < SnapBack) S.Progress = Envelope(S.Progress, 0.0f, 0.0f, 0.12f, Dt);
    }

    // ---------------------------------------------------------------- breathing / non-verbal voice

    /** Breaths per minute from arousal and exertion (0..1 each). */
    inline float BreathRate(float Arousal, float Exertion, float RestBpm = 14.0f, float PeakBpm = 40.0f)
    {
        const float A = FMath::Clamp(FMath::IsFinite(Arousal) ? Arousal : 0.0f, 0.0f, 1.0f);
        const float E = FMath::Clamp(FMath::IsFinite(Exertion) ? Exertion : 0.0f, 0.0f, 1.0f);
        return FMath::Lerp(RestBpm, PeakBpm, FMath::Clamp(0.75f * A * A + 0.5f * E, 0.0f, 1.0f));
    }

    enum class EBreathEvent : uint8 { None, Inhale, Exhale };

    struct FBreathState
    {
        /** 0..1 within the current cycle; inhale is [0, InhaleFraction). */
        float Phase = 0.0f;
        float CycleSeconds = 4.0f;
        float HoldSeconds = 0.0f;
        int32 Cycle = 0;
        bool bInhale = false;
        uint32 Seed = 0x9E3779B9u;
    };

    inline float Random01(uint32& Seed)
    {
        Seed = Seed * 1664525u + 1013904223u;
        return float(Seed >> 8) / float(1 << 24);
    }

    /**
     * Advances the rhythm. Irregularity (0..1) jitters every cycle's length (broken rhythm when aroused);
     * a hold (startle, held breath) freezes the rhythm and ends with an exhale.
     */
    inline EBreathEvent StepBreath(FBreathState& S, float RateBpm, float Irregularity, float Delta)
    {
        const float Dt = SafeDelta(Delta);
        if (Dt <= 0.0f) return EBreathEvent::None;
        if (S.HoldSeconds > 0.0f)
        {
            S.HoldSeconds -= Dt;
            if (S.HoldSeconds > 0.0f) return EBreathEvent::None;
            S.HoldSeconds = 0.0f;
            S.Phase = 0.42f;
            S.bInhale = false;
            return EBreathEvent::Exhale;
        }
        const float InhaleFraction = 0.42f;
        S.Phase += Dt / FMath::Max(0.5f, S.CycleSeconds);
        if (S.Phase >= 1.0f)
        {
            S.Phase = FMath::Fmod(S.Phase, 1.0f);
            ++S.Cycle;
            const float Jitter = 1.0f + FMath::Clamp(Irregularity, 0.0f, 1.0f) * (Random01(S.Seed) - 0.5f) * 0.8f;
            S.CycleSeconds = 60.0f / FMath::Clamp(FMath::IsFinite(RateBpm) ? RateBpm : 14.0f, 4.0f, 80.0f) * Jitter;
        }
        const bool bInhale = S.Phase < InhaleFraction;
        const EBreathEvent Event = bInhale != S.bInhale ? (bInhale ? EBreathEvent::Inhale : EBreathEvent::Exhale) : EBreathEvent::None;
        S.bInhale = bInhale;
        return Event;
    }

    /** Chest/breath signal for animation: rises during inhale, falls during exhale (0..1). */
    inline float BreathCurve(const FBreathState& S)
    {
        const float InhaleFraction = 0.42f;
        if (S.HoldSeconds > 0.0f) return 1.0f;
        return S.Phase < InhaleFraction ? FMath::Sin(0.5f * PI * S.Phase / InhaleFraction)
            : FMath::Cos(0.5f * PI * (S.Phase - InhaleFraction) / (1.0f - InhaleFraction));
    }

    enum class EVocal : uint8 { None, InhaleSoft, ExhaleSoft, InhaleDeep, Sigh, HeldRelease, Broken, NonVerbalSoft, NonVerbalStrong, EarWhisper };

    /** What an exhale sounds like: arousal, recent stimulus strength (0..1) and a random draw choose. */
    inline EVocal ChooseExhale(float Arousal, float Stimulus, bool bAfterHold, float Random)
    {
        const float A = FMath::Clamp(FMath::IsFinite(Arousal) ? Arousal : 0.0f, 0.0f, 1.0f);
        const float S = FMath::Clamp(FMath::IsFinite(Stimulus) ? Stimulus : 0.0f, 0.0f, 1.0f);
        if (bAfterHold) return EVocal::HeldRelease;
        if (A > 0.7f && S > 0.5f && Random < 0.25f + 0.5f * (A - 0.7f)) return EVocal::NonVerbalStrong;
        if (A > 0.4f && S > 0.25f && Random < 0.3f) return EVocal::NonVerbalSoft;
        if (A > 0.55f && Random < 0.35f) return EVocal::Broken;
        if (Random < 0.08f + 0.2f * A) return EVocal::Sigh;
        return EVocal::ExhaleSoft;
    }
}
