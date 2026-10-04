#pragma once

#include "CoreMinimal.h"

/** World-space convex obstacles for a hand centre. Expand them by the hand radius before Solve. */
enum class EGratiaContactShapeType : uint8 { Sphere, Capsule, AxisAlignedBox };

struct GRATIAVR_API FGratiaContactShape
{
    EGratiaContactShapeType Type = EGratiaContactShapeType::Sphere;
    FVector A = FVector::ZeroVector;
    FVector B = FVector::ZeroVector;
    FVector HalfExtent = FVector::ZeroVector;
    double Radius = 1.0;

    static FGratiaContactShape Sphere(const FVector& Center, double InRadius);
    static FGratiaContactShape Capsule(const FVector& Start, const FVector& End, double InRadius);
    static FGratiaContactShape Box(const FVector& Center, const FVector& InHalfExtent);
};

struct GRATIAVR_API FGratiaContactSolveSettings
{
    // Centimetres. A small outward skin avoids repeated zero-time hits and surface jitter.
    double Skin = 0.02;
    double PenetrationTolerance = 0.001;
    int32 MaxProjectionIterations = 32;
    int32 MaxSweepIterations = 8;
    // Reject unreasonable input before squared-distance arithmetic. This is not a travel-speed limit.
    double MaximumCoordinate = 1.0e9;
};

struct GRATIAVR_API FGratiaContactSolveResult
{
    FVector Position = FVector::ZeroVector;
    bool bBlocked = false;
    bool bInputValid = true;
    bool bConverged = true;
    bool bUsedFallback = false;
    bool bStartedPenetrating = false;
    int32 InvalidShapeCount = 0;
    int32 FirstShapeIndex = INDEX_NONE;
    int32 ProjectionIterations = 0;
    int32 SweepIterations = 0;
    double MaxPenetration = 0.0;
    double StartCorrectionDistance = 0.0;
    double TargetCorrectionDistance = 0.0;
};

namespace GratiaContactSolver
{
    /**
     * Sweeps the entire movement against the earliest obstacle, then slides along the surface.
     * Shapes are static during this call; resample moving proxies and pass the previous visual
     * hand position as From on each update. Penetrating starts are depenetrated deterministically.
     * No unchecked target is returned when an iteration budget expires.
     *
     * AABB inflation is conservative at corners. Moving-obstacle CCD and orientation-dependent
     * hand geometry are intentionally outside this point solver.
     */
    GRATIAVR_API FGratiaContactSolveResult Solve(const FVector& From, const FVector& Target,
        TConstArrayView<FGratiaContactShape> Shapes,
        const FGratiaContactSolveSettings& Settings = FGratiaContactSolveSettings());

    /** Maximum depth inside the unskinned obstacles, in centimetres. Invalid shapes are ignored. */
    GRATIAVR_API double MaxPenetration(const FVector& Point, TConstArrayView<FGratiaContactShape> Shapes);
}
