#include "../GratiaStage1Runtime.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGratiaHandOffsetSmootherTest, "Gratia.Stage1.HandOffsetSmoothing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGratiaHandOffsetSmootherTest::RunTest(const FString& Parameters)
{
    const float Delta = 1.0f / 90.0f;
    const double StepCm = 100.0 * Delta; // 1 m/s
    auto At = [](double X, double Yaw = 0.0) { return FTransform(FRotator(0.0, Yaw, 0.0), FVector(X, 0.0, 100.0)); };

    // A free hand moving fast stays exactly on the controller (no trailing).
    {
        FGratiaHandOffsetSmoother Smoother;
        double Worst = 0.0;
        for (int32 Frame = 0; Frame < 60; ++Frame)
        {
            const FTransform Target = At(Frame * StepCm, Frame * 4.0);
            Worst = FMath::Max(Worst, FVector::Distance(Smoother.Update(Target, Target, Frame ? Target : At(0.0), Delta).GetLocation(), Target.GetLocation()));
        }
        TestTrue(FString::Printf(TEXT("Free hand at 1 m/s and 360 deg/s follows the controller (%.4f cm)"), Worst), Worst < 0.001);
        TestTrue(TEXT("Free hand is settled and may ride the controller"), Smoother.IsSettled());
    }

    // Pushing into a wall at x = 0: the contact offset grows with the controller motion and is
    // followed exactly, so the hand never sinks past the surface.
    {
        FGratiaHandOffsetSmoother Smoother;
        double Deepest = -100.0;
        FTransform Shown = At(-5.0);
        for (int32 Frame = 0; Frame < 30; ++Frame)
        {
            const FTransform Target = At(-5.0 + Frame * StepCm);
            const FTransform Desired = At(FMath::Min(Target.GetLocation().X, 0.0));
            Shown = Smoother.Update(Target, Desired, Shown, Delta);
            Deepest = FMath::Max(Deepest, Shown.GetLocation().X);
        }
        TestTrue(FString::Printf(TEXT("Hand pushed into a surface at 1 m/s stays on it (%.4f cm deep)"), Deepest), Deepest <= 0.001);
    }

    // Holding a grip: the hand stays on the body part while the controller moves away.
    {
        FGratiaHandOffsetSmoother Smoother;
        const FTransform Held = At(0.0, 30.0);
        double Worst = 0.0;
        FTransform Shown = Held;
        for (int32 Frame = 0; Frame < 30; ++Frame)
        {
            Shown = Smoother.Update(At(Frame * 0.6, Frame * 2.0), Held, Shown, Delta);
            Worst = FMath::Max(Worst, FVector::Distance(Shown.GetLocation(), Held.GetLocation()));
        }
        TestTrue(FString::Printf(TEXT("Held grip pose is kept exactly while the controller moves (%.4f cm)"), Worst), Worst < 0.001);
    }

    // A pop (contact shape switch, grip/cup/press on or off) with a still controller eases in.
    {
        FGratiaHandOffsetSmoother Smoother;
        const FTransform Target = At(0.0);
        FTransform Shown = Smoother.Update(Target, Target, Target, Delta);
        const FTransform Popped(FRotator(0.0, 30.0, 0.0), FVector(4.0, 0.0, 100.0));
        Shown = Smoother.Update(Target, Popped, Shown, Delta);
        const double FirstMove = Shown.GetLocation().X;
        const double FirstTurn = FMath::RadiansToDegrees(Shown.GetRotation().AngularDistance(Target.GetRotation()));
        TestTrue(FString::Printf(TEXT("A 4 cm pop moves the hand only partly in one frame (%.2f cm)"), FirstMove), FirstMove > 0.3 && FirstMove < 1.5);
        TestTrue(FString::Printf(TEXT("A 30 deg pop turns the hand only partly in one frame (%.1f deg)"), FirstTurn), FirstTurn > 2.0 && FirstTurn < 12.0);
        for (int32 Frame = 0; Frame < 18; ++Frame) Shown = Smoother.Update(Target, Popped, Shown, Delta);
        TestTrue(TEXT("After 0.2 s the hand has reached the new contact pose"),
            FVector::Distance(Shown.GetLocation(), Popped.GetLocation()) < 0.1
            && FMath::RadiansToDegrees(Shown.GetRotation().AngularDistance(Popped.GetRotation())) < 0.5);
        TestTrue(TEXT("Residual has settled"), Smoother.IsSettled());
    }

    // After a reset (tracking recovery) the hand continues from what was on screen.
    {
        FGratiaHandOffsetSmoother Smoother;
        Smoother.Update(At(0.0), At(0.0), At(0.0), Delta);
        Smoother.Reset();
        const FTransform Shown = Smoother.Update(At(0.0), At(0.0), At(10.0), Delta);
        TestTrue(FString::Printf(TEXT("Reset continues from the shown pose instead of jumping (%.2f cm)"), Shown.GetLocation().X),
            Shown.GetLocation().X > 6.0 && Shown.GetLocation().X < 10.0);
        Smoother.Reset();
        const double Far = Smoother.Update(At(0.0), At(0.0), At(40.0), Delta).GetLocation().X;
        TestTrue(FString::Printf(TEXT("A residual larger than the cap is clamped (%.2f cm)"), Far), Far <= 15.001);
    }

    // Invalid frame time never produces NaN.
    {
        FGratiaHandOffsetSmoother Smoother;
        Smoother.Update(At(0.0), At(0.0), At(0.0), Delta);
        const FTransform Out = Smoother.Update(At(0.0), At(5.0), At(0.0), std::numeric_limits<float>::quiet_NaN());
        TestFalse(TEXT("NaN delta keeps the pose finite"), Out.ContainsNaN());
    }
    return true;
}
#endif
