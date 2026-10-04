#include "GratiaContactSolver.h"
#include <limits>

namespace
{
    constexpr double MathEpsilon = 1.0e-12;

    bool IsFiniteVector(const FVector& V)
    {
        return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
    }

    bool IsBoundedVector(const FVector& V, double Limit)
    {
        return IsFiniteVector(V) && FMath::Abs(V.X) <= Limit && FMath::Abs(V.Y) <= Limit && FMath::Abs(V.Z) <= Limit;
    }

    FVector SafeDirection(const FVector& Value, const FVector& Fallback = FVector(-1, 0, 0))
    {
        const double LengthSquared = Value.SizeSquared();
        return FMath::IsFinite(LengthSquared) && LengthSquared > MathEpsilon
            ? Value / FMath::Sqrt(LengthSquared) : Fallback;
    }

    bool IsValidShape(const FGratiaContactShape& Shape, double Limit)
    {
        if (!IsBoundedVector(Shape.A, Limit)) return false;
        switch (Shape.Type)
        {
        case EGratiaContactShapeType::Sphere:
            return FMath::IsFinite(Shape.Radius) && Shape.Radius > 0.0 && Shape.Radius <= Limit;
        case EGratiaContactShapeType::Capsule:
            return IsBoundedVector(Shape.B, Limit) && FMath::IsFinite(Shape.Radius) && Shape.Radius > 0.0 && Shape.Radius <= Limit;
        case EGratiaContactShapeType::AxisAlignedBox:
            return IsBoundedVector(Shape.HalfExtent, Limit) && Shape.HalfExtent.X > 0.0 && Shape.HalfExtent.Y > 0.0 && Shape.HalfExtent.Z > 0.0;
        default:
            return false;
        }
    }

    struct FDistance
    {
        double Signed = 0.0;
        FVector Normal = FVector(-1, 0, 0);
    };

    FDistance DistanceTo(const FVector& Point, const FGratiaContactShape& Shape, const FVector& Preferred)
    {
        FDistance Result;
        if (Shape.Type != EGratiaContactShapeType::AxisAlignedBox)
        {
            FVector Center = Shape.A;
            if (Shape.Type == EGratiaContactShapeType::Capsule)
            {
                const FVector Segment = Shape.B - Shape.A;
                const double LengthSquared = Segment.SizeSquared();
                if (LengthSquared > MathEpsilon)
                    Center += Segment * FMath::Clamp(FVector::DotProduct(Point - Shape.A, Segment) / LengthSquared, 0.0, 1.0);
            }
            const FVector Offset = Point - Center;
            Result.Signed = Offset.Size() - Shape.Radius;
            Result.Normal = SafeDirection(Offset, Preferred);
            return Result;
        }

        const FVector Relative = Point - Shape.A;
        const FVector Clamped(
            FMath::Clamp(Relative.X, -Shape.HalfExtent.X, Shape.HalfExtent.X),
            FMath::Clamp(Relative.Y, -Shape.HalfExtent.Y, Shape.HalfExtent.Y),
            FMath::Clamp(Relative.Z, -Shape.HalfExtent.Z, Shape.HalfExtent.Z));
        const FVector Outside = Relative - Clamped;
        if (Outside.SizeSquared() > MathEpsilon)
        {
            Result.Signed = Outside.Size();
            Result.Normal = SafeDirection(Outside, Preferred);
            return Result;
        }
        const FVector Depth = Shape.HalfExtent - Relative.GetAbs();
        int32 Axis = 0;
        if (Depth.Y < Depth.X) Axis = 1;
        if (Depth.Z < Depth[Axis]) Axis = 2;
        Result.Signed = -Depth[Axis];
        Result.Normal = FVector::ZeroVector;
        Result.Normal[Axis] = Relative[Axis] > 0.0 ? 1.0 : Relative[Axis] < 0.0 ? -1.0 : Preferred[Axis] > 0.0 ? 1.0 : -1.0;
        return Result;
    }

    // A line intersects each convex obstacle in at most one closed interval.
    struct FInterval
    {
        bool bHit = false;
        double Enter = 0.0;
        double Exit = 0.0;

