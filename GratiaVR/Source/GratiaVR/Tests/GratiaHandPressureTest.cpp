#include "../GratiaHandPressure.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGratiaHandPressureTest, "Gratia.Math.HandPressure",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FGratiaHandPressureTest::RunTest(const FString& Parameters)
{
    using namespace GratiaHandPressure;
    TestEqual(TEXT("Stationary penetration pushes"), Force(2, 0, 30, 0.5, 300), 60.0);
    TestEqual(TEXT("Fast sweep is bounded"), Force(0, 10000, 30, 0.5, 300), 300.0);
    TestEqual(TEXT("Retreat never attracts"), Force(0, -100, 30, 0.5, 300), 0.0);
    TestEqual(TEXT("Sliding without penetration adds no normal pressure"), Force(0, 0, 30, 0.5, 300), 0.0);
    TestEqual(TEXT("NaN is rejected"), Force(std::numeric_limits<double>::quiet_NaN(), 0, 30, 0.5, 300), 0.0);
    TestEqual(TEXT("Invalid settings are rejected"), Force(2, 10, -30, 0.5, 300), 0.0);
    return true;
}
#endif
