#include "GratiaRuntimeVerification.h"
#include "GratiaStage1Runtime.h"
#include "GratiaStage1HUD.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaLocomotion.h"
#include "GratiaInteraction.h"
#include "GratiaMenu.h"
#include "GratiaSecondaryMotion.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MotionControllerComponent.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaVerification, Log, All);

namespace
{
    // Fixture-specific names belong to the verification harness, not player tracking.
    const FName CharacterTestBones[] = {
        TEXT("DEF-spine_006"), TEXT("DEF-hand_L"), TEXT("DEF-hand_R"),
        TEXT("DEF-foot_L"), TEXT("DEF-foot_R"), TEXT("root")
    };

    bool IsFiniteTransform(const FTransform& Transform)
    {
        return !Transform.ContainsNaN() && Transform.GetRotation().IsNormalized();
    }
}

UGratiaRuntimeVerification::UGratiaRuntimeVerification()
{
    // The runtime explicitly drives verification after tracking/hand updates.
    PrimaryComponentTick.bCanEverTick = false;
}

AGratiaStage1Runtime& UGratiaRuntimeVerification::GetRuntime() const
{
    return *CastChecked<AGratiaStage1Runtime>(GetOwner());
}

void UGratiaRuntimeVerification::ConfigureFromCommandLine()
{
    bSmokeTest = FParse::Param(FCommandLine::Get(), TEXT("GratiaSmokeTest"));
    bSelfTest = FParse::Param(FCommandLine::Get(), TEXT("GratiaSelfTest"));
    FParse::Value(FCommandLine::Get(), TEXT("GratiaSoakSeconds="), SoakDuration);
    FParse::Value(FCommandLine::Get(), TEXT("GratiaPerfSeconds="), PerfDuration);
    SoakDuration = FMath::Clamp(SoakDuration, 0.0f, 3600.0f);
    PerfDuration = FMath::Clamp(PerfDuration, 0.0f, 3600.0f);
    if (SoakDuration > 0.0f || PerfDuration > 0.0f)
        PerfRows = TEXT("seconds,xr,profile,frame_ms,game_ms,render_ms,gpu_ms,physics_bodies,physics_fault\n");
}