        void Include(double InEnter, double InExit)
        {
            if (InEnter > InExit || !FMath::IsFinite(InEnter) || !FMath::IsFinite(InExit)) return;
            if (!bHit) { Enter = InEnter; Exit = InExit; bHit = true; }
            else { Enter = FMath::Min(Enter, InEnter); Exit = FMath::Max(Exit, InExit); }
        }
    };

    FInterval SphereInterval(const FVector& Origin, const FVector& Direction, const FVector& Center, double Radius)
    {
        FInterval Result;
        const FVector Relative = Origin - Center;
        const double A = Direction.SizeSquared();
        if (A <= MathEpsilon) return Result;
        const double B = FVector::DotProduct(Relative, Direction);
        const double C = Relative.SizeSquared() - Radius * Radius;
        const double Discriminant = B * B - A * C;
        if (Discriminant < 0.0) return Result;
        const double Root = FMath::Sqrt(FMath::Max(0.0, Discriminant));
        Result.Include((-B - Root) / A, (-B + Root) / A);
        return Result;
    }

    FInterval RayInterval(const FVector& Origin, const FVector& Direction, const FGratiaContactShape& Shape, double Skin)
    {
        if (Shape.Type == EGratiaContactShapeType::Sphere)
            return SphereInterval(Origin, Direction, Shape.A, Shape.Radius + Skin);

        FInterval Result;
        if (Shape.Type == EGratiaContactShapeType::AxisAlignedBox)
        {
            double Enter = -1.0e100, Exit = 1.0e100;
            const FVector Relative = Origin - Shape.A;
            const FVector Extent = Shape.HalfExtent + FVector(Skin);
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                if (FMath::Abs(Direction[Axis]) <= MathEpsilon)
                {
                    if (FMath::Abs(Relative[Axis]) > Extent[Axis]) return Result;
                    continue;
                }
                double Near = (-Extent[Axis] - Relative[Axis]) / Direction[Axis];
                double Far = (Extent[Axis] - Relative[Axis]) / Direction[Axis];
                if (Near > Far) Swap(Near, Far);
                Enter = FMath::Max(Enter, Near);
                Exit = FMath::Min(Exit, Far);
                if (Enter > Exit) return Result;
            }
            Result.Include(Enter, Exit);
            return Result;
        }

