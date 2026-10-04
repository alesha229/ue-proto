#include "GratiaContactSolver.h"

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
}

FGratiaContactShape FGratiaContactShape::Sphere(const FVector& Center, double InRadius)
{
    FGratiaContactShape Result;
    Result.Type = EGratiaContactShapeType::Sphere; Result.A = Center; Result.Radius = InRadius;
    return Result;
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
        Remaining *= 1.0 - Hit.Time;
        ContactNormals.Add(Hit.Normal);
        // Project against the accumulated contact manifold, not only the last hit.
        for (int32 Projection = 0; Projection < 4; ++Projection)
            for (const FVector& Normal : ContactNormals)
            {
                const double Inward = FVector::DotProduct(Remaining, Normal);
                if (Inward < 0.0) Remaining -= Normal * Inward;
            }
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
