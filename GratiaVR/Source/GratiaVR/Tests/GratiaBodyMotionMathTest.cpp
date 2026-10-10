#include "../GratiaBodyMotionMath.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGratiaBodyMotionMathTest, "Gratia.Math.BodyMotion",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FGratiaBodyMotionMathTest::RunTest(const FString& Parameters)
{
    using namespace GratiaBodyMotionMath;
    const float NaN = std::numeric_limits<float>::quiet_NaN();

    // Spring: same end state at 30 and 144 FPS (frame-rate independent), settles, rejects NaN.
    {
        FSpring A, B;
        for (int32 I = 0; I < 30; ++I) A.Step(1.0f, 2.0f, 0.7f, 1.0f / 30.0f);
        for (int32 I = 0; I < 144; ++I) B.Step(1.0f, 2.0f, 0.7f, 1.0f / 144.0f);
        TestTrue(TEXT("Spring is frame-rate independent"), FMath::Abs(A.X - B.X) < 0.01f);
        TestTrue(TEXT("Spring settles at the target"), FMath::Abs(A.X - 1.0f) < 0.02f);
        const float Before = A.X;
        A.Step(NaN, 2.0f, 0.7f, 0.016f);
        TestEqual(TEXT("NaN target is ignored"), A.X, Before);
        FSpring C;
        C.Step(1.0f, 2.0f, 0.7f, 10.0f);
        TestTrue(TEXT("A frame hitch is bounded"), FMath::IsFinite(C.X) && C.X <= 1.2f);
    }

    // Noise: bounded, smooth (no pops between 90 Hz frames), different seeds differ.
    {
        float MaxStep = 0.0f, Last = Noise(0.0f, 0.25f, 1.0f);
        bool bBounded = true, bDiffers = false;
        for (int32 I = 1; I < 90 * 60; ++I)
        {
            const float T = I / 90.0f, Value = Noise(T, 0.25f, 1.0f);
            bBounded &= Value >= -1.0f && Value <= 1.0f;
            MaxStep = FMath::Max(MaxStep, FMath::Abs(Value - Last));
            Last = Value;
            bDiffers |= FMath::Abs(Value - Noise(T, 0.25f, 2.0f)) > 0.05f;
        }
        TestTrue(TEXT("Noise stays in [-1, 1]"), bBounded);
        TestTrue(TEXT("Noise has no frame-to-frame pops"), MaxStep < 0.05f);
        TestTrue(TEXT("Seeds give independent tracks"), bDiffers);
        TestEqual(TEXT("NaN time gives zero"), Noise(NaN, 0.25f, 1.0f), 0.0f);
    }

    // Breathing: faster and deeper with excitement and fatigue; the phase never jumps when the rate changes.
    {
        const FBreathSettings S;
        TestTrue(TEXT("Excitement speeds breathing"), TargetBreathRate(1, 1, S) > TargetBreathRate(0, 1, S) * 2.0f);
        TestTrue(TEXT("Fatigue speeds breathing"), TargetBreathRate(0, 0, S) > TargetBreathRate(0, 1, S));
        TestTrue(TEXT("Fatigue deepens breathing"), TargetBreathDepth(0, 0, S) > TargetBreathDepth(0, 1, S));
        FBreathState State;
        float MaxCurveStep = 0.0f, LastCurve = BreathCurve(State.Phase);
        for (int32 I = 0; I < 90 * 20; ++I)
        {
            AdvanceBreath(State, I < 900 ? 0.0f : 1.0f, 1.0f, 1.0f / 90.0f, S);
            const float Curve = BreathCurve(State.Phase);
            MaxCurveStep = FMath::Max(MaxCurveStep, FMath::Abs(Curve - LastCurve));
            LastCurve = Curve;
        }
        TestTrue(TEXT("Breath curve stays continuous through a rate change"), MaxCurveStep < 0.1f);
        TestTrue(TEXT("Rate follows excitement"), State.PerMinute > 30.0f);
        TestEqual(TEXT("Breath curve starts at rest"), BreathCurve(0.0f), 0.0f);
        TestEqual(TEXT("Breath curve peaks after inhale"), BreathCurve(0.4f), 1.0f);
    }

    // Playhead modulation spans 0.5x..1.8x on a log scale and is safe on bad input.
    {
        TestEqual(TEXT("Slow caress plays at the minimum rate"), PlayRateForSpeed(2, 0.5f, 1.8f, 8, 80), 0.5f);
        TestEqual(TEXT("Fast caress plays at the maximum rate"), PlayRateForSpeed(500, 0.5f, 1.8f, 8, 80), 1.8f);
        const float Mid = PlayRateForSpeed(FMath::Sqrt(8.0f * 80.0f), 0.5f, 1.8f, 8, 80);
        TestTrue(TEXT("Geometric middle speed gives the middle rate"), FMath::Abs(Mid - 1.15f) < 0.01f);
        TestEqual(TEXT("NaN speed keeps normal speed"), PlayRateForSpeed(NaN, 0.5f, 1.8f, 8, 80), 1.0f);
        TestEqual(TEXT("Invalid range keeps normal speed"), PlayRateForSpeed(20, 0.5f, 1.8f, 80, 8), 1.0f);
    }

    // Dodge only for a close hand that is closing in.
    {
        TestEqual(TEXT("Far hand: no dodge"), DodgeWeight(100, 100, 25, 60, 60), 0.0f);
        TestEqual(TEXT("Receding hand: no dodge"), DodgeWeight(20, -50, 25, 60, 60), 0.0f);
        TestEqual(TEXT("Close fast hand: full dodge"), DodgeWeight(20, 100, 25, 60, 60), 1.0f);
        TestEqual(TEXT("NaN: no dodge"), DodgeWeight(NaN, 100, 25, 60, 60), 0.0f);
    }

    // Excitement and stamina stay in [0, 1], rise with stimulation, decay/recover without.
    {
        float E = 0.0f, Sta = 1.0f;
        for (int32 I = 0; I < 90 * 30; ++I)
        {
            E = UpdateExcitement(E, 1.0f, 0.2f, 0.05f, 1.0f / 90.0f);
            Sta = UpdateStamina(Sta, E, 0.05f, 0.03f, 1.0f / 90.0f);
        }
        TestTrue(TEXT("Stimulation raises excitement"), E > 0.9f && E <= 1.0f);
        TestTrue(TEXT("Sustained excitement drains stamina"), Sta < 0.5f && Sta >= 0.0f);
        for (int32 I = 0; I < 90 * 60; ++I)
        {
            E = UpdateExcitement(E, 0.0f, 0.2f, 0.05f, 1.0f / 90.0f);
            Sta = UpdateStamina(Sta, E, 0.05f, 0.03f, 1.0f / 90.0f);
        }
        TestTrue(TEXT("Excitement decays without stimulation"), E < 0.1f);
        TestTrue(TEXT("Stamina recovers at rest"), Sta > 0.6f);
        TestEqual(TEXT("NaN stimulus is treated as none"), UpdateExcitement(0.5f, NaN, 0.2f, 0.0f, 0.1f), 0.5f);
    }

    // Pace: the "right" caress speed counts fully, too slow or too fast counts less.
    {
        TestEqual(TEXT("Right pace"), PaceQuality(20, 10, 45), 1.0f);
        TestTrue(TEXT("Too fast counts less"), PaceQuality(200, 10, 45) < 0.5f);
        TestTrue(TEXT("Still hand counts a little"), PaceQuality(0, 10, 45) > 0.2f);
    }

    // Mocap cutting: cuts at the calmest samples, every fragment between MinGap and the limit.
    {
        TArray<float> Energy;
        for (int32 I = 0; I < 100; ++I) Energy.Add(I % 25 == 12 ? 0.0f : 1.0f);
        const TArray<int32> Cuts = FindCutSamples(Energy, 10, 30);
        TestTrue(TEXT("Cuts start and end at the clip bounds"), Cuts.Num() >= 3 && Cuts[0] == 0 && Cuts.Last() == 99);
        TestTrue(TEXT("Cuts land on calm samples"), Cuts.Num() >= 2 && Energy[Cuts[1]] == 0.0f);
        bool bSpacing = true;
        for (int32 I = 1; I < Cuts.Num(); ++I) bSpacing &= Cuts[I] - Cuts[I - 1] >= 10 && Cuts[I] - Cuts[I - 1] <= 40;
        TestTrue(TEXT("Fragments keep their length limits"), bSpacing);
        TestEqual(TEXT("Too short clip gives no fragments"), FindCutSamples(TArray<float>{1.0f}, 10, 30).Num(), 0);
    }

    // Pose distance.
    {
        const TArray<FVector> A = {FVector(0, 0, 0), FVector(10, 0, 0)}, B = {FVector(0, 0, 3), FVector(10, 4, 0)};
        TestEqual(TEXT("Mean point distance"), PoseDistance(A, B), 3.5f);
        TestEqual(TEXT("Mismatched sets never match"), PoseDistance(A, TArray<FVector>{FVector::ZeroVector}), FLT_MAX);
    }
    return true;
}
#endif