        const FVector Segment = Shape.B - Shape.A;
        const double Length = Segment.Size();
        const double Radius = Shape.Radius + Skin;
        if (Length <= MathEpsilon) return SphereInterval(Origin, Direction, Shape.A, Radius);
        const FVector Axis = Segment / Length;
        const FVector Relative = Origin - Shape.A;
        const double OriginAxis = FVector::DotProduct(Relative, Axis);
        const double DirectionAxis = FVector::DotProduct(Direction, Axis);
        const FVector RadialOrigin = Relative - Axis * OriginAxis;
        const FVector RadialDirection = Direction - Axis * DirectionAxis;
        const double A = RadialDirection.SizeSquared();
        const double B = FVector::DotProduct(RadialOrigin, RadialDirection);
        const double C = RadialOrigin.SizeSquared() - Radius * Radius;
        double Enter = -1.0e100, Exit = 1.0e100;
        bool bCylinder = true;
        if (A <= MathEpsilon)
        {
            bCylinder = C <= 0.0;
        }
        else
        {
            const double Discriminant = B * B - A * C;
            bCylinder = Discriminant >= 0.0;
            if (bCylinder)
            {
                const double Root = FMath::Sqrt(FMath::Max(0.0, Discriminant));
                Enter = (-B - Root) / A;
                Exit = (-B + Root) / A;
            }
        }
        if (bCylinder)
        {
            if (FMath::Abs(DirectionAxis) <= MathEpsilon)
                bCylinder = OriginAxis >= 0.0 && OriginAxis <= Length;
            else
            {
                double Near = -OriginAxis / DirectionAxis;
                double Far = (Length - OriginAxis) / DirectionAxis;
                if (Near > Far) Swap(Near, Far);
                Enter = FMath::Max(Enter, Near);
                Exit = FMath::Min(Exit, Far);
            }
            if (bCylinder) Result.Include(Enter, Exit);
        }
        const FInterval CapA = SphereInterval(Origin, Direction, Shape.A, Radius);
        const FInterval CapB = SphereInterval(Origin, Direction, Shape.B, Radius);
        if (CapA.bHit) Result.Include(CapA.Enter, CapA.Exit);
        if (CapB.bHit) Result.Include(CapB.Enter, CapB.Exit);
        return Result;
    }

    double ClearanceViolation(const FVector& Point, TConstArrayView<FGratiaContactShape> Shapes, double Skin)
    {
        double Worst = 0.0;
        for (const FGratiaContactShape& Shape : Shapes)
            Worst = FMath::Max(Worst, Skin - DistanceTo(Point, Shape, FVector(-1, 0, 0)).Signed);
        return Worst;
    }

    bool ProjectOut(FVector& Point, TConstArrayView<FGratiaContactShape> Shapes,
        const FGratiaContactSolveSettings& Settings, const FVector& Preferred, int32& Iterations)
    {
        for (int32 Pass = 0; Pass < Settings.MaxProjectionIterations; ++Pass)
        {
            if (ClearanceViolation(Point, Shapes, Settings.Skin) <= Settings.PenetrationTolerance) return true;
            ++Iterations;
            for (const FGratiaContactShape& Shape : Shapes)
            {
                const FDistance Distance = DistanceTo(Point, Shape, Preferred);
                if (Distance.Signed < Settings.Skin - Settings.PenetrationTolerance)
                    Point += Distance.Normal * (Settings.Skin - Distance.Signed + Settings.PenetrationTolerance * 0.25);
            }
            if (!IsFiniteVector(Point)) return false;
        }
        return ClearanceViolation(Point, Shapes, Settings.Skin) <= Settings.PenetrationTolerance;
    }

    // Cyclic projection can oscillate between overlapping spheres. Escape the connected union
    // along deterministic rays instead; a distant disconnected obstacle cannot enlarge the jump.
    FVector EscapeUnion(const FVector& Origin, TConstArrayView<FGratiaContactShape> Shapes,
        const FGratiaContactSolveSettings& Settings, const FVector& Preferred)
    {
        TArray<FVector, TInlineAllocator<27>> Directions;
        Directions.Add(Preferred);
        for (int32 X = -1; X <= 1; ++X) for (int32 Y = -1; Y <= 1; ++Y) for (int32 Z = -1; Z <= 1; ++Z)
            if (X != 0 || Y != 0 || Z != 0) Directions.Add(SafeDirection(FVector(X, Y, Z)));
        FVector Best = Origin;
        double BestDistance = 1.0e100;
        for (const FVector& Direction : Directions)
        {
            double End = 0.0;
            for (int32 Pass = 0; Pass <= Shapes.Num(); ++Pass)
            {
                const double Before = End;
                for (const FGratiaContactShape& Shape : Shapes)
                {
                    const FInterval Interval = RayInterval(Origin, Direction, Shape, Settings.Skin);
                    if (Interval.bHit && Interval.Enter <= End + Settings.PenetrationTolerance
                        && Interval.Exit >= End - Settings.PenetrationTolerance && Interval.Exit >= 0.0)
                        End = FMath::Max(End, Interval.Exit + Settings.PenetrationTolerance);
                }
                if (End <= Before) break;
            }
            const FVector Candidate = Origin + Direction * End;
            if (IsFiniteVector(Candidate) && End < BestDistance
                && ClearanceViolation(Candidate, Shapes, Settings.Skin) <= Settings.PenetrationTolerance)
            {
                Best = Candidate;
                BestDistance = End;
            }
        }
        if (BestDistance < 1.0e100) return Best;

        // Bounded last resort outside every obstacle's world bounding box.
        double MaximumX = Origin.X;
        for (const FGratiaContactShape& Shape : Shapes)
        {
            const double Extent = Shape.Type == EGratiaContactShapeType::AxisAlignedBox ? Shape.HalfExtent.X : Shape.Radius;
            MaximumX = FMath::Max(MaximumX, Shape.A.X + Extent);
            if (Shape.Type == EGratiaContactShapeType::Capsule) MaximumX = FMath::Max(MaximumX, Shape.B.X + Extent);
        }
        return FVector(MaximumX + Settings.Skin + Settings.PenetrationTolerance, Origin.Y, Origin.Z);
    }

    struct FSweepHit
    {
        int32 ShapeIndex = INDEX_NONE;
        double Time = 1.0;
        FVector Normal = FVector::ZeroVector;
    };

    FSweepHit Sweep(const FVector& Start, const FVector& Travel,
        TConstArrayView<FGratiaContactShape> Shapes, double Skin, double Tolerance)
    {
        FSweepHit Best;
        const FVector Preferred = SafeDirection(-Travel);
        for (int32 Index = 0; Index < Shapes.Num(); ++Index)
        {
            const FInterval Interval = RayInterval(Start, Travel, Shapes[Index], Skin);
            if (!Interval.bHit || Interval.Exit < 0.0 || Interval.Enter > 1.0) continue;
            const double Time = FMath::Max(0.0, Interval.Enter);
            const FVector AtHit = Start + Travel * Time;
            FGratiaContactShape Inflated = Shapes[Index];
            if (Inflated.Type == EGratiaContactShapeType::AxisAlignedBox) Inflated.HalfExtent += FVector(Skin);
            else Inflated.Radius += Skin;
            const FDistance Distance = DistanceTo(AtHit, Inflated, Preferred);
            // A tangent or outward departure from the boundary is allowed.
            if (FVector::DotProduct(Travel, Distance.Normal) >= -MathEpsilon) continue;
            // A projection within the tolerated skin may produce a slightly negative entry.
            if (Interval.Enter < 0.0 && DistanceTo(Start, Shapes[Index], Preferred).Signed < Skin - Tolerance)
            {
                Best.ShapeIndex = Index; Best.Time = 0.0; Best.Normal = Distance.Normal;
                return Best;
            }
            if (Best.ShapeIndex == INDEX_NONE || Time < Best.Time - MathEpsilon)
            {
                Best.ShapeIndex = Index; Best.Time = Time; Best.Normal = Distance.Normal;
            }
        }
        return Best;
    }

    FVector ConstrainToManifold(const FVector& Desired, TConstArrayView<FVector> Normals)
    {
        auto IsAllowed = [&Normals](const FVector& Candidate)
        {
            for (const FVector& Normal : Normals)
                if (FVector::DotProduct(Candidate, Normal) < -1.0e-9) return false;
            return true;
        };
        if (IsAllowed(Desired)) return Desired;
        // The closest vector in a 3D contact cone lies inside it, on one plane, on a
        // two-plane intersection or at zero. Enumerating those cases avoids cyclic
        // projection pushing a stationary hand away from an overlapping-sphere corner.
        FVector Best = FVector::ZeroVector;
        double BestError = Desired.SizeSquared();
        auto Consider = [&](const FVector& Candidate)
        {
            const double Error = (Desired - Candidate).SizeSquared();
            if (Error < BestError && IsAllowed(Candidate)) { Best = Candidate; BestError = Error; }
        };
        for (int32 A = 0; A < Normals.Num(); ++A)
        {
            Consider(Desired - Normals[A] * FVector::DotProduct(Desired, Normals[A]));
            for (int32 B = A + 1; B < Normals.Num(); ++B)
            {
                const FVector Cross = FVector::CrossProduct(Normals[A], Normals[B]);
                if (Cross.SizeSquared() > MathEpsilon)
                {
                    const FVector Direction = SafeDirection(Cross);
                    Consider(Direction * FVector::DotProduct(Desired, Direction));
                }
            }
        }
        return Best;
    }
}