void UGratiaRuntimeVerification::ConfigureCaptureView()
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    FString View;
    if (Runtime.bXRActive || !FParse::Value(FCommandLine::Get(), TEXT("GratiaViewTest="), View)
        || !Runtime.PlayerController.IsValid() || !Runtime.PlayerPawn.IsValid() || !Runtime.TestCharacter.IsValid()) return;
    // This camera belongs only to an explicit desktop QA invocation.
    AGratiaPreviewCharacter* Character = Runtime.TestCharacter.Get();
    Character->SetActorLocation(FVector(0, 0, Character->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
    Runtime.PlayerPawn->SetActorHiddenInGame(true);
    FVector Eye(-250, 0, 125), Target(0, 0, 110);
    if (View == TEXT("Back")) Eye = FVector(250, 0, 125);
    if (View == TEXT("Left")) Eye = FVector(0, -250, 125);
    if (View == TEXT("Right")) Eye = FVector(0, 250, 125);
    if (View == TEXT("Face")) { Eye = FVector(-100, 0, 170); Target = FVector(0, 0, 169); }
    ACameraActor* CaptureView = GetWorld()->SpawnActor<ACameraActor>(Eye, (Target - Eye).Rotation());
    if (CaptureView)
    {
        CaptureView->GetCameraComponent()->FieldOfView = View == TEXT("Face") ? 35.0f : 80.0f;
        Runtime.PlayerController->SetViewTarget(CaptureView);
    }
}

void UGratiaRuntimeVerification::TestCheck(bool bPassed, const TCHAR* Description)
{
    if (bPassed)
    {
        UE_LOG(LogGratiaVerification, Display, TEXT("TEST PASS: %s"), Description);
    }
    else
    {
        bTestFailed = true;
        UE_LOG(LogGratiaVerification, Error, TEXT("TEST FAIL: %s"), Description);
    }
}

void UGratiaRuntimeVerification::RunWorldChecks()
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    TestCheck(Runtime.bPawnReady, TEXT("Template pawn has camera, common tracking origin and both hand proxies"));
    TestCheck(Runtime.PlayerController.IsValid() && Runtime.PlayerController->GetHUD() && Runtime.PlayerController->GetHUD()->IsA<AGratiaStage1HUD>(),
        TEXT("Stage 1 HUD is active"));
    TestCheck(Runtime.Camera.IsValid() && IsFiniteTransform(Runtime.Camera->GetComponentTransform()), TEXT("Camera transform is finite"));
    TestCheck(!Runtime.Camera.IsValid() || Runtime.bXRActive || FMath::IsNearlyEqual(Runtime.Camera->GetRelativeLocation().Z,
        Runtime.OriginalCameraRelative.GetLocation().Z + Runtime.DesktopEyeHeightCm, 0.1), TEXT("Desktop preview camera height is applied when XR is inactive"));
    int32 MotionControllers = 0;
    if (Runtime.PlayerPawn.IsValid())
    {
        TInlineComponentArray<UMotionControllerComponent*> Components;
        Runtime.PlayerPawn->GetComponents(Components);
        MotionControllers = Components.Num();
    }
    TestCheck(MotionControllers == 4, TEXT("Four template motion controllers (left/right grip and aim) are present"));
    TestCheck(Runtime.LeftHand.ContactCollider.IsValid() && Runtime.RightHand.ContactCollider.IsValid()
        && Runtime.LeftHand.ContactCollider->IsRegistered() && Runtime.RightHand.ContactCollider->IsRegistered()
        && FMath::IsNearlyEqual(Runtime.LeftHand.ContactCollider->GetUnscaledSphereRadius(), 6.0f)
        && FMath::IsNearlyEqual(Runtime.RightHand.ContactCollider->GetUnscaledSphereRadius(), 6.0f),
        TEXT("Both hand query colliders are registered with a six centimetre radius"));
    int32 RuntimeActors = 0;
    for (TActorIterator<AGratiaStage1Runtime> It(GetWorld()); It; ++It) ++RuntimeActors;
    TestCheck(RuntimeActors == 1, TEXT("Exactly one calibration/tracking runtime actor is present"));
    bool bReferenceCube = false;
    for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
    {
        UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
        if (Mesh && It->GetActorLocation().Equals(FVector(155.0, 150.0, 50.0), 0.1)
            && Mesh->Bounds.BoxExtent.Equals(FVector(50.0), 0.1))
        {
            bReferenceCube = true;
            break;
        }
    }
    TestCheck(bReferenceCube, TEXT("One metre scale cube exists at the room reference position"));
    RunCharacterWorldChecks();
    UE_LOG(LogGratiaVerification, Display, TEXT("TEST CONFIG: world=%s pawn=%s controllers=%d XR=%s; hardware tracking and 90 FPS are not tested here"),
        *GetWorld()->GetName(), *GetNameSafe(Runtime.PlayerPawn.Get()), MotionControllers, Runtime.bXRActive ? TEXT("enabled") : TEXT("disabled"));
}

