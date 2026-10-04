#pragma once
#include "CoreMinimal.h"

namespace GratiaHandPressure
{
    // Pressure is unilateral. Retreat and tangential motion never pull the body.
    inline double Force(double Depth, double ClosingSpeed, double Stiffness, double Damping, double Limit)
    {
        if (!FMath::IsFinite(Depth) || !FMath::IsFinite(ClosingSpeed) || !FMath::IsFinite(Stiffness)
            || !FMath::IsFinite(Damping) || !FMath::IsFinite(Limit) || Stiffness < 0 || Damping < 0 || Limit < 0)
            return 0;
        return FMath::Clamp(FMath::Max(0.0, Depth) * Stiffness + ClosingSpeed * Damping, 0.0, Limit);
    }
}
