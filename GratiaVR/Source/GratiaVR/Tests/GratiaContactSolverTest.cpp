#include "../GratiaContactSolver.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGratiaContactSolverTest, "Gratia.Math.ContactSolver",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FGratiaContactSolverTest::RunTest(const FString& Parameters)
{
    FString Report;
    const bool bPassed = GratiaContactSolver::RunRegressionChecks(Report);
    AddInfo(Report);
    TestTrue(TEXT("World-independent collision solver regressions"), bPassed);
    if (!bPassed) AddError(Report);
    return bPassed;
}
#endif