FGratiaContactShape FGratiaContactShape::Sphere(const FVector& Center, double InRadius)
{
    FGratiaContactShape Result;
    Result.Type = EGratiaContactShapeType::Sphere; Result.A = Center; Result.Radius = InRadius;
    return Result;
}

namespace
{
    struct FContactRegressionReporter
    {
        bool bPassed = true;
        int32 Checks = 0;
        FString Failures;
        void TestTrue(const FString& Label, bool Value)
        {
            ++Checks;
            if (!Value) AddError(Label);
        }
        void TestFalse(const FString& Label, bool Value) { TestTrue(Label, !Value); }
        void AddError(const FString& Label)
        {
            bPassed = false;
            if (!Failures.IsEmpty()) Failures += TEXT("; ");
            Failures += Label;
        }
        FString MakeReport() const
        {
            return FString::Printf(TEXT("Contact solver: %d assertions; 2000 seeded mixed trajectories; %s%s%s"),
                Checks, bPassed ? TEXT("PASS") : TEXT("FAIL"),
                Failures.IsEmpty() ? TEXT("") : TEXT(": "), *Failures);
        }
    };
}

bool GratiaContactSolver::RunRegressionChecks(FString& Report)
{
    FContactRegressionReporter Check;
    auto IsSafe = [&Check](const TCHAR* Label, const FGratiaContactSolveResult& Result,
        TConstArrayView<FGratiaContactShape> Shapes)
    {
        Check.TestFalse(FString::Printf(TEXT("%s: finite output"), Label), Result.Position.ContainsNaN());
        Check.TestTrue(FString::Printf(TEXT("%s: no final penetration"), Label),
            GratiaContactSolver::MaxPenetration(Result.Position, Shapes) <= 0.001);
    };

    TArray<FGratiaContactShape> Shapes;
    Shapes.Add(FGratiaContactShape::Box(FVector::ZeroVector, FVector(50)));
    auto Result = GratiaContactSolver::Solve(FVector(-200, 0, 0), FVector(200, 0, 0), Shapes);
    IsSafe(TEXT("Fast complete cube traversal"), Result, Shapes);
    Check.TestTrue(TEXT("Cube sweep stops on the entering face, not on the far side"), Result.Position.X < -50.0 && Result.Position.X > -50.1);
    Check.TestTrue(TEXT("Cube reports the first shape"), Result.bBlocked && Result.FirstShapeIndex == 0);
    Check.TestTrue(TEXT("Ordinary cube sweep converges without fallback"), Result.bConverged && !Result.bUsedFallback);

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Sphere(FVector::ZeroVector, 10.0));
    Result = GratiaContactSolver::Solve(FVector(-10, 0, 0), FVector(30, 0, 0), Shapes);
    IsSafe(TEXT("Sphere boundary crossing"), Result, Shapes);
    Check.TestTrue(TEXT("A start on the exact surface cannot cross the sphere"), Result.Position.X < -9.999);
    Result = GratiaContactSolver::Solve(FVector(-10, 0, 0), FVector(-30, 0, 0), Shapes);
    Check.TestTrue(TEXT("Outward movement from the sphere surface is allowed"), Result.Position.Equals(FVector(-30, 0, 0), 0.001));
    Result = GratiaContactSolver::Solve(FVector(-30, 10.02, 0), FVector(30, 10.02, 0), Shapes);
    Check.TestTrue(TEXT("A grazing tangent does not snag"), Result.Position.Equals(FVector(30, 10.02, 0), 0.001));
    FVector Resting(-10.02, 0, 0);
    for (int32 Index = 0; Index < 1000; ++Index)
        Resting = GratiaContactSolver::Solve(Resting, Resting, Shapes).Position;
    Check.TestTrue(TEXT("A stationary surface contact does not drift"), Resting.Equals(FVector(-10.02, 0, 0), 0.00001));

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Sphere(FVector(-3, 0, 0), 5.0));
    Shapes.Add(FGratiaContactShape::Sphere(FVector(3, 0, 0), 5.0));
    Result = GratiaContactSolver::Solve(FVector(0, -20, 0), FVector::ZeroVector, Shapes);
    IsSafe(TEXT("Target inside two overlapping spheres"), Result, Shapes);
    Check.TestTrue(TEXT("Overlap endpoint stays on the approach side"), Result.Position.Y < -3.9);
    const FVector OverlapContact = Result.Position;
    FVector HeldContact = OverlapContact;
    for (int32 Index = 0; Index < 1000; ++Index)
        HeldContact = GratiaContactSolver::Solve(HeldContact, FVector::ZeroVector, Shapes).Position;
    Check.TestTrue(TEXT("A held target in an overlap corner does not drift away"),
        HeldContact.Equals(OverlapContact, 0.001));
    Result = GratiaContactSolver::Solve(FVector::ZeroVector, FVector::ZeroVector, Shapes);
    IsSafe(TEXT("Start inside two overlapping spheres"), Result, Shapes);
    Check.TestTrue(TEXT("Cyclic overlap uses a bounded deterministic escape"), Result.bStartedPenetrating && Result.bUsedFallback && Result.StartCorrectionDistance < 6.0);
    const FVector FirstEscape = Result.Position;
    for (int32 Index = 0; Index < 20; ++Index)
        Check.TestTrue(TEXT("Repeated identical overlap is deterministic"),
            GratiaContactSolver::Solve(FVector::ZeroVector, FVector::ZeroVector, Shapes).Position.Equals(FirstEscape, 0.000001));

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Capsule(FVector(0, 0, -20), FVector(0, 0, 20), 10.0));
    Result = GratiaContactSolver::Solve(FVector(-100, 0, 0), FVector(100, 0, 0), Shapes);
    IsSafe(TEXT("Capsule cylinder traversal"), Result, Shapes);
    Check.TestTrue(TEXT("Capsule cylinder stops fast movement"), Result.Position.X < -10.0 && Result.Position.X > -10.1);
    Result = GratiaContactSolver::Solve(FVector(0, 0, 100), FVector::ZeroVector, Shapes);
    IsSafe(TEXT("Capsule cap traversal"), Result, Shapes);
    Check.TestTrue(TEXT("Capsule upper end cap is swept"), Result.Position.Z > 30.0 && Result.Position.Z < 30.1);
    Shapes[0] = FGratiaContactShape::Capsule(FVector(-10, 0, -20), FVector(10, 0, 20), 10.0);
    Result = GratiaContactSolver::Solve(FVector(0, -100, 0), FVector(0, 100, 0), Shapes);
    IsSafe(TEXT("Oblique capsule traversal"), Result, Shapes);
    Check.TestTrue(TEXT("Capsule segment orientation is respected"), Result.Position.Y < -10.0 && Result.Position.Y > -10.1);
    Shapes[0] = FGratiaContactShape::Capsule(FVector::ZeroVector, FVector::ZeroVector, 10.0);
    Result = GratiaContactSolver::Solve(FVector(-100, 0, 0), FVector(100, 0, 0), Shapes);
    Check.TestTrue(TEXT("Zero-length capsule behaves as a sphere"), Result.Position.X < -10.0 && Result.Position.X > -10.1);

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Box(FVector::ZeroVector, FVector(50)));
    Result = GratiaContactSolver::Solve(FVector(-100, -30, 0), FVector(100, 100, 0), Shapes);
    IsSafe(TEXT("Tangential box slide"), Result, Shapes);
    Check.TestTrue(TEXT("Sliding preserves unobstructed tangential movement"), FMath::Abs(Result.Position.Y - 100.0) < 0.001);
    Check.TestTrue(TEXT("Sliding does not go through the blocked face"), Result.Position.X < -50.0);

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Box(FVector::ZeroVector, FVector(5, 100, 100)));
    Shapes.Add(FGratiaContactShape::Box(FVector::ZeroVector, FVector(100, 5, 100)));
    Result = GratiaContactSolver::Solve(FVector(-50, -50, 0), FVector(50, 50, 0), Shapes);
    IsSafe(TEXT("Two-face corner"), Result, Shapes);
    Check.TestTrue(TEXT("Both corner normals constrain the final movement"), Result.Position.X < -5.0 && Result.Position.Y < -5.0);

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Sphere(FVector(-20, 0, 0), 5.0));
    Shapes.Add(FGratiaContactShape::Sphere(FVector(20, 0, 0), 5.0));
    Result = GratiaContactSolver::Solve(FVector(-100, 0, 0), FVector(100, 0, 0), Shapes);
    const FVector Ordered = Result.Position;
    Swap(Shapes[0], Shapes[1]);
    Result = GratiaContactSolver::Solve(FVector(-100, 0, 0), FVector(100, 0, 0), Shapes);
    Check.TestTrue(TEXT("Earliest collision is independent of shape iteration order"), Ordered.Equals(Result.Position, 0.000001));
    Check.TestTrue(TEXT("Earliest index refers to the caller's original shape array"), Result.FirstShapeIndex == 1);

    Shapes.Reset();
    Shapes.Add(FGratiaContactShape::Sphere(FVector::ZeroVector, 10.0));
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    Result = GratiaContactSolver::Solve(FVector(-30, 0, 0), FVector(NaN, 0, 0), Shapes);
    IsSafe(TEXT("Invalid target"), Result, Shapes);
    Check.TestTrue(TEXT("Invalid target holds the finite previous position and reports rejection"),
        !Result.bInputValid && Result.Position.Equals(FVector(-30, 0, 0), 0.001));
    Shapes.Add(FGratiaContactShape::Sphere(FVector(NaN, 0, 0), 10.0));
    Result = GratiaContactSolver::Solve(FVector(-30, 0, 0), FVector(30, 0, 0), Shapes);
    IsSafe(TEXT("Invalid shape"), Result, Shapes);
    Check.TestTrue(TEXT("Invalid geometry is counted and cannot disable valid collisions"),
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
            Check.AddError(FString::Printf(TEXT("Seeded mixed-shape trajectory failed at step %d"), Index));
            break;
        }
        Point = Result.Position;
    }
    Report = Check.MakeReport();
    return Check.bPassed;
}


