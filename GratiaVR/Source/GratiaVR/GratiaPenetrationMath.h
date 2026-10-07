#pragma once

#include "CoreMinimal.h"

/**
 * Geometry of a jointed shaft entering a body channel, without engine objects (automation-tested).
 * Units are world centimetres; the solver scales profile values by the character scale first.
 */
namespace GratiaPenetration
{
/** Polyline with cumulative arc length; sampling extends past both ends along the end directions. */
struct FPath
{
    TArray<FVector> Points;
    TArray<double> Along;

    void Reset() { Points.Reset(); Along.Reset(); }
    void Add(const FVector& Point)
    {
        if (!Points.IsEmpty() && FVector::DistSquared(Points.Last(), Point) < 1.0e-8) return;
        Along.Add(Points.IsEmpty() ? 0.0 : Along.Last() + FVector::Distance(Points.Last(), Point));
        Points.Add(Point);
    }
    double Length() const { return Along.IsEmpty() ? 0.0 : Along.Last(); }
    bool IsValid() const { return Points.Num() >= 2 && Length() > 1.0e-4; }
    FVector Direction(int32 Segment) const
    {
        Segment = FMath::Clamp(Segment, 0, Points.Num() - 2);
        return (Points[Segment + 1] - Points[Segment]).GetSafeNormal();
    }
    /** Point (and unit direction) at arc length S. */
    FVector Sample(double S, FVector* OutDirection = nullptr) const
    {
        if (!IsValid()) { if (OutDirection) *OutDirection = FVector::XAxisVector; return Points.IsEmpty() ? FVector::ZeroVector : Points[0]; }
        int32 Segment = 0;
        if (S >= Length()) Segment = Points.Num() - 2;
        else if (S > 0.0) while (Segment < Points.Num() - 2 && Along[Segment + 1] < S) ++Segment;
        const FVector Direction = this->Direction(Segment);
        if (OutDirection) *OutDirection = Direction;
        return Points[Segment] + Direction * (S - Along[Segment]);
    }
};

/** Shaft shape: rounded tip over TipCm, a body of Radius that grows to Radius x BaseScale at the base. */
struct FShaft
{
    double Length = 18.0;
    double Radius = 2.0;
    double TipCm = 2.5;
    double BaseScale = 1.0;
    int32 Joints = 8;