void UGratiaRuntimeVerification::RunCharacterWorldChecks()
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    int32 CharacterCount = 0;
    for (TActorIterator<AGratiaPreviewCharacter> It(GetWorld()); It; ++It)
    {
        ++CharacterCount;
        Runtime.TestCharacter = *It;
    }
    TestCheck(CharacterCount == 1, TEXT("Exactly one Gratia preview character is present"));
    if (!Runtime.TestCharacter.IsValid()) return;
    AGratiaPreviewCharacter* Character = Runtime.TestCharacter.Get();
    USkeletalMeshComponent* Component = Character->CharacterMesh;
    USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
    TestCheck(Mesh && Mesh->GetPathName() == TEXT("/Game/Gratia/GameRig/SK_Gratia_Game.SK_Gratia_Game"),
        TEXT("The cooked Gratia skeletal mesh is loaded"));
    if (!Mesh || !Component) return;

    bool bAllClipsCompatible = true;
    for (EGratiaPreviewPose Pose : {EGratiaPreviewPose::Idle, EGratiaPreviewPose::Arms, EGratiaPreviewPose::Head})
    {
        const UAnimSequence* Clip = Character->GetPreviewAnimation(Pose);
        bAllClipsCompatible &= Clip && Clip->GetPlayLength() > 0.1f && Clip->GetSkeleton() == Mesh->GetSkeleton();
    }
    TestCheck(bAllClipsCompatible, TEXT("All three cooked preview clips are non-empty and use the Gratia skeleton"));
    for (const bool bBright : {false, true})
    {
        const UAnimSequence* Clip = Character->GetReactionAnimation(bBright);
        TestCheck(Clip && FMath::IsNearlyEqual(Clip->GetPlayLength(), 2.0f, 0.04f) && Clip->GetSkeleton() == Mesh->GetSkeleton(),
            bBright ? TEXT("Bright reaction cue is cooked on the clean skeleton") : TEXT("Soft reaction cue is cooked on the clean skeleton"));
    }
    UAnimSingleNodeInstance* Animation = Component->GetSingleNodeInstance();
    TestCheck(Animation && Animation->GetAnimationAsset() == Character->GetExpectedAnimation()
        && (Character->IsIdlePreview() ? Animation->IsPlaying() && Animation->IsLooping() : !Animation->IsPlaying()),
        TEXT("The requested preview animation is active with the correct playback mode"));
    TestCheck(Mesh->FindMorphTarget(TEXT("Mouth O wide")) != nullptr,
        TEXT("The formerly colliding Mouth O morph is present under its unique name"));
    TestCheck(Mesh->FindMorphTarget(TEXT("Eye L close")) && Mesh->FindMorphTarget(TEXT("Eye R close")),
        TEXT("Both eyelid morphs are available for idle blinking"));
    TestCheck(Character->GetActorScale3D().Equals(FVector::OneVector, 0.001)
        && Component->GetComponentScale().Equals(FVector::OneVector, 0.001)
        && FMath::IsNearlyEqual(Mesh->GetBounds().BoxExtent.Z * 2.0, 216.36, 2.2),
        TEXT("Unit character scale and source height including accessories are preserved within one percent"));

    bool bBonesFinite = true;
    InitialCharacterBones.Reset();
    for (const FName Bone : CharacterTestBones)
    {
        const bool bPresent = Component->GetBoneIndex(Bone) != INDEX_NONE;
        const FTransform Transform = Component->GetSocketTransform(Bone, RTS_Component);
        bBonesFinite &= bPresent && IsFiniteTransform(Transform);
        InitialCharacterBones.Add(Transform);
    }
    TestCheck(bBonesFinite && IsFiniteTransform(Character->GetActorTransform()),
        TEXT("Head, both hands, both feet and root bones exist with finite transforms"));
    InitialCharacterActor = Character->GetActorTransform();
    InitialAnimationTime = Animation ? Animation->GetCurrentTime() : 0.0f;
    UE_LOG(LogGratiaVerification, Display, TEXT("CHARACTER TEST CONFIG: actor=%s mode=%d clip=%s source_height_cm=%.2f"),
        *Character->GetName(), static_cast<int32>(Character->PreviewPose), *GetNameSafe(Character->GetExpectedAnimation()),
        Mesh->GetBounds().BoxExtent.Z * 2.0);
}

