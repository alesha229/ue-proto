#include "../GratiaSceneLibrary.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGratiaMusicAnalysisTest, "Gratia.Scenes.MusicSampling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGratiaMusicAnalysisTest::RunTest(const FString& Parameters)
{
    auto* Track = NewObject<UGratiaMusicAnalysis>();
    TestEqual(TEXT("Empty track is silent"), Track->Sample(0).X, 0.0f);
    Track->FramesPerSecond = 2;
    Track->Frames = {FVector4f(0, .2f, 0, 1), FVector4f(1, .6f, 1, 0)};
    const FVector4f Middle = Track->Sample(.25f);
    TestTrue(TEXT("Bands and beat interpolate in track time"), FMath::IsNearlyEqual(Middle.X, .5f) && FMath::IsNearlyEqual(Middle.Y, .4f) && FMath::IsNearlyEqual(Middle.W, .5f));
    TestEqual(TEXT("Negative time is silent"), Track->Sample(-.1f).X, 0.0f);
    TestEqual(TEXT("Track end is silent"), Track->Sample(1).X, 0.0f);
    TestEqual(TEXT("Huge time cannot overflow the sample index"), Track->Sample(std::numeric_limits<float>::max()).X, 0.0f);
    TestEqual(TEXT("NaN time is silent"), Track->Sample(std::numeric_limits<float>::quiet_NaN()).X, 0.0f);
    Track->Frames[0] = FVector4f(-2, 4, std::numeric_limits<float>::infinity(), 0);
    const FVector4f Safe = Track->Sample(0);
    TestTrue(TEXT("Malformed authored bands remain finite and bounded"), Safe.X == 0 && Safe.Y == 1 && Safe.Z == 0);
    Track->FramesPerSecond = std::numeric_limits<float>::quiet_NaN();
    TestEqual(TEXT("Malformed sample rate is silent"), Track->Sample(0).Y, 0.0f);
    return true;
}
#endif
