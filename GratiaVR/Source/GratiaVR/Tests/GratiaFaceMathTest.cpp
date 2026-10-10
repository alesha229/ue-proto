#include "../GratiaFaceMath.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGratiaFaceMathTest, "Gratia.Math.Face",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FGratiaFaceMathTest::RunTest(const FString& Parameters)
{
    using namespace GratiaFaceMath;
    {
        // Underdamped: snaps past the target, then settles on it.
        FSpring Spring; float Peak = 0.0f, Time = 0.0f, Reached = -1.0f;
        for (int32 Frame = 0; Frame < 180; ++Frame)
        {
            Spring.Step(1.0f, 5.0f, 0.22f, 1.0f / 90.0f); Time += 1.0f / 90.0f;
            Peak = FMath::Max(Peak, Spring.Value);
            if (Reached < 0.0f && Spring.Value >= 0.9f) Reached = Time;
        }
        TestTrue(TEXT("Expression spring overshoots"), Peak > 1.3f);
        TestTrue(TEXT("Expression spring reaches 90% within 3-6 frames at 90 Hz"), Reached > 0.0f && Reached <= 6.0f / 90.0f);
        TestTrue(TEXT("Expression spring settles in 2 s"), FMath::Abs(Spring.Value - 1.0f) < 0.02f);
    }
    {
        FSpring Spring; float Peak = 0.0f;
        for (int32 Frame = 0; Frame < 300; ++Frame) Peak = FMath::Max(Peak, Spring.Step(1.0f, 3.0f, 1.0f, 1.0f / 90.0f));
        TestTrue(TEXT("Critical spring does not overshoot"), Peak <= 1.0001f && Spring.Value > 0.99f);
    }
    {
        FSpring Spring; Spring.Value = 0.5f;
        Spring.Step(std::numeric_limits<float>::quiet_NaN(), 5.0f, 0.2f, 0.01f);
        Spring.Step(1.0f, 5.0f, 0.2f, std::numeric_limits<float>::infinity());
        TestEqual(TEXT("Spring rejects NaN target and infinite time"), Spring.Value, 0.5f);
        Spring.Step(1.0f, 5.0f, 0.2f, 10.0f);
        TestTrue(TEXT("A long hitch stays finite"), FMath::IsFinite(Spring.Value) && FMath::IsFinite(Spring.Velocity));
    }
    TestEqual(TEXT("Blink starts open"), BlinkWeight(0.0f, 0.07f, 0.03f, 0.16f), 0.0f);
    TestEqual(TEXT("Blink holds closed"), BlinkWeight(0.08f, 0.07f, 0.03f, 0.16f), 1.0f);
    TestTrue(TEXT("Blink opens slower than it closes"), BlinkWeight(0.07f + 0.03f + 0.07f, 0.07f, 0.03f, 0.16f) > 0.2f);
    TestEqual(TEXT("Blink ends open"), BlinkWeight(0.3f, 0.07f, 0.03f, 0.16f), 0.0f);
    TestEqual(TEXT("Blink rejects NaN"), BlinkWeight(std::numeric_limits<float>::quiet_NaN(), 0.07f, 0.03f, 0.16f), 0.0f);
    {
        FRandomStream Random(7); EFixation Current = EFixation::LeftEye; bool bRepeat = false, bMouth = false;
        for (int32 I = 0; I < 200; ++I)
        {
            const EFixation Next = NextFixation(Current, 0.25f, Random);
            bRepeat |= Next == Current; bMouth |= Next == EFixation::Mouth; Current = Next;
        }
        TestFalse(TEXT("Fixations never repeat"), bRepeat);
        TestTrue(TEXT("Fixations visit the mouth"), bMouth);
    }
    {
        const FEyeMorphs Right = EyeMorphs(15.0f, -40.0f, 30.0f, 20.0f);
        TestTrue(TEXT("Eye morphs follow yaw and clamp pitch"),
            FMath::IsNearlyEqual(Right.Right, 0.5f) && Right.Left == 0.0f && Right.Down == 1.0f && Right.Up == 0.0f);
    }
    {
        float Shares[3];
        GazeShares(0.55f, 0.3f, 0.15f, true, true, Shares);
        TestTrue(TEXT("Gaze shares sum to one"), FMath::IsNearlyEqual(Shares[0] + Shares[1] + Shares[2], 1.0f));
        GazeShares(0.55f, 0.3f, 0.15f, false, false, Shares);
        TestTrue(TEXT("Missing neck and chest hand the turn to the head"), Shares[0] == 1.0f && Shares[1] == 0.0f && Shares[2] == 0.0f);
    }
    {
        // Snappy timing: 90% of a step within 3 frames at 90 Hz, overshoot, elastic settle.
        FSnapSettle Snap; int32 Reached = -1; float Peak = 0.0f, MinAfterPeak = 2.0f;
        for (int32 Frame = 0; Frame < 360; ++Frame)
        {
            const float Value = Snap.Step(1.0f, 0.028f, 0.35f, 2.4f, 0.22f, 1.0f / 90.0f);
            if (Reached < 0 && Value >= 0.9f) Reached = Frame + 1;
            Peak = FMath::Max(Peak, Value);
            if (Frame > 5) MinAfterPeak = FMath::Min(MinAfterPeak, Value);
        }
        TestTrue(TEXT("Snap reaches 90% within 3 frames"), Reached > 0 && Reached <= 3);
        TestTrue(TEXT("Snap overshoots by about the overshoot share"), Peak > 1.15f && Peak < 1.4f);
        TestTrue(TEXT("Snap wobbles back below the target"), MinAfterPeak < 0.95f);
        TestTrue(TEXT("Snap settles in 4 s"), FMath::Abs(Snap.Value - 1.0f) < 0.01f);
        Snap.Step(std::numeric_limits<float>::quiet_NaN(), 0.028f, 0.35f, 2.4f, 0.22f, 0.01f);
        TestTrue(TEXT("Snap stays finite on NaN"), FMath::IsFinite(Snap.Value));
    }
    {
        FRandomStream Random(3); bool bInRange = true;
        for (int32 I = 0; I < 100; ++I) { const float T = NextBlinkInterval(2.5f, 5.0f, 0.0f, Random); bInRange &= T >= 2.5f && T <= 5.0f; }
        TestTrue(TEXT("Blink interval stays in 2.5-5 s"), bInRange);
        TestTrue(TEXT("Excitement shortens blink intervals"), NextBlinkInterval(5.0f, 5.0f, 1.0f, Random) < 3.1f);
    }
    {
        float MaxStep = 0.0f, Low = 1.0f, High = -1.0f, Previous = Noise1D(0.0f, 9);
        for (int32 I = 1; I < 4000; ++I)
        {
            const float Value = Noise1D(I * 0.01f, 9);
            MaxStep = FMath::Max(MaxStep, FMath::Abs(Value - Previous)); Previous = Value;
            Low = FMath::Min(Low, Value); High = FMath::Max(High, Value);
        }
        TestTrue(TEXT("Noise is continuous"), MaxStep < 0.05f);
        TestTrue(TEXT("Noise spans both signs within [-1, 1]"), Low < -0.2f && High > 0.2f && Low >= -1.0f && High <= 1.0f);
        TestTrue(TEXT("Noise seeds differ"), !FMath::IsNearlyEqual(Noise1D(3.5f, 1), Noise1D(3.5f, 2)));
        TestEqual(TEXT("Noise rejects NaN"), Noise1D(std::numeric_limits<float>::quiet_NaN(), 1), 0.0f);
    }
    {
        float Vertical, Horizontal;
        SquashStretch(0.08f, Vertical, Horizontal);
        TestTrue(TEXT("Stretch preserves volume"), FMath::IsNearlyEqual(Vertical * Horizontal * Horizontal, 1.0f, 1.0e-4f) && Horizontal < 1.0f);
        SquashStretch(-0.06f, Vertical, Horizontal);
        TestTrue(TEXT("Squash widens"), Vertical < 1.0f && Horizontal > 1.0f);
        SquashStretch(std::numeric_limits<float>::infinity(), Vertical, Horizontal);
        TestTrue(TEXT("Squash rejects infinity"), Vertical == 1.0f && Horizontal == 1.0f);
    }
    {
        TestEqual(TEXT("Mouth stays centred from the front"), MouthAsymmetry(5.0f, 15.0f, 45.0f), 0.0f);
        TestTrue(TEXT("Mouth moves onto the cheek in 3/4 view"), MouthAsymmetry(-50.0f, 15.0f, 45.0f) > 0.99f);
        TestTrue(TEXT("Mouth fades back in profile"), MouthAsymmetry(120.0f, 15.0f, 45.0f) < 0.01f);
    }
    {
        TestEqual(TEXT("Slow hand: no reflex"), ReflexUrgency(10.0f, 50.0f, 30.0f, 120.0f), 0.0f);
        TestEqual(TEXT("Far hand: no reflex"), ReflexUrgency(60.0f, 400.0f, 30.0f, 120.0f), 0.0f);
        TestTrue(TEXT("Fast near hand: reflex"), ReflexUrgency(10.0f, 300.0f, 30.0f, 120.0f) > 0.5f);
        TestTrue(TEXT("Too close averts"), ShouldAvert(15.0f, 0.0f, 22.0f, 55.0f, 60.0f));
        TestTrue(TEXT("Fast approach averts"), ShouldAvert(45.0f, 90.0f, 22.0f, 55.0f, 60.0f));
        TestFalse(TEXT("Slow approach keeps contact"), ShouldAvert(45.0f, 20.0f, 22.0f, 55.0f, 60.0f));
        TestTrue(TEXT("Pupils dilate in the dark and with excitement"),
            PupilScale(0.0f, 0.0f, 1.3f, 0.8f, 0.35f) > PupilScale(1.0f, 0.0f, 1.3f, 0.8f, 0.35f)
            && PupilScale(0.5f, 1.0f, 1.3f, 0.8f, 0.35f) > PupilScale(0.5f, 0.0f, 1.3f, 0.8f, 0.35f));
    }
    {
        float Yaw = 5.0f, Pitch = -7.0f;
        LimitSaccade(1.0f, 0.0f, 2.5f, Yaw, Pitch);
        TestTrue(TEXT("Contact saccade stays within the cap"), FMath::Sqrt(FMath::Square(Yaw - 1.0f) + FMath::Square(Pitch)) <= 2.5f + 1.0e-3f);
        TestTrue(TEXT("Contact saccade keeps its direction"), Yaw > 1.0f && Pitch < 0.0f);
        Yaw = 1.5f; Pitch = 0.5f;
        LimitSaccade(1.0f, 0.0f, 2.5f, Yaw, Pitch);
        TestTrue(TEXT("Small saccade is unchanged"), FMath::IsNearlyEqual(Yaw, 1.5f) && FMath::IsNearlyEqual(Pitch, 0.5f));
        TestTrue(TEXT("Fluster rises fast and fades slowly"),
            StepFluster(0.0f, true, 0.15f, 1.2f, 0.15f) > 0.99f && StepFluster(1.0f, false, 0.15f, 1.2f, 0.6f) > 0.45f
            && StepFluster(1.0f, false, 0.15f, 1.2f, 1.3f) == 0.0f);
    }
    {
        float Yaw, Pitch;
        DirectionAngles(FVector(100.0, 100.0, 0.0), FVector::ForwardVector, FVector::UpVector, Yaw, Pitch);
        TestTrue(TEXT("Direction yaw is positive to the right"), FMath::IsNearlyEqual(Yaw, 45.0f, 0.01f) && FMath::IsNearlyEqual(Pitch, 0.0f, 0.01f));
        DirectionAngles(FVector(100.0, 0.0, -100.0), FVector::ForwardVector, FVector::UpVector, Yaw, Pitch);
        TestTrue(TEXT("Direction pitch is negative below"), FMath::IsNearlyEqual(Pitch, -45.0f, 0.01f));
    }
    return true;
}
#endif