void UGratiaRuntimeVerification::SampleCharacterAnimation(bool bFinish)
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    if (!Runtime.TestCharacter.IsValid() || !Runtime.TestCharacter->IsIdlePreview())
    {
        if (bFinish)
        {
            UE_LOG(LogGratiaVerification, Display, TEXT("Idle motion observation skipped for a fixed diagnostic pose or absent character."));
        }
        return;
    }
    USkeletalMeshComponent* Component = Runtime.TestCharacter->CharacterMesh;
    if (!Component || InitialCharacterBones.Num() != UE_ARRAY_COUNT(CharacterTestBones))
    {
        if (bFinish) TestCheck(false, TEXT("Idle motion observation has a valid baseline"));
        return;
    }
    bool bFinite = true;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(CharacterTestBones); ++Index)
    {
        const FTransform Current = Component->GetSocketTransform(CharacterTestBones[Index], RTS_Component);
        bFinite &= IsFiniteTransform(Current);
        if (!IsFiniteTransform(Current)) continue;
        const FTransform& Baseline = InitialCharacterBones[Index];
        const double Distance = FVector::Distance(Current.GetLocation(), Baseline.GetLocation());
        const double Angle = FMath::RadiansToDegrees(Current.GetRotation().AngularDistance(Baseline.GetRotation()));
        if (Index == 0)
        {
            MaxHeadMovementCm = FMath::Max(MaxHeadMovementCm, Distance);
            MaxHeadMovementDegrees = FMath::Max(MaxHeadMovementDegrees, Angle);
        }
        else if (Index == 3 || Index == 4)
        {
            MaxFootDriftCm = FMath::Max(MaxFootDriftCm, Distance);
            MaxFootDriftDegrees = FMath::Max(MaxFootDriftDegrees, Angle);
        }
        else if (Index == 5)
        {
            MaxRootDriftCm = FMath::Max(MaxRootDriftCm, Distance);
            MaxRootDriftDegrees = FMath::Max(MaxRootDriftDegrees, Angle);
        }
    }
    const FTransform ActorTransform = Runtime.TestCharacter->GetActorTransform();
    bFinite &= IsFiniteTransform(ActorTransform);
    MaxRootDriftCm = FMath::Max(MaxRootDriftCm, FVector::Distance(ActorTransform.GetLocation(), InitialCharacterActor.GetLocation()));
    MaxRootDriftDegrees = FMath::Max(MaxRootDriftDegrees,
        FMath::RadiansToDegrees(ActorTransform.GetRotation().AngularDistance(InitialCharacterActor.GetRotation())));
    if (UAnimSingleNodeInstance* Animation = Component->GetSingleNodeInstance())
    {
        bAnimationTimeAdvanced |= FMath::Abs(Animation->GetCurrentTime() - InitialAnimationTime) > 0.05f;
    }
    // Observe the whole interval: a four-second loop can occupy the same pose at t=3 and t=7.
    bCharacterBonesRemainFinite &= bFinite;
    if (bFinish)
    {
        TestCheck(bCharacterBonesRemainFinite, TEXT("Animated character bone transforms remain finite throughout the observation"));
        TestCheck(bAnimationTimeAdvanced && (MaxHeadMovementCm > 0.02 || MaxHeadMovementDegrees > 0.05),
            TEXT("Idle animation advances and visibly changes the evaluated head transform between three and seven seconds"));
        TestCheck(MaxFootDriftCm <= 0.1 && MaxFootDriftDegrees <= 0.1,
            TEXT("Idle keeps both feet planted within one millimetre and one tenth of a degree"));
        TestCheck(MaxRootDriftCm <= 0.1 && MaxRootDriftDegrees <= 0.1,
            TEXT("Idle has no actor or root drift beyond one millimetre and one tenth of a degree"));
        UE_LOG(LogGratiaVerification, Display, TEXT("CHARACTER MOTION: head=%.5fcm/%.5fdeg feet=%.5fcm/%.5fdeg root=%.5fcm/%.5fdeg"),
            MaxHeadMovementCm, MaxHeadMovementDegrees, MaxFootDriftCm, MaxFootDriftDegrees, MaxRootDriftCm, MaxRootDriftDegrees);
    }
}

