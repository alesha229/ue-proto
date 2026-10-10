#pragma once
#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/** Model-independent math of the procedural face: springs, blink timing, fixations, eye morphs, noise, squash. */
namespace GratiaFaceMath
{
/** Damped spring x'' = w^2 (target - x) - 2 zeta w x', w = 2 pi FrequencyHz. Damping below 1 overshoots
 *  the target and rings down (a snap followed by an elastic settle); 1 is critical, no overshoot.
 *  Semi-implicit substeps of at most 1/240 s keep it stable up to ~60 Hz. */
struct FSpring
{
    float Value = 0.0f;
    float Velocity = 0.0f;
    void Reset(float To = 0.0f) { Value = FMath::IsFinite(To) ? To : 0.0f; Velocity = 0.0f; }
    /** Adds an instantaneous velocity change (an inertial kick, e.g. from head acceleration). */
    void Kick(float DeltaVelocity) { if (FMath::IsFinite(DeltaVelocity)) Velocity += DeltaVelocity; }
    float Step(float Target, float FrequencyHz, float Damping, float DeltaSeconds)
    {
        if (!FMath::IsFinite(Target) || !FMath::IsFinite(FrequencyHz) || !FMath::IsFinite(Damping)
            || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return Value;
        const float Omega = 2.0f * PI * FMath::Clamp(FrequencyHz, 0.0f, 60.0f);
        const float Zeta = FMath::Max(0.0f, Damping);
        const float Time = FMath::Min(DeltaSeconds, 0.1f);
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(Time * 240.0f));
        const float Dt = Time / Steps;
        for (int32 I = 0; I < Steps; ++I)
        {
            Velocity += (Omega * Omega * (Target - Value) - 2.0f * Zeta * Omega * Velocity) * Dt;
            Value += Velocity * Dt;
        }
        if (!FMath::IsFinite(Value) || !FMath::IsFinite(Velocity)) Reset(Target);
        return Value;
    }
};

/** Snappy timing: the value reaches a new target within SnapSeconds (2-3 frames), overshoots it by
 *  Overshoot of the jump and then wobbles back on a slow underdamped spring (jelly settle).
 *  Output = Fast + Overshoot * (Fast - Slow): Fast is a critically damped follower, Slow an underdamped one. */
struct FSnapSettle
{
    FSpring Fast, Slow;
    float Value = 0.0f;
    void Reset(float To = 0.0f) { Fast.Reset(To); Slow.Reset(To); Value = Fast.Value; }
    float Step(float Target, float SnapSeconds, float Overshoot, float SettleHz, float SettleDamping, float DeltaSeconds)
    {
        // Critical spring: 90% of a step after omega t ~ 3.9.
        const float SnapHz = 3.9f / (2.0f * PI * FMath::Max(SnapSeconds, 0.005f));
        Fast.Step(Target, SnapHz, 1.0f, DeltaSeconds);
        Slow.Step(Fast.Value, SettleHz, SettleDamping, DeltaSeconds);
        Value = Fast.Value + FMath::Max(0.0f, Overshoot) * (Fast.Value - Slow.Value);
        if (!FMath::IsFinite(Value)) Reset(FMath::IsFinite(Target) ? Target : 0.0f);
        return Value;
    }
};

/** Lid closure 0..1 of one blink Elapsed seconds after it began: accelerating close, hold, decelerating open. */
inline float BlinkWeight(float Elapsed, float CloseSeconds, float HoldSeconds, float OpenSeconds)
{
    if (!FMath::IsFinite(Elapsed) || Elapsed <= 0.0f) return 0.0f;
    const float Close = FMath::Max(CloseSeconds, 0.001f), Hold = FMath::Max(HoldSeconds, 0.0f), Open = FMath::Max(OpenSeconds, 0.001f);
    if (Elapsed < Close) return FMath::Square(Elapsed / Close);
    if (Elapsed < Close + Hold) return 1.0f;
    const float Opening = (Elapsed - Close - Hold) / Open;
    return Opening >= 1.0f ? 0.0f : FMath::Square(1.0f - Opening);
}

/** Seconds until the next spontaneous blink: uniform in [Min, Max], shortened by excitement (up to 40%). */
inline float NextBlinkInterval(float MinSeconds, float MaxSeconds, float Excitement, FRandomStream& Random)
{
    const float Low = FMath::Max(0.2f, FMath::Min(MinSeconds, MaxSeconds)), High = FMath::Max(Low, FMath::Max(MinSeconds, MaxSeconds));
    const float Scale = 1.0f - 0.4f * FMath::Clamp(FMath::IsFinite(Excitement) ? Excitement : 0.0f, 0.0f, 1.0f);
    return Random.FRandRange(Low, High) * Scale;
}

/** Fixation points on the player's face. */
enum class EFixation : uint8 { LeftEye, RightEye, Mouth };

/** The next fixation: never the same point twice in a row; from an eye, the mouth with MouthShare, else the other eye. */
inline EFixation NextFixation(EFixation Current, float MouthShare, FRandomStream& Random)
{
    if (Current == EFixation::Mouth) return Random.FRand() < 0.5f ? EFixation::LeftEye : EFixation::RightEye;
    if (Random.FRand() < FMath::Clamp(MouthShare, 0.0f, 1.0f)) return EFixation::Mouth;
    return Current == EFixation::LeftEye ? EFixation::RightEye : EFixation::LeftEye;
}

/** Fixation point in the viewer's camera frame (X forward, Y right, Z up), cm. */
inline FVector FixationOffset(EFixation Fixation, float EyeSpacingCm, float MouthDropCm)
{
    switch (Fixation)
    {
    case EFixation::LeftEye: return FVector(0.0, -0.5 * EyeSpacingCm, 0.0);
    case EFixation::RightEye: return FVector(0.0, 0.5 * EyeSpacingCm, 0.0);
    default: return FVector(0.0, 0.0, -MouthDropCm);
    }
}

struct FEyeMorphs
{
    float Left = 0.0f, Right = 0.0f, Up = 0.0f, Down = 0.0f;
};

/** Look morph weights for an eye direction relative to the head (yaw + to the character's right, pitch + up). */
inline FEyeMorphs EyeMorphs(float YawDegrees, float PitchDegrees, float FullYawDegrees, float FullPitchDegrees)
{
    FEyeMorphs Result;
    if (!FMath::IsFinite(YawDegrees) || !FMath::IsFinite(PitchDegrees)) return Result;
    const float Yaw = YawDegrees / FMath::Max(1.0f, FullYawDegrees), Pitch = PitchDegrees / FMath::Max(1.0f, FullPitchDegrees);
    Result.Right = FMath::Clamp(Yaw, 0.0f, 1.0f);
    Result.Left = FMath::Clamp(-Yaw, 0.0f, 1.0f);
    Result.Up = FMath::Clamp(Pitch, 0.0f, 1.0f);
    Result.Down = FMath::Clamp(-Pitch, 0.0f, 1.0f);
    return Result;
}

/** Splits a gaze turn over head, neck and chest (sums to 1). A missing bone hands its share to the head. */
inline void GazeShares(float Head, float Neck, float Chest, bool bHasNeck, bool bHasChest, float Out[3])
{
    Head = FMath::IsFinite(Head) ? FMath::Max(0.0f, Head) : 0.0f;
    Neck = bHasNeck && FMath::IsFinite(Neck) ? FMath::Max(0.0f, Neck) : 0.0f;
    Chest = bHasChest && FMath::IsFinite(Chest) ? FMath::Max(0.0f, Chest) : 0.0f;
    const float Sum = Head + Neck + Chest;
    if (Sum <= UE_SMALL_NUMBER) { Out[0] = 1.0f; Out[1] = Out[2] = 0.0f; return; }
    Out[0] = Head / Sum; Out[1] = Neck / Sum; Out[2] = Chest / Sum;
}

/** Yaw (+ towards the frame's right, Up x Forward) and pitch (+ up) in degrees of a direction in a forward/up frame. */
inline void DirectionAngles(const FVector& Direction, const FVector& Forward, const FVector& Up, float& YawDegrees, float& PitchDegrees)
{
    YawDegrees = PitchDegrees = 0.0f;
    const FVector F = Forward.GetSafeNormal(), U = Up.GetSafeNormal();
    const FVector R = FVector::CrossProduct(U, F).GetSafeNormal();
    if (Direction.ContainsNaN() || F.IsNearlyZero() || R.IsNearlyZero()) return;
    const double Ahead = FVector::DotProduct(Direction, F), Side = FVector::DotProduct(Direction, R), Height = FVector::DotProduct(Direction, U);
    YawDegrees = float(FMath::RadiansToDegrees(FMath::Atan2(Side, Ahead)));
    PitchDegrees = float(FMath::RadiansToDegrees(FMath::Atan2(Height, FMath::Sqrt(Ahead * Ahead + Side * Side))));
}

/** Smooth 1D gradient (Perlin) noise in about [-1, 1]; continuous in Time, different per Seed. */
inline float Noise1D(float Time, int32 Seed)
{
    if (!FMath::IsFinite(Time)) return 0.0f;
    auto Gradient = [Seed](int32 Cell)
    {
        uint32 H = uint32(Cell) * 0x27d4eb2dU ^ uint32(Seed) * 0x165667b1U;
        H ^= H >> 15; H *= 0x85ebca6bU; H ^= H >> 13;
        return (H & 0xFFFF) / 32767.5f - 1.0f;
    };
    // Wrap far times so float precision stays good during long sessions.
    const float T = FMath::Fmod(Time, 4096.0f);
    const int32 Cell = FMath::FloorToInt(T);
    const float F = T - Cell;
    const float A = Gradient(Cell) * F, B = Gradient(Cell + 1) * (F - 1.0f);
    const float S = F * F * F * (F * (F * 6.0f - 15.0f) + 10.0f);
    return FMath::Clamp(2.0f * FMath::Lerp(A, B, S), -1.0f, 1.0f);
}

/** Two-octave noise for blendshape micro-jitter (slow drift + faint detail), about [-1, 1]. */
inline float Jitter(float Time, float FrequencyHz, int32 Seed)
{
    return 0.75f * Noise1D(Time * FrequencyHz, Seed) + 0.25f * Noise1D(Time * FrequencyHz * 2.7f, Seed + 101);
}

/** Volume-preserving head scale for a squash (Amount < 0) or stretch (Amount > 0): vertical 1 + Amount,
 *  both horizontal axes 1 / sqrt(1 + Amount). */
inline void SquashStretch(float Amount, float& Vertical, float& Horizontal)
{
    const float A = FMath::Clamp(FMath::IsFinite(Amount) ? Amount : 0.0f, -0.4f, 0.4f);
    Vertical = 1.0f + A;
    Horizontal = 1.0f / FMath::Sqrt(Vertical);
}

/** 0..1 how strongly the mouth is drawn onto the cheek for a viewer YawDegrees around the head
 *  (0 = from the front). Starts at StartDegrees, full at FullDegrees, fades back out towards a profile view. */
inline float MouthAsymmetry(float YawDegrees, float StartDegrees, float FullDegrees)
{
    if (!FMath::IsFinite(YawDegrees)) return 0.0f;
    const float A = FMath::Abs(YawDegrees);
    const float In = FMath::SmoothStep(StartDegrees, FMath::Max(StartDegrees + 1.0f, FullDegrees), A);
    const float Out = 1.0f - FMath::SmoothStep(80.0f, 110.0f, A);
    return In * Out;
}

/** Reflex squint: a hand moving at least MinSpeed within Radius of the face; returns 0..1 urgency. */
inline float ReflexUrgency(float DistanceCm, float SpeedCmPerSecond, float RadiusCm, float MinSpeedCmPerSecond)
{
    if (!FMath::IsFinite(DistanceCm) || !FMath::IsFinite(SpeedCmPerSecond) || DistanceCm > RadiusCm || SpeedCmPerSecond < MinSpeedCmPerSecond) return 0.0f;
    const float Near = 1.0f - FMath::Clamp(DistanceCm / FMath::Max(1.0f, RadiusCm), 0.0f, 1.0f);
    const float Fast = FMath::Clamp(SpeedCmPerSecond / FMath::Max(1.0f, 2.0f * MinSpeedCmPerSecond), 0.0f, 1.0f);
    return FMath::Clamp(0.4f + 0.6f * FMath::Max(Near, Fast), 0.0f, 1.0f);
}

/** Gaze aversion trigger: the viewer's face is closer than CloseCm, or approaches faster than the threshold within RangeCm. */
inline bool ShouldAvert(float DistanceCm, float ApproachCmPerSecond, float CloseCm, float RangeCm, float ApproachThresholdCmPerSecond)
{
    if (!FMath::IsFinite(DistanceCm) || !FMath::IsFinite(ApproachCmPerSecond)) return false;
    return DistanceCm < CloseCm || (DistanceCm < RangeCm && ApproachCmPerSecond > ApproachThresholdCmPerSecond);
}

/** Pupil size factor: dilates in the dark and with excitement, contracts in bright light. Light and excitement 0..1. */
inline float PupilScale(float LightLevel, float Excitement, float DarkScale, float BrightScale, float ExcitementGain)
{
    const float L = FMath::Clamp(FMath::IsFinite(LightLevel) ? LightLevel : 0.5f, 0.0f, 1.0f);
    const float E = FMath::Clamp(FMath::IsFinite(Excitement) ? Excitement : 0.0f, 0.0f, 1.0f);
    return FMath::Clamp(FMath::Lerp(DarkScale, BrightScale, L) + ExcitementGain * E, 0.3f, 2.5f);
}
}
