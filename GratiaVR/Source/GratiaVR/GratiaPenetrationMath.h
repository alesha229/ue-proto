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

/** Profile of a shaft along its length (the primitive's selectable forms; the hands are Smooth). */
enum class EShaftForm : uint8 { Smooth, Realistic, Knotted, Beads, Cone, Ribbed, Flared, Tentacle, Count };

/** Shaft shape: Form's profile of Radius (rounded tip over TipCm, Radius x BaseScale at the base for Smooth). */
struct FShaft
{
    double Length = 18.0;
    double Radius = 2.0;
    double TipCm = 2.5;
    double BaseScale = 1.0;
    int32 Joints = 8;
    EShaftForm Form = EShaftForm::Smooth;

    /** Radius at distance U from the tip (0 outside the shaft). */
    double RadiusAt(double U) const
    {
        if (U <= 0.0 || U > Length + 1.0e-6) return 0.0;
        const double L = FMath::Max(Length, 0.01), R = Radius, T = FMath::Clamp(U / L, 0.0, 1.0);
        // Rounded end: 0 at the tip, full over Over cm (a quarter ellipse).
        auto Cap = [U](double Over) { const double X = FMath::Clamp(U / FMath::Max(Over, 0.01), 0.0, 1.0); return FMath::Sqrt(FMath::Max(0.0, 1.0 - FMath::Square(1.0 - X))); };
        auto Smooth = [](double X) { X = FMath::Clamp(X, 0.0, 1.0); return X * X * (3.0 - 2.0 * X); };
        auto Bump = [U](double Centre, double Width) { return FMath::Exp(-FMath::Square((U - Centre) / FMath::Max(Width, 0.01))); };
        switch (Form)
        {
        case EShaftForm::Realistic:
        {
            // A head a little wider than the shaft with a ridge and a groove behind it, the shaft thickening to the base.
            const double Head = FMath::Max(1.5, 0.2 * L);
            const double Body = R * FMath::Lerp(1.0, 1.1, T);
            if (U < Head) return 1.1 * R * Cap(0.7 * Head);
            if (U < Head + 0.5) return FMath::Lerp(1.1 * R, 0.86 * R, (U - Head) / 0.5);
            return FMath::Lerp(0.86 * R, Body, Smooth((U - Head - 0.5) / 2.5));
        }
        case EShaftForm::Knotted:
            // A pointed, tapering tip and a knot near the base (up to 1.45 x the shaft).
            return R * (0.85 * (0.3 + 0.7 * Smooth(U / (0.3 * L))) * Cap(1.0) + 0.6 * Bump(0.8 * L, 0.07 * L));
        case EShaftForm::Beads:
        {
            // Balls growing from 0.55 to 1 x the radius toward the base on a thin cord.
            const int32 Count = FMath::Clamp(FMath::RoundToInt(L / (3.2 * R + 1.0)), 3, 8);
            double Size[8], Centre[8], Total = 0.0;
            for (int32 Ball = 0; Ball < Count; ++Ball)
            {
                Size[Ball] = R * (0.55 + 0.45 * Ball / double(Count - 1));
                Total += (Ball ? 0.35 * R : 0.0) + 2.0 * Size[Ball];
            }
            const double Fit = L / FMath::Max(Total, 0.01);
            double Along = 0.0, Best = 0.22 * R;
            for (int32 Ball = 0; Ball < Count; ++Ball)
            {
                Along += (Ball ? 0.35 * R * Fit : 0.0) + Size[Ball] * Fit;
                Centre[Ball] = Along;
                Along += Size[Ball] * Fit;
                const double Half = Size[Ball] * Fit, Off = U - Centre[Ball];
                if (FMath::Abs(Off) < Half) Best = FMath::Max(Best, Size[Ball] * FMath::Sqrt(1.0 - FMath::Square(Off / Half)));
            }
            return Best;
        }
        case EShaftForm::Cone:
            // A narrow rounded tip widening steadily to 1.5 x the radius at the base.
            return R * FMath::Lerp(0.3, 1.5, FMath::Pow(T, 0.85)) * Cap(1.2);
        case EShaftForm::Ribbed:
        {
            const double Body = R * FMath::Lerp(1.0, BaseScale, T) * Cap(TipCm);
            const double Period = FMath::Max(1.2, 0.07 * L);
            return U > TipCm ? Body * (1.0 + 0.13 * (0.5 + 0.5 * FMath::Cos(2.0 * PI * (U - TipCm) / Period))) : Body;
        }
        case EShaftForm::Flared:
        {
            // A blunt flared head (1.4 x) over a narrow neck, a ring halfway down and a thick base.
            const double Head = FMath::Max(1.5, 0.09 * L), Neck = 0.04 * L;
            const double Body = R * FMath::Lerp(0.95, 1.12, T) * (1.0 + 0.16 * Bump(0.45 * L, 0.035 * L));
            if (U < Head) return 1.4 * R * FMath::Sqrt(Cap(Head));
            if (U < Head + Neck) return FMath::Lerp(1.4 * R, 0.86 * R, Smooth((U - Head) / Neck));
            return FMath::Lerp(0.86 * R, Body, Smooth((U - Head - Neck) / (0.1 * L)));
        }
        case EShaftForm::Tentacle:
        {
            // From a thin pointed tip to 1.35 x at the base, with soft rings.
            const double Body = R * FMath::Lerp(0.12, 1.35, FMath::Pow(T, 0.75)) * Cap(0.6);
            return T > 0.2 ? Body * (1.0 + 0.06 * FMath::Sin(2.0 * PI * U / 1.8)) : Body;
        }
        default:
        {
            const double Body = R * FMath::Lerp(1.0, BaseScale, T);
            if (U >= TipCm) return Body;
            return Body * Cap(TipCm);
        }
        }
    }
    /** Slope of the radius along U (per cm). */
    double SlopeAt(double U) const { constexpr double H = 0.05; return (RadiusAt(U + H) - RadiusAt(FMath::Max(1.0e-4, U - H))) / (2.0 * H); }
    /** Widest radius along the shaft. */
    double MaxRadius() const
    {
        double Widest = 0.0;
        for (int32 Sample = 1; Sample <= 96; ++Sample) Widest = FMath::Max(Widest, RadiusAt(Length * Sample / 96.0));
        return Widest;
    }
    double Spacing() const { return Length / FMath::Max(1, Joints - 1); }
};