FGratiaContactShape FGratiaContactShape::Capsule(const FVector& Start, const FVector& End, double InRadius)
{
    FGratiaContactShape Result;
    Result.Type = EGratiaContactShapeType::Capsule; Result.A = Start; Result.B = End; Result.Radius = InRadius;
    return Result;
}

FGratiaContactShape FGratiaContactShape::Box(const FVector& Center, const FVector& InHalfExtent)
{
    FGratiaContactShape Result;
    Result.Type = EGratiaContactShapeType::AxisAlignedBox; Result.A = Center; Result.HalfExtent = InHalfExtent;
    return Result;
}

double GratiaContactSolver::MaxPenetration(const FVector& Point, TConstArrayView<FGratiaContactShape> Shapes)
{
    if (!IsBoundedVector(Point, 3.0e9)) return TNumericLimits<double>::Max();
    double Worst = 0.0;
    for (const FGratiaContactShape& Shape : Shapes)
        if (IsValidShape(Shape, 1.0e9))
            Worst = FMath::Max(Worst, -DistanceTo(Point, Shape, FVector(-1, 0, 0)).Signed);
    return Worst;
}

FGratiaContactSolveResult GratiaContactSolver::Solve(const FVector& From, const FVector& Target,
    TConstArrayView<FGratiaContactShape> Shapes, const FGratiaContactSolveSettings& InSettings)
{
    FGratiaContactSolveSettings Settings = InSettings;
    Settings.Skin = FMath::IsFinite(Settings.Skin) ? FMath::Clamp(Settings.Skin, 0.0, 10.0) : 0.02;
    Settings.PenetrationTolerance = FMath::IsFinite(Settings.PenetrationTolerance)
        ? FMath::Clamp(Settings.PenetrationTolerance, 1.0e-6, 0.1) : 0.001;
    Settings.MaxProjectionIterations = FMath::Clamp(Settings.MaxProjectionIterations, 1, 128);
    Settings.MaxSweepIterations = FMath::Clamp(Settings.MaxSweepIterations, 1, 32);
    Settings.MaximumCoordinate = FMath::IsFinite(Settings.MaximumCoordinate)
        ? FMath::Clamp(Settings.MaximumCoordinate, 1.0, 1.0e9) : 1.0e9;

    FGratiaContactSolveResult Result;
    TArray<FGratiaContactShape, TInlineAllocator<32>> ValidShapes;
    TArray<int32, TInlineAllocator<32>> OriginalIndices;
    for (int32 Index = 0; Index < Shapes.Num(); ++Index)
    {
        if (IsValidShape(Shapes[Index], Settings.MaximumCoordinate))
        {
            ValidShapes.Add(Shapes[Index]); OriginalIndices.Add(Index);
        }
        else ++Result.InvalidShapeCount;
    }
    Result.bInputValid = Result.InvalidShapeCount == 0
        && IsBoundedVector(From, Settings.MaximumCoordinate) && IsBoundedVector(Target, Settings.MaximumCoordinate);
    FVector Start = IsBoundedVector(From, Settings.MaximumCoordinate) ? From
        : IsBoundedVector(Target, Settings.MaximumCoordinate) ? Target : FVector::ZeroVector;
    const FVector SafeTarget = IsBoundedVector(Target, Settings.MaximumCoordinate) ? Target : Start;
    const FVector Preferred = SafeDirection(Start - SafeTarget);
    const FVector OriginalStart = Start;
    Result.bStartedPenetrating = ClearanceViolation(Start, ValidShapes, 0.0) > Settings.PenetrationTolerance;
    if (!ProjectOut(Start, ValidShapes, Settings, Preferred, Result.ProjectionIterations))
    {
        Start = EscapeUnion(OriginalStart, ValidShapes, Settings, Preferred);
        Result.bUsedFallback = true;
    }
    Result.StartCorrectionDistance = FVector::Distance(OriginalStart, Start);
    Result.bBlocked = Result.StartCorrectionDistance > Settings.PenetrationTolerance;
    FVector Point = Start, Remaining = SafeTarget - Start;
    TArray<FVector, TInlineAllocator<8>> ContactNormals;
    bool bFinished = Remaining.SizeSquared() <= MathEpsilon;
    for (int32 Pass = 0; !bFinished && Pass < Settings.MaxSweepIterations; ++Pass)
    {
        ++Result.SweepIterations;
        const FSweepHit Hit = Sweep(Point, Remaining, ValidShapes, Settings.Skin, Settings.PenetrationTolerance);
        if (Hit.ShapeIndex == INDEX_NONE)
        {
            Point += Remaining; Remaining = FVector::ZeroVector; bFinished = true; break;
        }
        Result.bBlocked = true;
        if (Result.FirstShapeIndex == INDEX_NONE) Result.FirstShapeIndex = OriginalIndices[Hit.ShapeIndex];
        Point += Remaining * Hit.Time;
        ContactNormals.Add(Hit.Normal);
        Remaining = ConstrainToManifold(SafeTarget - Point, ContactNormals);
        bFinished = Remaining.SizeSquared() <= MathEpsilon;
    }
    if (!bFinished)
    {
        // Keep the last swept-safe point; returning the unchecked target would tunnel.
        Result.bUsedFallback = true; Result.bConverged = false;
    }
    FVector Final = Point;
    if (!ProjectOut(Final, ValidShapes, Settings, Preferred, Result.ProjectionIterations))
    {
        // Start was already validated. Stop there if final projection cannot resolve the union.
        Final = Start; Result.bUsedFallback = true; Result.bConverged = false;
    }
    Result.Position = Final;
    Result.MaxPenetration = ClearanceViolation(Final, ValidShapes, 0.0);
    Result.TargetCorrectionDistance = FVector::Distance(Final, SafeTarget);
    Result.bBlocked |= Result.TargetCorrectionDistance > Settings.PenetrationTolerance;
    Result.bConverged &= IsFiniteVector(Final) && Result.MaxPenetration <= Settings.PenetrationTolerance;
    Result.bUsedFallback |= !Result.bInputValid;
    return Result;
}
