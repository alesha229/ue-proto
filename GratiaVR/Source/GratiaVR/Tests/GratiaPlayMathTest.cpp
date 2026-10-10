#include "../Play/GratiaPlayMath.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGratiaPlayMathTest, "Gratia.Math.Play",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FGratiaPlayMathTest::RunTest(const FString& Parameters)
{
    using namespace GratiaPlay;
    const double NaN = std::numeric_limits<double>::quiet_NaN();

    // Spring: same end state at 45 and 144 Hz; overshoots when underdamped.
    {
        FSpring A, B;
        for (int32 I = 0; I < 45; ++I) A.Step(1.0f, 3.0f, 1.0f, 1.0f / 45.0f);
        for (int32 I = 0; I < 144; ++I) B.Step(1.0f, 3.0f, 1.0f, 1.0f / 144.0f);
        TestTrue(TEXT("Spring settles"), FMath::IsNearlyEqual(A.Value, 1.0f, 0.01f));
        TestTrue(TEXT("Spring is frame-rate independent"), FMath::IsNearlyEqual(A.Value, B.Value, 0.005f));
        FSpring C; float Peak = 0.0f;
        for (int32 I = 0; I < 90; ++I) { C.Step(1.0f, 3.0f, 0.3f, 1.0f / 90.0f); Peak = FMath::Max(Peak, C.Value); }
        TestTrue(TEXT("Underdamped spring overshoots"), Peak > 1.05f);
        C.Step(float(NaN), 3.0f, 0.3f, 0.01f);
        TestTrue(TEXT("NaN target ignored"), FMath::IsFinite(C.Value));
    }

    // Envelope and drive.
    TestTrue(TEXT("Envelope rises fast"), Envelope(0.0f, 1.0f, 0.01f, 1.0f, 0.05f) > 0.99f);
    TestTrue(TEXT("Envelope falls slowly"), Envelope(1.0f, 0.0f, 0.01f, 1.0f, 0.05f) > 0.9f);
    TestEqual(TEXT("Free body keeps full drive"), DriveScale(0.0f, 0.3f), 1.0f);
    TestEqual(TEXT("Pressed body softens"), DriveScale(1.0f, 0.3f), 0.3f);

    // Two-bone IK keeps bone lengths, reaches reachable goals, clamps unreachable ones, keeps the bend side.
    {
        const FVector Root(0, 0, 0), Joint(30, 0, -5), End(55, 0, 0);
        FVector NewJoint, NewEnd; bool bReached = false;
        TestTrue(TEXT("IK solves"), SolveTwoBone(Root, Joint, End, FVector(20, 30, 10), FVector(0, 0, -1), NewJoint, NewEnd, &bReached));
        TestTrue(TEXT("IK reaches"), bReached && NewEnd.Equals(FVector(20, 30, 10), 0.01));
        TestTrue(TEXT("Upper length kept"), FMath::IsNearlyEqual(FVector::Distance(Root, NewJoint), FVector::Distance(Root, Joint), 0.01));
        TestTrue(TEXT("Lower length kept"), FMath::IsNearlyEqual(FVector::Distance(NewJoint, NewEnd), FVector::Distance(Joint, End), 0.01));
        TestTrue(TEXT("Bend side kept"), NewJoint.Z < 5.0);
        TestTrue(TEXT("Far goal clamps"), SolveTwoBone(Root, Joint, End, FVector(500, 0, 0), FVector(0, 0, -1), NewJoint, NewEnd, &bReached));
        TestFalse(TEXT("Far goal reported unreached"), bReached);
        TestTrue(TEXT("Far goal: limb straight toward it"), NewEnd.X > 55.0 && FMath::Abs(NewEnd.Y) < 0.01);
        TestFalse(TEXT("NaN goal rejected"), SolveTwoBone(Root, Joint, End, FVector(NaN, 0, 0), FVector::ZeroVector, NewJoint, NewEnd));
        TestTrue(TEXT("Straight limb uses the pole"), SolveTwoBone(FVector::ZeroVector, FVector(30, 0, 0), FVector(60, 0, 0), FVector(40, 0, 0), FVector(0, 0, 1), NewJoint, NewEnd) && NewJoint.Z > 1.0);
    }

    // Body pull and ground lift.
    TestTrue(TEXT("Within reach: no pull"), BodyPull(FVector::ZeroVector, FVector(50, 0, 0), 55.0, 0.5f, 10.0f).IsNearlyZero());
    TestTrue(TEXT("Over reach: pulled toward"), BodyPull(FVector::ZeroVector, FVector(65, 0, 0), 55.0, 0.5f, 10.0f).Equals(FVector(5, 0, 0), 0.001));
    TestTrue(TEXT("Pull is bounded"), BodyPull(FVector::ZeroVector, FVector(500, 0, 0), 55.0, 0.5f, 10.0f).Equals(FVector(10, 0, 0), 0.001));
    TestEqual(TEXT("Above the bed: no lift"), GroundLift(20.0, 10.0, 4.0, 25.0), 0.0);
    TestEqual(TEXT("Sunk into the bed: lifted"), GroundLift(8.0, 10.0, 4.0, 25.0), 6.0);
    TestEqual(TEXT("Lift bounded"), GroundLift(-50.0, 10.0, 4.0, 25.0), 25.0);

    // Arousal: the right pace fills the meter and climbs stages; rough touch drains; hysteresis on the way down.
    {
        TArray<FArousalStage> Stages;
        FArousalStage S0; S0.Threshold = 0.0f; S0.PaceMin = 3; S0.PaceMax = 15; S0.RoughSpeed = 70; Stages.Add(S0);
        FArousalStage S1; S1.Threshold = 0.25f; S1.PaceMin = 5; S1.PaceMax = 25; S1.RoughSpeed = 90; Stages.Add(S1);
        FArousalTuning Tuning;
        FArousalState Good, Fast;
        for (int32 I = 0; I < 900; ++I) { StepArousal(Good, Stages, Tuning, 10.0f, 1.0f, 0.1f); StepArousal(Fast, Stages, Tuning, 60.0f, 1.0f, 0.1f); }
        TestTrue(TEXT("Wanted pace climbs a stage"), Good.Stage == 1 && Good.Value > 0.25f);
        TestTrue(TEXT("Wrong pace fills slower"), Fast.Value < Good.Value * 0.5f);
        FArousalState Rough; Rough.Value = 0.5f;
        for (int32 I = 0; I < 50; ++I) StepArousal(Rough, Stages, Tuning, 200.0f, 1.0f, 0.1f);
        TestTrue(TEXT("Rough touch drains and is noticed"), Rough.Value < 0.5f && Rough.Rough > 0.5f);
        FArousalState Down; Down.Value = 0.23f; Down.Stage = 1; Down.SinceStimulus = 0.0f;
        StepArousal(Down, Stages, Tuning, 0.0f, 0.0f, 0.1f);
        TestEqual(TEXT("Hysteresis keeps the stage just under its threshold"), Down.Stage, 1);
        Down.Value = 0.1f; StepArousal(Down, Stages, Tuning, 0.0f, 0.0f, 0.1f);
        TestEqual(TEXT("Well below: stage drops"), Down.Stage, 0);
        FArousalState Bad;
        StepArousal(Bad, Stages, Tuning, float(NaN), 1.0f, 0.1f);
        TestTrue(TEXT("NaN speed is harmless"), FMath::IsFinite(Bad.Value));
    }

    // Haptics.
    TestTrue(TEXT("Heartbeat pulses at the beat"), Heartbeat(0.035, 60.0f) > 0.9f);
    TestTrue(TEXT("Heartbeat quiet between beats"), Heartbeat(0.6, 60.0f) == 0.0f);
    TestTrue(TEXT("Second, weaker pulse"), Heartbeat(0.21 + 0.035, 60.0f) > 0.3f && Heartbeat(0.21 + 0.035, 60.0f) < 0.9f);
    TestTrue(TEXT("Still hand: no slide"), Slide(0.0f, 10.0, 0.04f, 0.3f, 60.0f, 1.2f).Amplitude == 0.0f);
    TestTrue(TEXT("Faster slide is stronger"), Slide(50.0f, 10.0, 0.04f, 0.3f, 60.0f, 1.2f).Amplitude > Slide(5.0f, 10.0, 0.04f, 0.3f, 60.0f, 1.2f).Amplitude);
    TestTrue(TEXT("Deeper press is stronger"), Press(3.0f, 0.08f, 0.6f, 3.0f).Amplitude > Press(0.5f, 0.08f, 0.6f, 3.0f).Amplitude);
    {
        FHaptic A; A.Amplitude = 0.5f; A.Frequency = 0.2f;
        FHaptic B; B.Amplitude = 0.5f; B.Frequency = 0.8f;
        const FHaptic M = Mix({ A, B });
        TestTrue(TEXT("Mix stays within 0..1"), FMath::IsNearlyEqual(M.Amplitude, 0.75f, 0.001f));
        TestTrue(TEXT("Mix frequency is weighted"), FMath::IsNearlyEqual(M.Frequency, 0.5f, 0.001f));
    }

    // Garments: fabric stays where it is left, small pulls snap back, completion locks, a zipper needs the anchor.
    {
        FGarmentState S;
        StepGarment(S, true, 0.5f, false, false, 0.12f, 0.95f, 0.15f, 0.01f);
        StepGarment(S, false, 0.0f, false, false, 0.12f, 0.95f, 0.15f, 0.5f);
        TestTrue(TEXT("Left half way: stays"), FMath::IsNearlyEqual(S.Progress, 0.5f));
        StepGarment(S, true, 0.6f, false, false, 0.12f, 0.95f, 0.15f, 0.01f);
        TestTrue(TEXT("Completed piece locks"), S.bComplete && S.Progress == 1.0f);
        FGarmentState Small;
        StepGarment(Small, true, 0.05f, false, false, 0.12f, 0.95f, 0.15f, 0.01f);
        for (int32 I = 0; I < 60; ++I) StepGarment(Small, false, 0.0f, false, false, 0.12f, 0.95f, 0.15f, 0.02f);
        TestTrue(TEXT("Small pull snaps back"), Small.Progress < 0.01f);
        FGarmentState Zip;
        StepGarment(Zip, true, 0.8f, false, true, 0.12f, 0.95f, 0.15f, 0.01f);
        TestTrue(TEXT("Zipper sticks without the anchor"), FMath::IsNearlyEqual(Zip.Progress, 0.15f));
        StepGarment(Zip, true, 0.8f, true, true, 0.12f, 0.95f, 0.15f, 0.01f);
        TestTrue(TEXT("Zipper opens with the anchor"), Zip.Progress > 0.5f);
    }

    // Breathing: faster when aroused, inhale/exhale alternate, a hold ends with an exhale.
    {
        TestTrue(TEXT("Aroused breathing is faster"), BreathRate(1.0f, 0.0f) > BreathRate(0.0f, 0.0f) + 15.0f);
        FBreathState S;
        int32 Inhales = 0, Exhales = 0;
        for (int32 I = 0; I < 90 * 20; ++I)
        {
            const EBreathEvent E = StepBreath(S, 15.0f, 0.0f, 1.0f / 90.0f);
            Inhales += E == EBreathEvent::Inhale; Exhales += E == EBreathEvent::Exhale;
        }
        TestTrue(TEXT("About five breaths in 20 s at 15/min"), Inhales >= 4 && Inhales <= 6 && FMath::Abs(Inhales - Exhales) <= 1);
        S.HoldSeconds = 0.5f;
        EBreathEvent Last = EBreathEvent::None;
        for (int32 I = 0; I < 60 && Last == EBreathEvent::None; ++I) Last = StepBreath(S, 15.0f, 0.0f, 1.0f / 90.0f);
        TestTrue(TEXT("Held breath released with an exhale"), Last == EBreathEvent::Exhale);
        TestTrue(TEXT("After a hold: released exhale"), ChooseExhale(0.2f, 0.0f, true, 0.5f) == EVocal::HeldRelease);
        TestTrue(TEXT("Calm, no stimulus: plain exhale"), ChooseExhale(0.0f, 0.0f, false, 0.9f) == EVocal::ExhaleSoft);
        TestTrue(TEXT("Peak with strong stimulus: voiced"), ChooseExhale(0.95f, 1.0f, false, 0.1f) == EVocal::NonVerbalStrong);
    }
    return true;
}
#endif