/** Menu name of a primitive form. */
inline const TCHAR* ShaftFormName(EShaftForm Form)
{
    switch (Form)
    {
    case EShaftForm::Realistic: return TEXT("Реалистичный");
    case EShaftForm::Knotted: return TEXT("С узлом");
    case EShaftForm::Beads: return TEXT("Бусины");
    case EShaftForm::Cone: return TEXT("Конус");
    case EShaftForm::Ribbed: return TEXT("Ребристый");
    case EShaftForm::Flared: return TEXT("Расклёшенный");
    case EShaftForm::Tentacle: return TEXT("Щупальце");
    default: return TEXT("Гладкий");
    }
}

/** The player's hand as a shaft: three straight fingers, the whole hand with straight fingers, a fist. */
enum class EHandShape : uint8 { None, Fingers, Hand, Fist };

/** Leading tip back to mid-forearm (the base), cm at hand scale 1: the fingers and the flat hand reach the
 *  fingertips and widen through the knuckles and the palm into the forearm; the fist leads with its knuckles and
 *  narrows to the wrist and the forearm. The forearm is part of the shaft so a deep hand still opens the walls. */
inline FShaft HandShaft(EHandShape Shape, double Scale = 1.0)
{
    FShaft Shaft;
    switch (Shape)
    {
    case EHandShape::Fingers: Shaft.Length = 30.0; Shaft.Radius = 1.6; Shaft.TipCm = 1.3; Shaft.BaseScale = 2.0; break;
    case EHandShape::Hand: Shaft.Length = 30.0; Shaft.Radius = 1.8; Shaft.TipCm = 2.0; Shaft.BaseScale = 2.0; break;
    case EHandShape::Fist: Shaft.Length = 26.0; Shaft.Radius = 4.2; Shaft.TipCm = 3.0; Shaft.BaseScale = 0.75; break;
    default: break;
    }
    const double Safe = FMath::IsFinite(Scale) && Scale > 0.1 ? Scale : 1.0;
    Shaft.Length *= Safe; Shaft.Radius *= Safe; Shaft.TipCm *= Safe;
    return Shaft;
}

/** Palm centre to the shape's leading tip (fingertips, or the fist's knuckles), cm at hand scale 1. */
inline double HandTipFromPalmCm(EHandShape Shape) { return Shape == EHandShape::Fist ? 6.0 : 13.0; }

/** Palm centre back to the wrist, cm at hand scale 1. */
constexpr double HandWristBackCm = 5.0;

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