    /** Radius at distance U from the tip (0 outside the shaft). */
    double RadiusAt(double U) const
    {
        if (U <= 0.0 || U > Length + 1.0e-6) return 0.0;
        const double Body = Radius * FMath::Lerp(1.0, BaseScale, FMath::Clamp(U / FMath::Max(Length, 0.01), 0.0, 1.0));
        if (U >= TipCm) return Body;
        const double T = U / FMath::Max(TipCm, 0.01);
        return Body * FMath::Sqrt(FMath::Max(0.0, 1.0 - FMath::Square(1.0 - T)));
    }
    double Spacing() const { return Length / FMath::Max(1, Joints - 1); }
};

/** Straight free shaft from Base along Direction (joint 0 is the base, the last joint is the tip). */
inline void StraightJoints(const FShaft& Shaft, const FVector& Base, const FVector& Direction, TArray<FVector>& Out)
{
    Out.Reset();
    for (int32 Joint = 0; Joint < Shaft.Joints; ++Joint) Out.Add(Base + Direction * (Shaft.Spacing() * Joint));
}

/** A free shaft enters the channel: it points inward within MaxAngle, its base is outside the
 *  entrance plane, and its tip lies within Radius of the entrance (along and across the axis). */
inline bool CanCapture(const FShaft& Shaft, const FVector& Base, const FVector& Direction, const FVector& Entrance, const FVector& Inward,
    double Radius, double MaxAngleDegrees)
{
    if (FVector::DotProduct(Direction, Inward) < FMath::Cos(FMath::DegreesToRadians(MaxAngleDegrees))) return false;
    if (FVector::DotProduct(Base - Entrance, Inward) > 0.0) return false;
    const FVector Tip = Base + Direction * Shaft.Length - Entrance;
    const double Along = FVector::DotProduct(Tip, Inward);
    return Along >= -Radius && Along <= Radius && (Tip - Inward * Along).Size() <= Radius;
}

/** Cubic Bezier from A (leaving along TA) to B (arriving along TB), as a dense path. */
inline void AddBezier(FPath& Path, const FVector& A, const FVector& TA, const FVector& B, const FVector& TB, int32 Samples = 24)
{
    const double Handle = 0.4 * FVector::Distance(A, B);
    const FVector C1 = A + TA * Handle, C2 = B - TB * Handle;
    for (int32 I = 0; I <= Samples; ++I)
    {
        const double T = double(I) / Samples, U = 1.0 - T;
        Path.Add(U * U * U * A + 3.0 * U * U * T * C1 + 3.0 * U * T * T * C2 + T * T * T * B);
    }
}

/** Shaft engaged in a channel. Outside: a curve from the held base (leaving along its direction)
 *  to the entrance (arriving along the channel). The rest of the length lies in the channel.
 *  Inserted is that rest (may be negative: pulled out). Clamped to MaxDepth, the shaft compresses
 *  instead of passing the channel end. Out: joints evenly spaced along the shaft. */
inline double EngagedJoints(const FShaft& Shaft, const FVector& Base, const FVector& Direction, const FPath& Channel, double MaxDepth,
    TArray<FVector>& Out)
{
    FVector Inward;
    const FVector Entrance = Channel.Sample(0.0, &Inward);
    FPath Shape;
    AddBezier(Shape, Base, Direction, Entrance, Inward);
    const double Inserted = Shaft.Length - Shape.Length();
    const double Depth = FMath::Clamp(Inserted, 0.0, FMath::Max(0.0, MaxDepth));
    // Channel samples: its own corner points, then the depth reached.
    for (int32 Point = 1; Point < Channel.Points.Num() && Channel.Along[Point] < Depth; ++Point) Shape.Add(Channel.Points[Point]);
    Shape.Add(Channel.Sample(Depth));
    Out.Reset();
    // Pulled out (Inserted < 0) the tip stops short of the entrance; past MaxDepth it compresses.
    const double Step = (Inserted < 0.0 ? Shaft.Length : Shape.Length()) / FMath::Max(1, Shaft.Joints - 1);
    for (int32 Joint = 0; Joint < Shaft.Joints; ++Joint) Out.Add(Shape.Sample(Step * Joint));
    return Inserted;
}

/** How far the shaft opens the channel at arc length S: its radius there minus the closed radius. */
inline double OpeningAt(const FShaft& Shaft, double Inserted, double S, double RestRadius)
{
    return FMath::Max(0.0, Shaft.RadiusAt(Inserted - S) - RestRadius);
}

/** Opening felt by a wall bone at S: the wall stretches over Falloff around the shaft (it starts
 *  opening before the tip arrives and closes behind it). */
inline double WallOpening(const FShaft& Shaft, double Inserted, double S, double RestRadius, double Falloff)
{
    double Opening = OpeningAt(Shaft, Inserted, S, RestRadius);
    if (Falloff <= 0.0) return Opening;
    for (int32 Step = 1; Step <= 3; ++Step)
    {
        const double Offset = Falloff * Step / 3.0, Weight = 1.0 - Step / 4.0;
        Opening = FMath::Max(Opening, Weight * FMath::Max(OpeningAt(Shaft, Inserted, S - Offset, RestRadius), OpeningAt(Shaft, Inserted, S + Offset, RestRadius)));
    }
    return Opening;
}

/** Bone offset from the opening: starts at Start, rises by Response and saturates smoothly at Max
 *  (a large shaft stretches the wall to its limit without a hard stop). */
inline double BoneOffset(double Opening, double Start, double Response, double Max)
{
    const double Linear = FMath::Max(0.0, Opening - Start) * Response;
    return Max <= 0.0 ? 0.0 : Max * (1.0 - FMath::Exp(-Linear / Max));
}
}
