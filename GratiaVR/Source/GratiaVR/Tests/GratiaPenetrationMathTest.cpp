#include "../GratiaPenetrationMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGratiaPenetrationMathTest, "Gratia.Penetration.Math",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGratiaPenetrationMathTest::RunTest(const FString& Parameters)
{
    using namespace GratiaPenetration;
    // Channel along +X from the origin, 14 cm deep.
    FPath Channel;
    Channel.Add(FVector::ZeroVector);
    Channel.Add(FVector(14, 0, 0));
    const FVector Inward = FVector::XAxisVector;
    FShaft Shaft;
    Shaft.Length = 18.0; Shaft.Radius = 2.0; Shaft.TipCm = 2.5; Shaft.BaseScale = 1.0; Shaft.Joints = 8;

    // Radius profile: zero at the tip, full past the taper, zero beyond the base.
    TestEqual(TEXT("Tip has no radius"), Shaft.RadiusAt(0.0), 0.0);
    TestTrue(TEXT("Taper rises toward the body"), Shaft.RadiusAt(0.5) > 0.0 && Shaft.RadiusAt(0.5) < Shaft.RadiusAt(2.0));
    TestEqual(TEXT("Body radius past the taper"), Shaft.RadiusAt(5.0), 2.0, 1.0e-9);
    TestEqual(TEXT("Nothing beyond the base"), Shaft.RadiusAt(18.5), 0.0);

    // Capture: aligned tip at the entrance enters; sideways, from inside or from far away it does not.
    TestTrue(TEXT("Aligned tip 1 cm before the entrance is captured"), CanCapture(Shaft, FVector(-19, 0, 0), Inward, FVector::ZeroVector, Inward, 3.0, 50.0));
    TestTrue(TEXT("Tip 2 cm off-axis within the radius is captured"), CanCapture(Shaft, FVector(-18, 2, 0), Inward, FVector::ZeroVector, Inward, 3.0, 50.0));
    TestFalse(TEXT("Sideways shaft is not captured"), CanCapture(Shaft, FVector(0, -19, 0), FVector::YAxisVector, FVector::ZeroVector, Inward, 3.0, 50.0));
    TestFalse(TEXT("Tip 6 cm before the entrance is not captured"), CanCapture(Shaft, FVector(-24, 0, 0), Inward, FVector::ZeroVector, Inward, 3.0, 50.0));
    TestFalse(TEXT("Base inside the body is not captured"), CanCapture(Shaft, FVector(1, 0, 0), Inward, FVector::ZeroVector, Inward, 30.0, 50.0));
    const FVector Tilted = FRotator(0, 40, 0).RotateVector(Inward);
    TestTrue(TEXT("40 degree approach is captured"), CanCapture(Shaft, -Tilted * 18.5, Tilted, FVector::ZeroVector, Inward, 3.0, 50.0));
    const FVector Steep = FRotator(0, 60, 0).RotateVector(Inward);
    TestFalse(TEXT("60 degree approach is not captured"), CanCapture(Shaft, -Steep * 18.5, Steep, FVector::ZeroVector, Inward, 3.0, 50.0));

    // Straight insertion: inserted length equals the base advance; the joints keep the shaft length.
    TArray<FVector> Joints;
    double WorstDepth = 0.0, WorstLength = 0.0, WorstTip = 0.0;
    for (int32 Step = 0; Step <= 14; ++Step)
    {
        const double Inserted = EngagedJoints(Shaft, FVector(Step - 18.0, 0, 0), Inward, Channel, Channel.Length(), Joints);
        WorstDepth = FMath::Max(WorstDepth, FMath::Abs(Inserted - Step));
        double Length = 0.0;
        for (int32 Joint = 1; Joint < Joints.Num(); ++Joint) Length += FVector::Distance(Joints[Joint - 1], Joints[Joint]);
        WorstLength = FMath::Max(WorstLength, FMath::Abs(Length - Shaft.Length));
        WorstTip = FMath::Max(WorstTip, FVector::Distance(Joints.Last(), FVector(Step, 0, 0)));
    }
    TestTrue(FString::Printf(TEXT("Inserted depth follows the base (%.4f cm)"), WorstDepth), WorstDepth < 0.01);
    TestTrue(FString::Printf(TEXT("Shaft keeps its length (%.4f cm)"), WorstLength), WorstLength < 0.05);
    TestTrue(FString::Printf(TEXT("Tip lies on the channel at the depth (%.4f cm)"), WorstTip), WorstTip < 0.05);
    TestEqual(TEXT("Joint count is kept"), Joints.Num(), Shaft.Joints);

    // Bent approach (hand 45 degrees off the channel): the inside part lies on the channel axis.
    {
        const FVector Base = FVector(-8, -8, 0);
        const double Inserted = EngagedJoints(Shaft, Base, FVector(1, 1, 0).GetSafeNormal(), Channel, Channel.Length(), Joints);
        TestTrue(TEXT("Bent shaft is inserted"), Inserted > 2.0 && Inserted < Shaft.Length);
        double OffAxis = 0.0;
        for (const FVector& Joint : Joints) if (Joint.X > 0.5) OffAxis = FMath::Max(OffAxis, FMath::Abs(double(Joint.Y)) + FMath::Abs(double(Joint.Z)));
        TestTrue(FString::Printf(TEXT("Inside joints follow the channel (%.4f cm off axis)"), OffAxis), OffAxis < 0.05);
        TestTrue(TEXT("Base joint stays at the hand"), Joints[0].Equals(Base, 1.0e-6));
    }

    // Past the channel end the shaft compresses instead of leaving the channel.
    {
        const double Inserted = EngagedJoints(Shaft, FVector(-1, 0, 0), Inward, Channel, Channel.Length(), Joints);
        TestTrue(TEXT("Over-insertion is reported"), Inserted > Channel.Length());
        TestTrue(TEXT("Tip stops at the channel end"), FVector::Distance(Joints.Last(), FVector(14, 0, 0)) < 0.01);
    }

    // Pulled out: negative insertion, the tip stays before the entrance.
    {
        const double Inserted = EngagedJoints(Shaft, FVector(-23, 0, 0), Inward, Channel, Channel.Length(), Joints);
        TestTrue(TEXT("Pulled-out shaft reports negative insertion"), Inserted < -4.0);
        TestTrue(TEXT("Pulled-out tip is before the entrance"), Joints.Last().X < 0.0);
    }

    // Walls: open around the shaft, ahead of the tip by the falloff, closed far ahead.
    const double Rest = 0.4, Falloff = 2.5;
    TestEqual(TEXT("Wall at the full body is radius minus rest"), WallOpening(Shaft, 10.0, 3.0, Rest, Falloff), 1.6, 1.0e-6);
    TestTrue(TEXT("Wall just ahead of the tip starts to open"), WallOpening(Shaft, 6.0, 7.0, Rest, Falloff) > 0.0);
    TestEqual(TEXT("Wall far ahead of the tip is closed"), WallOpening(Shaft, 6.0, 12.0, Rest, Falloff), 0.0);

    // Bone offsets: start threshold, linear at first, saturating toward the maximum for large sizes.
    TestEqual(TEXT("Below the start the bone stays"), BoneOffset(1.0, 1.5, 1.0, 3.0), 0.0);
    TestTrue(TEXT("Small opening is nearly linear"), FMath::IsNearlyEqual(BoneOffset(0.1, 0.0, 1.0, 3.0), 0.1, 0.005));
    double Previous = 0.0;
    bool bMonotonic = true;
    for (int32 Step = 1; Step <= 40; ++Step)
    {
        const double Offset = BoneOffset(Step * 0.5, 0.0, 1.0, 3.0);
        bMonotonic &= Offset > Previous && Offset < 3.0;
        Previous = Offset;
    }
    TestTrue(TEXT("Offset grows with the size and never reaches the maximum"), bMonotonic);
    FShaft Large = Shaft;
    Large.Radius = 4.3; Large.Length = 32.0;
    TestTrue(TEXT("A large shaft opens the wall further"),
        BoneOffset(WallOpening(Large, 12.0, 4.0, Rest, Falloff), 0.0, 1.0, 3.0) > BoneOffset(WallOpening(Shaft, 12.0, 4.0, Rest, Falloff), 0.0, 1.0, 3.0));
    TestTrue(TEXT("Outer ring moves only for a large shaft"),
        BoneOffset(WallOpening(Shaft, 12.0, 0.0, Rest, Falloff), 2.5, 0.4, 2.5) == 0.0
        && BoneOffset(WallOpening(Large, 12.0, 0.0, Rest, Falloff), 2.5, 0.4, 2.5) > 0.0);
    return true;
}
#endif