void UGratiaRuntimeVerification::RunSelfChecks()
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    FString MovementFailure;
    TestCheck(Runtime.Locomotion && Runtime.Locomotion->RunChecks(MovementFailure), TEXT("Head-relative walking, deadzone, wall sweep, snap pivot and teleport suppression"));
    if (!MovementFailure.IsEmpty()) UE_LOG(LogGratiaVerification, Error, TEXT("%s"), *MovementFailure);
    FString ContactFailure;
    TestCheck(Runtime.TestCharacter.IsValid() && Runtime.TestCharacter->Interaction && Runtime.TestCharacter->Interaction->RunChecks(ContactFailure),
        TEXT("All contact zones complete repeated two-hand cycles, cooldown and proxy constraint checks"));
    if (!ContactFailure.IsEmpty()) UE_LOG(LogGratiaVerification, Error, TEXT("%s"), *ContactFailure);
    TestCheck(Runtime.Menu && Runtime.Menu->RunChecks(), TEXT("World menu, ten pose/mood switches, reset and all quality profiles"));
    FString PhysicsFailure;
    TestCheck(Runtime.TestCharacter.IsValid() && Runtime.TestCharacter->SecondaryMotion && Runtime.TestCharacter->SecondaryMotion->RunChecks(PhysicsFailure),
        TEXT("Cooked PhysicsAsset, real Chaos bodies, quality budgets, planted core and physics disable/reset"));
    if (!PhysicsFailure.IsEmpty()) UE_LOG(LogGratiaVerification, Error, TEXT("%s"), *PhysicsFailure);
    FGratiaTrackingGate Gate;
    TestCheck(!Gate.CanInteract(), TEXT("Fresh tracking gate forbids interaction"));
    Gate.Update(true, 0.05f, 0.15f, 0.25f);
    Gate.Update(false, 0.01f, 0.15f, 0.25f);
    TestCheck(!Gate.CanInteract() && Gate.StableSeconds == 0.0f, TEXT("One transient pose does not acquire tracking"));
    for (int32 Index = 0; Index < 4; ++Index) Gate.Update(true, 0.05f, 0.15f, 0.25f);
    TestCheck(Gate.State == EGratiaHandState::Recovering && !Gate.CanInteract(), TEXT("Stable tracking requires visual recovery before contact"));
    for (int32 Index = 0; Index < 5; ++Index) Gate.Update(true, 0.05f, 0.15f, 0.25f);
    TestCheck(Gate.CanInteract(), TEXT("Continuous tracking and full recovery allow contact"));
    Gate.Update(false, 0.0f, 0.15f, 0.25f);
    TestCheck(!Gate.CanInteract() && Gate.RecoverySeconds == 0.0f, TEXT("Tracking loss cancels contact immediately"));
    Gate.Update(true, 1000.0f, 0.15f, 0.25f);
    TestCheck(Gate.State == EGratiaHandState::Acquiring, TEXT("A long stalled frame cannot skip tracking acquisition"));

    Runtime.AdjustHeight(2.0f);
    TestCheck(Runtime.TrackingOrigin.IsValid() && FMath::IsNearlyEqual(Runtime.TrackingOrigin->GetRelativeLocation().Z,
        Runtime.OriginalOriginZ + 2.0, 0.01), TEXT("Height calibration moves tracking origin by exactly two centimetres"));
    Runtime.AdjustHeight(10000.0f);
    TestCheck(Runtime.HeightOffsetCm == 100.0f, TEXT("Height calibration has a positive safety bound"));
    Runtime.AdjustHeight(-10000.0f);
    TestCheck(Runtime.HeightOffsetCm == -100.0f, TEXT("Height calibration has a negative safety bound"));
    Runtime.ResetHeight();
    TestCheck(Runtime.HeightOffsetCm == 0.0f, TEXT("Height reset returns to the initial floor origin"));

    for (const bool bLeft : { true, false })
    {
        Runtime.SetForcedTrackingLoss(bLeft, true);
        AGratiaStage1Runtime::FHandProxy& Hand = bLeft ? Runtime.LeftHand : Runtime.RightHand;
        Runtime.SetHandCollision(Hand, true);
        TestCheck(Hand.ContactCollider.IsValid()
            && Hand.ContactCollider->GetCollisionEnabled() == ECollisionEnabled::QueryOnly
            && Hand.ContactCollider->GetGenerateOverlapEvents(),
            bLeft ? TEXT("Left hand query contact can be enabled") : TEXT("Right hand query contact can be enabled"));
        Runtime.UpdateHand(Hand, bLeft, 0.016f);
        TestCheck(!Runtime.IsHandInteractionAllowed(bLeft), bLeft ? TEXT("Left lost hand forbids contact") : TEXT("Right lost hand forbids contact"));
        TestCheck(Hand.Visual.IsValid() && !Hand.Visual->GetAttachParent() && IsFiniteTransform(Hand.Visual->GetComponentTransform()),
            bLeft ? TEXT("Left proxy is detached safely from stale controller poses") : TEXT("Right proxy is detached safely from stale controller poses"));
        bool bCollisionDisabled = true;
        for (const AGratiaStage1Runtime::FCollisionSnapshot& Snapshot : Hand.Collisions)
        {
            bCollisionDisabled &= !Snapshot.Component.IsValid() ||
                (Snapshot.Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Snapshot.Component->GetGenerateOverlapEvents());
        }
        TestCheck(bCollisionDisabled, bLeft ? TEXT("Left lost proxy collision and overlap events are off") : TEXT("Right lost proxy collision and overlap events are off"));
        Runtime.SetForcedTrackingLoss(bLeft, false);
    }

    if (Runtime.PlayerPawn.IsValid() && Runtime.Camera.IsValid() && !Runtime.bXRActive)
    {
        Runtime.PlayerPawn->AddActorWorldOffset(FVector(30.0, -20.0, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
        Runtime.PlayerPawn->AddActorWorldRotation(FRotator(0.0, 30.0, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
        Runtime.Recenter();
        bRecenterTestPending = true;
    }
    else
    {
        UE_LOG(LogGratiaVerification, Display, TEXT("Desktop recenter perturbation test skipped while XR is active or pawn missing."));
    }
}

void UGratiaRuntimeVerification::RunRequestedTests(float DeltaSeconds)
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    if (!bSmokeTest && !bSelfTest) return;
    if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.0f) TestElapsedSeconds += DeltaSeconds;
    if (bRecenterTestPending && !Runtime.bPendingRecenter)
    {
        bRecenterTestPending = false;
        const FVector View = Runtime.Camera.IsValid() ? Runtime.Camera->GetComponentLocation() : FVector(UE_BIG_NUMBER);
        const FVector Anchor = Runtime.GetActorLocation();
        TestCheck(FMath::IsNearlyEqual(View.X, Anchor.X, 0.1) && FMath::IsNearlyEqual(View.Y, Anchor.Y, 0.1),
            TEXT("Recenter aligns the desktop head position with the room floor anchor"));
        TestCheck(Runtime.Camera.IsValid() && FMath::Abs(FMath::FindDeltaAngleDegrees(Runtime.Camera->GetComponentRotation().Yaw,
            Runtime.GetActorRotation().Yaw)) <= 0.1, TEXT("Recenter aligns desktop head yaw with the room anchor"));
    }
    if (!bTestChecksDone && TestElapsedSeconds >= 3.0f)
    {
        bTestChecksDone = true;
        RunWorldChecks();
        if (bSelfTest) RunSelfChecks();
        if (bSmokeTest && FApp::CanEverRender() && GEngine && GEngine->GameViewport)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/Windows/GratiaStage1Smoke.png")),
                true, true, false);
            UE_LOG(LogGratiaVerification, Display, TEXT("TEST SCREENSHOT requested in Saved/Screenshots/Windows (not a VR capture)."));
        }
    }
    if (bTestChecksDone && !bCharacterMotionChecksDone)
    {
        const bool bFinish = TestElapsedSeconds >= 7.0f;
        SampleCharacterAnimation(bFinish);
        bCharacterMotionChecksDone = bFinish;
    }
    if (bTestChecksDone && bCharacterMotionChecksDone && TestElapsedSeconds >= 8.0f && !bRecenterTestPending)
    {
        if (bTestFailed)
        {
            UE_LOG(LogGratiaVerification, Error, TEXT("GRATIA_STAGE1_TEST_FAIL"));
        }
        else
        {
            if (bSmokeTest) UE_LOG(LogGratiaVerification, Display, TEXT("GRATIA_STAGE1_SMOKE_PASS"));
            if (bSelfTest) UE_LOG(LogGratiaVerification, Display, TEXT("GRATIA_STAGE1_SELFTEST_PASS"));
        }
        FPlatformMisc::RequestExitWithStatus(false, bTestFailed ? 1 : 0, TEXT("GratiaStage1Tests"));
    }
}

