#include "../GratiaContactSolver.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGratiaContactSolverTest, "Gratia.Math.ContactSolver",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FGratiaContactSolverTest::RunTest(const FString& Parameters)
{
    const FGratiaContactSolveSettings Settings;
    auto IsSafe = [this](const TCHAR* Label, const FGratiaContactSolveResult& Result,
        TConstArrayView<FGratiaContactShape> Shapes)
    {
        TestFalse(FString::Printf(TEXT("%s: finite output"), Label), Result.Position.ContainsNaN());
        TestTrue(FString::Printf(TEXT("%s: no final penetration"), Label),
            GratiaContactSolver::MaxPenetration(Result.Position, Shapes) <= 0.001);
    };

    TArray<FGratiaContactShape> Shapes;
    Shapes.Add(FGratiaContactShape::Box(FVector::ZeroVector, FVector(50)));
    auto Result = GratiaContactSolver::Solve(FVector(-200, 0, 0), FVector(200, 0, 0), Shapes);
    IsSafe(TEXT("Fast complete cube traversal"), Result, Shapes);
    TestTrue(TEXT("Cube sweep stops on the entering face, not on the far side"), Result.Position.X < -50.0 && Result.Position.X > -50.1);
    TestTrue(TEXT("Cube reports the first shape"), Result.bBlocked && Result.FirstShapeIndex == 0);
    TestTrue(TEXT("Ordinary cube sweep converges without fallback"), Result.bConverged && !Result.bUsedFallback);

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Sphere(FVector::ZeroVector, 10.0));
    Result = GratiaContactSolver::Solve(FVector(-10, 0, 0), FVector(30, 0, 0), Shapes);
    IsSafe(TEXT("Sphere boundary crossing"), Result, Shapes);
    TestTrue(TEXT("A start on the exact surface cannot cross the sphere"), Result.Position.X < -9.999);
    Result = GratiaContactSolver::Solve(FVector(-10, 0, 0), FVector(-30, 0, 0), Shapes);
    TestTrue(TEXT("Outward movement from the sphere surface is allowed"), Result.Position.Equals(FVector(-30, 0, 0), 0.001));
    Result = GratiaContactSolver::Solve(FVector(-30, 10.02, 0), FVector(30, 10.02, 0), Shapes);
    TestTrue(TEXT("A grazing tangent does not snag"), Result.Position.Equals(FVector(30, 10.02, 0), 0.001));
    FVector Resting(-10.02, 0, 0);
    for (int32 Index = 0; Index < 1000; ++Index)
        Resting = GratiaContactSolver::Solve(Resting, Resting, Shapes).Position;
    TestTrue(TEXT("A stationary surface contact does not drift"), Resting.Equals(FVector(-10.02, 0, 0), 0.00001));

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Sphere(FVector(-3, 0, 0), 5.0));
    Shapes.Add(FGratiaContactShape::Sphere(FVector(3, 0, 0), 5.0));
    Result = GratiaContactSolver::Solve(FVector(0, -20, 0), FVector::ZeroVector, Shapes);
    IsSafe(TEXT("Target inside two overlapping spheres"), Result, Shapes);
    TestTrue(TEXT("Overlap endpoint stays on the approach side"), Result.Position.Y < -3.9);
    Result = GratiaContactSolver::Solve(FVector::ZeroVector, FVector::ZeroVector, Shapes);
    IsSafe(TEXT("Start inside two overlapping spheres"), Result, Shapes);
    TestTrue(TEXT("Cyclic overlap uses a bounded deterministic escape"), Result.bStartedPenetrating && Result.bUsedFallback && Result.StartCorrectionDistance < 6.0);
    const FVector FirstEscape = Result.Position;
    for (int32 Index = 0; Index < 20; ++Index)
        TestTrue(TEXT("Repeated identical overlap is deterministic"),
            GratiaContactSolver::Solve(FVector::ZeroVector, FVector::ZeroVector, Shapes).Position.Equals(FirstEscape, 0.000001));

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Capsule(FVector(0, 0, -20), FVector(0, 0, 20), 10.0));
    Result = GratiaContactSolver::Solve(FVector(-100, 0, 0), FVector(100, 0, 0), Shapes);
    IsSafe(TEXT("Capsule cylinder traversal"), Result, Shapes);
    TestTrue(TEXT("Capsule cylinder stops fast movement"), Result.Position.X < -10.0 && Result.Position.X > -10.1);
    Result = GratiaContactSolver::Solve(FVector(0, 0, 100), FVector::ZeroVector, Shapes);
    IsSafe(TEXT("Capsule cap traversal"), Result, Shapes);
    TestTrue(TEXT("Capsule upper end cap is swept"), Result.Position.Z > 30.0 && Result.Position.Z < 30.1);
    Shapes[0] = FGratiaContactShape::Capsule(FVector(-10, 0, -20), FVector(10, 0, 20), 10.0);
    Result = GratiaContactSolver::Solve(FVector(0, -100, 0), FVector(0, 100, 0), Shapes);
    IsSafe(TEXT("Oblique capsule traversal"), Result, Shapes);
    TestTrue(TEXT("Capsule segment orientation is respected"), Result.Position.Y < -10.0 && Result.Position.Y > -10.1);
    Shapes[0] = FGratiaContactShape::Capsule(FVector::ZeroVector, FVector::ZeroVector, 10.0);
    Result = GratiaContactSolver::Solve(FVector(-100, 0, 0), FVector(100, 0, 0), Shapes);
    TestTrue(TEXT("Zero-length capsule behaves as a sphere"), Result.Position.X < -10.0 && Result.Position.X > -10.1);

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Box(FVector::ZeroVector, FVector(50)));
    Result = GratiaContactSolver::Solve(FVector(-100, -30, 0), FVector(100, 100, 0), Shapes);
    IsSafe(TEXT("Tangential box slide"), Result, Shapes);
    TestTrue(TEXT("Sliding preserves unobstructed tangential movement"), FMath::Abs(Result.Position.Y - 100.0) < 0.001);
    TestTrue(TEXT("Sliding does not go through the blocked face"), Result.Position.X < -50.0);

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Box(FVector::ZeroVector, FVector(5, 100, 100)));
    Shapes.Add(FGratiaContactShape::Box(FVector::ZeroVector, FVector(100, 5, 100)));
    Result = GratiaContactSolver::Solve(FVector(-50, -50, 0), FVector(50, 50, 0), Shapes);
    IsSafe(TEXT("Two-face corner"), Result, Shapes);
    TestTrue(TEXT("Both corner normals constrain the final movement"), Result.Position.X < -5.0 && Result.Position.Y < -5.0);

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Sphere(FVector(-20, 0, 0), 5.0));
    Shapes.Add(FGratiaContactShape::Sphere(FVector(20, 0, 0), 5.0));
    Result = GratiaContactSolver::Solve(FVector(-100, 0, 0), FVector(100, 0, 0), Shapes);
    const FVector Ordered = Result.Position;
    Swap(Shapes[0], Shapes[1]);
    Result = GratiaContactSolver::Solve(FVector(-100, 0, 0), FVector(100, 0, 0), Shapes);
    TestTrue(TEXT("Earliest collision is independent of shape iteration order"), Ordered.Equals(Result.Position, 0.000001));
    TestTrue(TEXT("Earliest index refers to the caller's original shape array"), Result.FirstShapeIndex == 1);

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Sphere(FVector::ZeroVector, 10.0));
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    Result = GratiaContactSolver::Solve(FVector(-30, 0, 0), FVector(NaN, 0, 0), Shapes);
    IsSafe(TEXT("Invalid target"), Result, Shapes);
    TestTrue(TEXT("Invalid target holds the finite previous position and reports rejection"),
        !Result.bInputValid && Result.Position.Equals(FVector(-30, 0, 0), 0.001));
    Shapes.Add(FGratiaContactShape::Sphere(FVector(NaN, 0, 0), 10.0));
    Result = GratiaContactSolver::Solve(FVector(-30, 0, 0), FVector(30, 0, 0), Shapes);
    IsSafe(TEXT("Invalid shape"), Result, Shapes);
    TestTrue(TEXT("Invalid geometry is counted and cannot disable valid collisions"),
        !Result.bInputValid && Result.InvalidShapeCount == 1 && Result.Position.X < -10.0);

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Sphere(FVector::ZeroVector, 10.0));
    Shapes.Add(FGratiaContactShape::Capsule(FVector(20, 0, -15), FVector(20, 0, 15), 8.0));
    Shapes.Add(FGratiaContactShape::Box(FVector(-20, 20, 0), FVector(5, 10, 15)));
    FRandomStream Random(728913);
    FVector Point(-80, -80, 0);
    for (int32 Index = 0; Index < 2000; ++Index)
    {
        const FVector Target(Random.FRandRange(-75.0f, 75.0f), Random.FRandRange(-75.0f, 75.0f), Random.FRandRange(-40.0f, 40.0f));
        Result = GratiaContactSolver::Solve(Point, Target, Shapes);
        if (Result.Position.ContainsNaN() || GratiaContactSolver::MaxPenetration(Result.Position, Shapes) > 0.001)
        {
            AddError(FString::Printf(TEXT("Seeded mixed-shape trajectory failed at step %d"), Index));
            return false;
        }
        Point = Result.Position;
    }
    return true;
}
#endif
