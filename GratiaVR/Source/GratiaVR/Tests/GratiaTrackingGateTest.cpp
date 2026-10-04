#include "../GratiaStage1Runtime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGratiaTrackingGateTest, "Gratia.Stage1.TrackingLossGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGratiaTrackingGateTest::RunTest(const FString& Parameters)
{
    FGratiaTrackingGate Gate;
    TestFalse(TEXT("No contact before first real tracking"), Gate.CanInteract());
    Gate.Update(true, 0.05f, 0.15f, 0.25f);
    TestTrue(TEXT("First pose is acquiring, not interactive"), Gate.State == EGratiaHandState::Acquiring);
    Gate.Update(false, 0.01f, 0.15f, 0.25f);
    TestTrue(TEXT("A transient pose is discarded immediately"), Gate.State == EGratiaHandState::Unavailable);
    TestEqual(TEXT("Loss resets stable time"), Gate.StableSeconds, 0.0f);

    for (int32 Index = 0; Index < 4; ++Index) Gate.Update(true, 0.05f, 0.15f, 0.25f);
    TestTrue(TEXT("Stable tracking still requires visual recovery"), Gate.State == EGratiaHandState::Recovering);
    TestFalse(TEXT("Contact disabled during recovery"), Gate.CanInteract());
    TestTrue(TEXT("Recovery alpha is bounded"), Gate.RecoveryAlpha(0.25f) >= 0.0f && Gate.RecoveryAlpha(0.25f) <= 1.0f);
    for (int32 Index = 0; Index < 5; ++Index) Gate.Update(true, 0.05f, 0.15f, 0.25f);
    TestTrue(TEXT("Only complete recovery permits contact"), Gate.CanInteract());
    Gate.Update(false, 0.0f, 0.15f, 0.25f);
    TestFalse(TEXT("Loss disables contact in the same update"), Gate.CanInteract());
    TestEqual(TEXT("Loss also resets recovery time"), Gate.RecoverySeconds, 0.0f);

    Gate.Update(true, 1000.0f, 0.15f, 0.25f);
    TestTrue(TEXT("A long stalled frame cannot bypass stable tracking"), Gate.State == EGratiaHandState::Acquiring);
    Gate.Update(true, -1.0f, 0.15f, 0.25f);
    TestEqual(TEXT("Negative delta cannot advance the gate"), Gate.StableSeconds, 0.1f);
    return true;
}
#endif