void UGratiaRuntimeVerification::RunSoakAndMetrics(float DeltaSeconds)
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    if (bMetricsFinished || (SoakDuration <= 0.0f && PerfDuration <= 0.0f)
        || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
    MetricsSeconds += DeltaSeconds;
    MetricsInterval += DeltaSeconds;
    if (SoakDuration > 0.0f && Runtime.TestCharacter.IsValid())
    {
        auto* Character = Runtime.TestCharacter.Get();
        auto* Contact = Character->Interaction.Get();
        const int32 Cycle = FMath::FloorToInt(FMath::Max(0.0f, MetricsSeconds - 3.0f) / 3.0f);
        const int32 ZoneIndex = Cycle % Contact->Zones.Num();
        if (Cycle != SoakCycle)
        {
            SoakCycle = Cycle;
            Contact->Quality = (Cycle / Contact->Zones.Num()) % 3;
            Contact->Mood = Cycle % 3;
            Contact->bHairMotion = Contact->bClothMotion = Contact->bBodyMotion = true;
            Contact->bPhysicalMotion = Cycle % 9 != 8;
            Contact->bLocalSpring = Cycle % 7 != 6;
        }
        const auto& Zone = Contact->Zones[ZoneIndex];
        FVector Target = Zone.Bone.IsNone() ? FVector(155, 150, 108) :
            Character->CharacterMesh->GetSocketLocation(Zone.Bone) + Character->GetActorTransform().TransformVectorNoScale(Zone.Offset);
        Target += FVector(-Zone.Radius - 6.0f, 0, 0);
        const FTransform Raw(FQuat::Identity, Target);
        const FTransform Start(FQuat::Identity, Target + FVector(-100, 0, 0));
        const FTransform Visual = Contact->ConstrainHand(Start, Raw);
        const bool Held = MetricsSeconds >= 3.0f && MetricsSeconds < SoakDuration - 5.0f
            && FMath::Fmod(MetricsSeconds - 3.0f, 3.0f) < 2.0f;
        const int32 ContactHand = (Cycle / Contact->Zones.Num()) % 2;
        Contact->SetHandSample(true, Raw, Visual, Held && (ContactHand == 0 || Cycle % 3 == 0));
        Contact->SetHandSample(false, Raw, Visual, Held && (ContactHand == 1 || Cycle % 3 == 0));
        if (Character->SecondaryMotion->HasFault()) bTestFailed = true;
        for (const FTransform& Bone : Character->CharacterMesh->GetComponentSpaceTransforms())
            if (Bone.ContainsNaN()) bTestFailed = true;
    }
    if (MetricsInterval >= 1.0f)
    {
        MetricsInterval = 0.0f;
        const auto* Contact = Runtime.TestCharacter.IsValid() ? Runtime.TestCharacter->Interaction.Get() : nullptr;
        const auto* Physics = Runtime.TestCharacter.IsValid() ? Runtime.TestCharacter->SecondaryMotion.Get() : nullptr;
        PerfRows += FString::Printf(TEXT("%.3f,%d,%d,%.3f,%.3f,%.3f,%.3f,%d,%d\n"),
            MetricsSeconds, Runtime.bXRActive ? 1 : 0, Contact ? Contact->Quality : 1,
            Runtime.EstimatedFrameMs, Runtime.GameThreadMs, Runtime.RenderThreadMs, Runtime.GPUFrameMs,
            Physics ? Physics->GetActiveBodyCount() : 0, Physics && Physics->HasFault() ? 1 : 0);
        if (FMath::FloorToInt(MetricsSeconds) % 30 == 0)
            UE_LOG(LogGratiaVerification, Display, TEXT("METRICS %.1fs frame=%.2fms GT=%.2f RT=%.2f GPU=%.2f XR=%d physics=%d"),
                MetricsSeconds, Runtime.EstimatedFrameMs, Runtime.GameThreadMs, Runtime.RenderThreadMs, Runtime.GPUFrameMs, Runtime.bXRActive ? 1 : 0,
                Physics ? Physics->GetActiveBodyCount() : 0);
    }
    const float Duration = SoakDuration > 0.0f ? SoakDuration : PerfDuration;
    if (MetricsSeconds < Duration) return;
    bMetricsFinished = true;
    const FString MetricsPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GratiaMetrics.csv"));
    FFileHelper::SaveStringToFile(PerfRows, *MetricsPath);
    UE_LOG(LogGratiaVerification, Display, TEXT("METRICS saved=%s duration=%.1f"), *MetricsPath, MetricsSeconds);
    if (SoakDuration > 0.0f)
    {
        if (Runtime.TestCharacter.IsValid())
        {
            auto* Contact = Runtime.TestCharacter->Interaction.Get();
            for (const auto& Zone : Contact->Zones)
                UE_LOG(LogGratiaVerification, Display, TEXT("SOAK zone=%s reactions=%d state=%d"), *Zone.Name.ToString(), Zone.Reactions, int32(Zone.State));
            TestCheck(Contact->Reaction < 0.01f && Contact->ActiveZone == INDEX_NONE, TEXT("Soak returns to neutral after final release"));
            TestCheck(!Runtime.TestCharacter->SecondaryMotion->HasFault(), TEXT("Soak secondary physics remains finite and bounded"));
        }
        else bTestFailed = true;
        UE_LOG(LogGratiaVerification, Display, TEXT("GRATIA_SOAK_%s seconds=%.1f cycles=%d"), bTestFailed ? TEXT("FAIL") : TEXT("PASS"), MetricsSeconds, SoakCycle);
        FPlatformMisc::RequestExitWithStatus(false, bTestFailed ? 1 : 0, TEXT("GratiaSoak"));
    }
}
