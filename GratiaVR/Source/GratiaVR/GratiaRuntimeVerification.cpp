#include "GratiaRuntimeVerification.h"
#include "GratiaStage1Runtime.h"
#include "GratiaStage1HUD.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaLocomotion.h"
#include "GratiaInteraction.h"
#include "GratiaReactionPresentation.h"
#include "GratiaMenu.h"
#include "GratiaSecondaryMotion.h"
#include "GratiaClothInteraction.h"
#include "GratiaCharacterProfile.h"
#include "GratiaContactSolver.h"
#include "GratiaAnimInstance.h"
#include "GratiaHandInput.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
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
#include "PhysicsEngine/PhysicsAsset.h"
#include "InputKeyEventArgs.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedInputDeveloperSettings.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaVerification, Log, All);

namespace
{
    const FName CharacterTestSemantics[] = {
        TEXT("Head"), TEXT("LeftHand"), TEXT("RightHand"),
        TEXT("LeftFoot"), TEXT("RightFoot"), TEXT("Root")
    };

    bool VerificationIsFiniteTransform(const FTransform& Transform)
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
    bClothQA = FParse::Param(FCommandLine::Get(), TEXT("GratiaClothQA"));
    bSelfTest = FParse::Param(FCommandLine::Get(), TEXT("GratiaSelfTest"));
    bReactionQAEnabled = FParse::Value(FCommandLine::Get(), TEXT("GratiaReactionZone="), ReactionQAZone);
    FParse::Value(FCommandLine::Get(), TEXT("GratiaExpectedReactionClip="), ReactionQAExpectedClip);
    FParse::Value(FCommandLine::Get(), TEXT("GratiaCorrectivePrefix="), ReactionQACorrectivePrefix);
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
        || !Runtime.PlayerController.IsValid() || !Runtime.PlayerPawn.IsValid() || !Runtime.TargetCharacter.IsValid()) return;
    // This camera belongs only to an explicit desktop QA invocation.
    AGratiaPreviewCharacter* Character = Runtime.TargetCharacter.Get();
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

void UGratiaRuntimeVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    if (bReactionQAStarted && GetRuntime().TargetCharacter.IsValid() && GetRuntime().TargetCharacter->Interaction)
    {
        UGratiaInteraction* Interaction = GetRuntime().TargetCharacter->Interaction;
        Interaction->OnContactReaction.RemoveDynamic(this, &UGratiaRuntimeVerification::OnReactionQAContact);
        Interaction->SetHandSample(true, FTransform::Identity, FTransform::Identity, false);
        Interaction->SetHandSample(false, FTransform::Identity, FTransform::Identity, false);
    }
    if (bInputIntegrationStarted && !bInputIntegrationDone)
    {
        AGratiaStage1Runtime& Runtime = GetRuntime();
        if (Runtime.PlayerController.IsValid())
            Runtime.PlayerController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.0f));
        if (Runtime.PlayerPawn.IsValid())
            Runtime.PlayerPawn->SetActorTransform(InputOriginalPawn, false, nullptr, ETeleportType::TeleportPhysics);
    }
    Super::EndPlay(Reason);
}

void UGratiaRuntimeVerification::TestSkip(const TCHAR* Description) const
{
    UE_LOG(LogGratiaVerification, Display, TEXT("TEST SKIP: %s"), Description);
}

void UGratiaRuntimeVerification::RunWorldChecks()
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    TestCheck(Runtime.bPawnReady, TEXT("Template pawn has camera, common tracking origin and both hand proxies"));
    TestCheck(Runtime.PlayerController.IsValid() && Runtime.PlayerController->GetHUD() && Runtime.PlayerController->GetHUD()->IsA<AGratiaStage1HUD>(),
        TEXT("Stage 1 HUD is active"));
    TestCheck(Runtime.Camera.IsValid() && VerificationIsFiniteTransform(Runtime.Camera->GetComponentTransform()), TEXT("Camera transform is finite"));
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
        ++CharacterCount;
    TestCheck(Runtime.TargetCharacter.IsValid() && Runtime.TargetCharacter->GetWorld() == GetWorld(),
        TEXT("The runtime has an explicit valid character target in this world"));
    if (!Runtime.TargetCharacter.IsValid()) return;
    AGratiaPreviewCharacter* Character = Runtime.TargetCharacter.Get();
    UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    TestCheck(Profile != nullptr, TEXT("The character target has a cooked CharacterProfile"));
    if (!Profile) return;
    FString RequestedProfilePath;
    if (FParse::Value(FCommandLine::Get(), TEXT("GratiaCharacterProfile="), RequestedProfilePath))
    {
        const UGratiaCharacterProfile* RequestedProfile = LoadObject<UGratiaCharacterProfile>(nullptr, *RequestedProfilePath);
        TestCheck(RequestedProfile && RequestedProfile == Profile,
            TEXT("The explicitly requested character profile is actually active; fallback to another model is not accepted"));
    }
    TArray<FString> ProfileErrors, ProfileWarnings;
    TestCheck(Profile->ValidateProfile(ProfileErrors, ProfileWarnings), TEXT("The selected CharacterProfile satisfies its resource/capability contract"));
    for (const FString& Error : ProfileErrors)
    {
        UE_LOG(LogGratiaVerification, Error, TEXT("PROFILE ERROR: %s"), *Error);
    }
    for (const FString& Warning : ProfileWarnings)
    {
        UE_LOG(LogGratiaVerification, Display, TEXT("PROFILE WARNING: %s"), *Warning);
    }
    ObservedCharacterProfile = Profile;

    USkeletalMeshComponent* Component = Character->CharacterMesh;
    USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
    TestCheck(Mesh && Mesh == Profile->Mesh, TEXT("The rendered skeletal mesh matches the selected CharacterProfile"));
    if (!Mesh || !Component) return;

    bool bAllClipsCompatible = true;
    int32 ProvidedClips = 0;
    for (const UAnimSequence* Clip : {Profile->Idle.Get(), Profile->Arms.Get(), Profile->Head.Get(),
        Profile->ReactSoft.Get(), Profile->ReactBright.Get()})
    {
        if (!Clip) continue;
        ++ProvidedClips;
        bAllClipsCompatible &= Clip->GetPlayLength() > 0.0f && Clip->GetSkeleton() == Mesh->GetSkeleton();
    }
    if (ProvidedClips)
        TestCheck(bAllClipsCompatible, TEXT("Every supplied profile animation is non-empty and compatible with this model's skeleton"));
    else TestSkip(TEXT("This profile supplies no animation clips; reference pose or its AnimationClass is expected"));
    if (!Profile->Capabilities.bReactionAnimations)
        TestSkip(TEXT("ReactionAnimations is disabled in the selected profile"));
    if (!Profile->Capabilities.bBlink)
        TestSkip(TEXT("Blink is disabled in the selected profile; eyelid morphs are not required"));
    if (!Profile->Capabilities.bFacialReactions)
        TestSkip(TEXT("FacialReactions is disabled in the selected profile; facial morphs are not required"));
    if (!Profile->Capabilities.bGaze)
        TestSkip(TEXT("Gaze is disabled in the selected profile"));

    UAnimSingleNodeInstance* Animation = Component->GetSingleNodeInstance();
    if (Profile->AnimationClass)
    {
        TestCheck(Component->GetAnimInstance() && Component->GetAnimInstance()->IsA(Profile->AnimationClass),
            TEXT("The configured profile AnimationClass is active"));
    }
    else if (Character->GetExpectedAnimation())
    {
        TestCheck(Animation && Animation->GetAnimationAsset() == Character->GetExpectedAnimation()
            && (Character->IsIdlePreview() ? Animation->IsPlaying() && Animation->IsLooping() : !Animation->IsPlaying()),
            TEXT("The requested profile preview clip is active with the correct playback mode"));
    }
    else TestSkip(TEXT("No preview clip is supplied for this profile; reference pose is expected"));
    TestCheck(Character->GetActorScale3D().Equals(FVector::OneVector, 0.001)
        && Component->GetComponentScale().Equals(FVector::OneVector, 0.001)
        && !Mesh->GetBounds().BoxExtent.ContainsNaN() && Mesh->GetBounds().BoxExtent.Z > 0.0,
        TEXT("The character uses unit actor/component scale and finite positive mesh bounds"));
    if (Profile->ExpectedBoneCount > 0)
        TestCheck(Mesh->GetRefSkeleton().GetNum() == Profile->ExpectedBoneCount, TEXT("Bone count matches this profile's optional regression expectation"));
    if (Profile->ExpectedMorphCount > 0)
        TestCheck(Mesh->GetMorphTargets().Num() == Profile->ExpectedMorphCount, TEXT("Morph count matches this profile's optional regression expectation"));
    UPhysicsAsset* Physics = Profile->PhysicsAsset ? Profile->PhysicsAsset.Get() : Mesh->GetPhysicsAsset();
    if (Profile->ExpectedPhysicsBodyCount > 0)
        TestCheck(Physics && Physics->SkeletalBodySetups.Num() == Profile->ExpectedPhysicsBodyCount,
            TEXT("Physics body count matches this profile's optional regression expectation"));
    if (Profile->ExpectedConstraintCount > 0)
        TestCheck(Physics && Physics->ConstraintSetup.Num() == Profile->ExpectedConstraintCount,
            TEXT("Physics constraint count matches this profile's optional regression expectation"));

    bool bBonesFinite = true;
    InitialCharacterBones.Reset();
    ObservedCharacterBones.Reset();
    for (const FName Semantic : CharacterTestSemantics)
    {
        const FName Bone = Profile->ResolveBone(Semantic);
        ObservedCharacterBones.Add(Bone);
        if (Bone.IsNone())
        {
            InitialCharacterBones.Add(FTransform::Identity);
            UE_LOG(LogGratiaVerification, Display, TEXT("TEST SKIP: no optional observation mapping for %s"), *Semantic.ToString());
            continue;
        }
        const bool bPresent = Component->GetBoneIndex(Bone) != INDEX_NONE;
        const FTransform Transform = Component->GetSocketTransform(Bone, RTS_Component);
        bBonesFinite &= bPresent && VerificationIsFiniteTransform(Transform);
        InitialCharacterBones.Add(Transform);
    }
    TestCheck(bBonesFinite && VerificationIsFiniteTransform(Character->GetActorTransform()),
        TEXT("All mapped observation bones and the character actor have finite transforms"));
    if (Profile->bRequirePlantedIdle)
        TestCheck(!Profile->ResolveBone(TEXT("LeftFoot")).IsNone() && !Profile->ResolveBone(TEXT("RightFoot")).IsNone()
            && !Profile->ResolveBone(TEXT("Root")).IsNone(), TEXT("The planted-idle contract supplies both feet and root semantic mappings"));
    InitialCharacterActor = Character->GetActorTransform();
    InitialAnimationTime = Animation ? Animation->GetCurrentTime() : 0.0f;
    UE_LOG(LogGratiaVerification, Display, TEXT("CHARACTER TEST CONFIG: actor=%s profile=%s characters=%d mode=%d clip=%s bounds_height_cm=%.2f"),
        *Character->GetName(), *Profile->ProfileId.ToString(), CharacterCount, static_cast<int32>(Character->PreviewPose), *GetNameSafe(Character->GetExpectedAnimation()),
        Mesh->GetBounds().BoxExtent.Z * 2.0);
}

void UGratiaRuntimeVerification::SampleCharacterAnimation(bool bFinish)
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    if (!Runtime.TargetCharacter.IsValid() || !Runtime.TargetCharacter->IsIdlePreview())
    {
        if (bFinish)
        {
            UE_LOG(LogGratiaVerification, Display, TEXT("Idle motion observation skipped for a fixed diagnostic pose or absent character."));
        }
        return;
    }
    USkeletalMeshComponent* Component = Runtime.TargetCharacter->CharacterMesh;
    UGratiaCharacterProfile* Profile = Runtime.TargetCharacter->CharacterProfile;
    if (!Component || !Profile || InitialCharacterBones.Num() != UE_ARRAY_COUNT(CharacterTestSemantics)
        || ObservedCharacterBones.Num() != InitialCharacterBones.Num() || ObservedCharacterProfile.Get() != Profile)
    {
        if (bFinish) TestCheck(false, TEXT("Idle motion observation has a valid baseline"));
        return;
    }
    bool bFinite = true;
    for (int32 Index = 0; Index < ObservedCharacterBones.Num(); ++Index)
    {
        if (ObservedCharacterBones[Index].IsNone()) continue;
        const FTransform Current = Component->GetSocketTransform(ObservedCharacterBones[Index], RTS_Component);
        bFinite &= VerificationIsFiniteTransform(Current);
        if (!VerificationIsFiniteTransform(Current)) continue;
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
    const FTransform ActorTransform = Runtime.TargetCharacter->GetActorTransform();
    bFinite &= VerificationIsFiniteTransform(ActorTransform);
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
        if (Profile->Idle && !Profile->AnimationClass)
            TestCheck(bAnimationTimeAdvanced, TEXT("The supplied native idle animation advances throughout the observation"));
        else TestSkip(TEXT("No native idle clip is supplied; animation-time advancement is not asserted"));
        if (Profile->bRequirePlantedIdle)
        {
            TestCheck(MaxFootDriftCm <= Profile->MaxIdleFootDriftCm && MaxFootDriftDegrees <= Profile->MaxIdleFootRotationDegrees,
                TEXT("Idle foot drift stays inside this profile's planted-idle limits"));
            TestCheck(MaxRootDriftCm <= Profile->MaxIdleRootDriftCm && MaxRootDriftDegrees <= Profile->MaxIdleRootRotationDegrees,
                TEXT("Idle actor/root drift stays inside this profile's planted-idle limits"));
        }
        else TestSkip(TEXT("This profile does not require a planted idle; drift is measured without a planted-pose assertion"));
        UE_LOG(LogGratiaVerification, Display, TEXT("CHARACTER MOTION: head=%.5fcm/%.5fdeg feet=%.5fcm/%.5fdeg root=%.5fcm/%.5fdeg"),
            MaxHeadMovementCm, MaxHeadMovementDegrees, MaxFootDriftCm, MaxFootDriftDegrees, MaxRootDriftCm, MaxRootDriftDegrees);
    }
}

void UGratiaRuntimeVerification::RunSelfChecks()
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    FString InputAssetFailure;
    TestCheck(ValidateInputAssets(InputAssetFailure),
        TEXT("Cooked input assets use registered XR keys, vector-parent bindings and no legacy template locomotion"));
    if (!InputAssetFailure.IsEmpty())
    {
        UE_LOG(LogGratiaVerification, Error, TEXT("%s"), *InputAssetFailure);
    }
    FString MovementFailure;
    TestCheck(Runtime.Locomotion && Runtime.Locomotion->RunChecks(MovementFailure), TEXT("Head-relative walking, deadzone, wall sweep, snap pivot and teleport suppression"));
    if (!MovementFailure.IsEmpty()) UE_LOG(LogGratiaVerification, Error, TEXT("%s"), *MovementFailure);
    FString SolverFailure;
    const bool bSolverPassed = GratiaContactSolver::RunRegressionChecks(SolverFailure);
    TestCheck(bSolverPassed,
        TEXT("Independent hand solver covers fast sweep, penetrating starts, overlapping proxies, sliding and pose changes"));
    if (!SolverFailure.IsEmpty())
    {
        if (bSolverPassed) { UE_LOG(LogGratiaVerification, Display, TEXT("%s"), *SolverFailure); }
        else { UE_LOG(LogGratiaVerification, Error, TEXT("%s"), *SolverFailure); }
    }
    UGratiaCharacterProfile* Profile = Runtime.TargetCharacter.IsValid() ? Runtime.TargetCharacter->CharacterProfile.Get() : nullptr;
    FString ContactFailure;
    if (Profile && Profile->Capabilities.bContacts)
        TestCheck(Runtime.TargetCharacter->Interaction && Runtime.TargetCharacter->Interaction->RunChecks(ContactFailure),
            TEXT("All enabled profile contact zones complete repeated two-hand cycles, cooldown and proxy constraint checks"));
    else TestSkip(TEXT("Contacts is disabled or the profile is absent; character-zone cycles are not run"));
    if (!ContactFailure.IsEmpty()) UE_LOG(LogGratiaVerification, Error, TEXT("%s"), *ContactFailure);
    TestCheck(Runtime.Menu && Runtime.Menu->RunChecks(), TEXT("World menu, ten pose/mood switches, reset and all quality profiles"));
    FString PhysicsFailure;
    if (Profile && Profile->Capabilities.bSecondaryPhysics)
        TestCheck(Runtime.TargetCharacter->SecondaryMotion && Runtime.TargetCharacter->SecondaryMotion->RunChecks(PhysicsFailure),
            TEXT("Enabled profile physics satisfies its asset, body budgets, controlled core and disable/reset contract"));
    else if (Profile && Runtime.TargetCharacter->SecondaryMotion)
    {
        Runtime.TargetCharacter->SecondaryMotion->RefreshSettings(true);
        TestCheck(Runtime.TargetCharacter->SecondaryMotion->GetActiveBodyCount() == 0,
            TEXT("A profile with SecondaryPhysics disabled leaves no active secondary bodies"));
        TestSkip(TEXT("SecondaryPhysics is disabled; physical asset/constraint checks are not required"));
    }
    else TestSkip(TEXT("No profile/secondary-motion component is present; physical asset checks are not run"));
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
        TestCheck(Hand.Visual.IsValid() && !Hand.Visual->GetAttachParent() && VerificationIsFiniteTransform(Hand.Visual->GetComponentTransform()),
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

bool UGratiaRuntimeVerification::ValidateInputAssets(FString& Failure) const
{
    TArray<FString> Errors;
    TSet<FString> VectorWalkKeys;
    const TSet<FString> KnownProfiles = {TEXT("OculusTouch"), TEXT("ValveIndex"), TEXT("MixedReality"), TEXT("Vive")};
    const TSet<FString> ExpectedWalkKeys = {
        TEXT("OculusTouch_Left_Thumbstick_2D"), TEXT("ValveIndex_Left_Thumbstick_2D"),
        TEXT("MixedReality_Left_Thumbstick_2D"), TEXT("Vive_Left_Trackpad_2D")
    };
    const UEnhancedInputDeveloperSettings* Settings = GetDefault<UEnhancedInputDeveloperSettings>();
    bool bMovementRegistered = false;
    if (!Settings || !Settings->bEnableDefaultMappingContexts)
        Errors.Add(TEXT("OpenXR default mapping contexts are not enabled."));
    else for (const FDefaultContextSetting& DefaultContext : Settings->DefaultMappingContexts)
    {
        const UInputMappingContext* Context = DefaultContext.InputMappingContext.LoadSynchronous();
        if (!Context)
        {
            Errors.Add(FString::Printf(TEXT("A configured input mapping context is not cooked: %s."),
                *DefaultContext.InputMappingContext.ToSoftObjectPath().ToString()));
            continue;
        }
        const bool bMovement = Context->GetFName() == TEXT("IMC_GratiaLocomotion");
        const bool bMenu = Context->GetFName() == TEXT("IMC_GratiaMenu");
        const bool bTemplate = Context->GetFName() == TEXT("IMC_Default");
        bMovementRegistered |= bMovement && DefaultContext.bAddImmediately;
        Context->ForEachKeyMapping([&](const FEnhancedActionKeyMapping& Mapping)
        {
            if (!Mapping.Action) return;
            const FString Key = Mapping.Key.ToString();
            const FString Action = Mapping.Action->GetName();
            if ((bMovement || bMenu) && !Mapping.Key.IsValid())
                Errors.Add(FString::Printf(TEXT("%s/%s uses an unregistered key %s."), *Context->GetName(), *Action, *Key));
            TArray<FString> Tokens;
            const bool bKnownXR = Key.ParseIntoArray(Tokens, TEXT("_")) == EKeys::NUM_XR_KEY_TOKENS
                && KnownProfiles.Contains(Tokens[0]);
            if (bKnownXR && Mapping.Action->ValueType == EInputActionValueType::Axis2D)
            {
                if (!Mapping.Key.IsAxis2D() || Tokens[3] != TEXT("2D"))
                    Errors.Add(FString::Printf(TEXT("%s/%s binds an OpenXR vector action to scalar path key %s."),
                        *Context->GetName(), *Action, *Key));
                if (!Mapping.Modifiers.IsEmpty())
                    Errors.Add(FString::Printf(TEXT("%s/%s modifies the complete XR vector at mapping level (%s)."),
                        *Context->GetName(), *Action, *Key));
            }
            if (bMovement && Action == TEXT("IA_Walk") && bKnownXR)
                VectorWalkKeys.Add(Key);
            if (bTemplate && (Action == TEXT("IA_Move") || Action == TEXT("IA_Turn") || Action.StartsWith(TEXT("IA_Turn_"))))
                Errors.Add(FString::Printf(TEXT("Legacy template locomotion %s remains mapped to %s."), *Action, *Key));
        });
    }
    if (!bMovementRegistered)
        Errors.Add(TEXT("The locomotion context is not registered for OpenXR session creation and immediate Enhanced Input use."));
    for (const FString& Key : ExpectedWalkKeys)
        if (!VectorWalkKeys.Contains(Key))
            Errors.Add(FString::Printf(TEXT("No cooked vector-parent walk mapping exists for %s."), *Key));
    for (const FString& Key : VectorWalkKeys)
        if (!ExpectedWalkKeys.Contains(Key))
            Errors.Add(FString::Printf(TEXT("Unexpected XR walk mapping needs a validated provider/type contract: %s."), *Key));
    Failure = FString::Join(Errors, TEXT("\n"));
    return Errors.IsEmpty();
}

void UGratiaRuntimeVerification::OnReactionQAContact(FName ZoneName, int32 HandIndex, float HandSpeed, int32 Mood)
{
    ++ReactionQAEventCount;
    UGratiaInteraction* Interaction = GetRuntime().TargetCharacter.IsValid() ? GetRuntime().TargetCharacter->Interaction.Get() : nullptr;
    ReactionQAHandIndex = HandIndex;
    ReactionQASerial = Interaction ? Interaction->ReactionSerial : 0;
    bReactionQAContactReceived = ZoneName == FName(*ReactionQAZone) && HandIndex == 0 && ReactionQASerial > 0;
    TestCheck(bReactionQAContactReceived, TEXT("Synthetic QA hand reached the requested zone through the ordinary contact event"));
    UE_LOG(LogGratiaVerification, Display,
        TEXT("REACTION QA EVENT: source=synthetic-resource-QA zone=%s hand=%d serial=%d speed=%.3f mood=%d events=%d; not real VR tracking/contact acceptance"),
        *ZoneName.ToString(), HandIndex, ReactionQASerial, HandSpeed, Mood, ReactionQAEventCount);
}

void UGratiaRuntimeVerification::RunReactionResourceQA(float DeltaSeconds)
{
    if (bReactionQACompleted) return;
    AGratiaStage1Runtime& Runtime = GetRuntime();
    if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.0f) ReactionQASeconds += DeltaSeconds;
    if (Runtime.bXRActive || bSmokeTest || bSelfTest || SoakDuration > 0.0f || PerfDuration > 0.0f)
    {
        TestCheck(false, TEXT("Reaction resource QA requires a separate desktop -nohmd invocation without smoke/selftest/soak"));
        FinishReactionResourceQA();
        return;
    }
    AGratiaPreviewCharacter* Character = Runtime.TargetCharacter.Get();
    UGratiaCharacterProfile* Profile = Character ? Character->CharacterProfile.Get() : nullptr;
    UGratiaInteraction* Interaction = Character ? Character->Interaction.Get() : nullptr;
    USkeletalMeshComponent* Component = Character ? Character->CharacterMesh.Get() : nullptr;
    UGratiaAnimInstance* Animation = Component ? Cast<UGratiaAnimInstance>(Component->GetAnimInstance()) : nullptr;
    if (!bReactionQAStarted)
    {
        if (ReactionQASeconds < 3.0f) return;
        bReactionQAStarted = true;
        UE_LOG(LogGratiaVerification, Display,
            TEXT("REACTION QA CONFIG: source=synthetic-resource-QA zone=%s expected_clip=%s corrective_prefix=%s profile=%s; visual hand injection tests resource routing, not controller input or collision surface accuracy"),
            *ReactionQAZone, *ReactionQAExpectedClip, *ReactionQACorrectivePrefix, Profile ? *Profile->GetPathName() : TEXT("MISSING"));
        TestCheck(Character && Profile && Component && Interaction && Animation && Character->IsIdlePreview(),
            TEXT("Reaction resource QA has an explicit character target, profile, native reaction animation and idle base"));
        TestCheck(Profile && Profile->Capabilities.bContacts && Profile->Capabilities.bReactionAnimations
            && Profile->Capabilities.bFacialReactions && Profile->bAuthoredReactionFacialCurves,
            TEXT("The requested resource fixture supports contacts, authored reaction clips and facial curves"));
        if (bTestFailed) { FinishReactionResourceQA(); return; }
        UAnimSequence* Expected = ReactionQAExpectedClip.IsEmpty() ? nullptr : LoadObject<UAnimSequence>(nullptr, *ReactionQAExpectedClip);
        const TObjectPtr<UAnimSequence>* RoutedClip = Profile->ReactionClips.Find(FName(*ReactionQAZone));
        TestCheck(Expected && RoutedClip && RoutedClip->Get() == Expected && Component->GetSkeletalMeshAsset()
            && Expected->GetSkeleton() == Component->GetSkeletalMeshAsset()->GetSkeleton() && Expected->GetPlayLength() > 0.0f,
            TEXT("The actual profile zone mapping references the exact expected cooked clip and compatible skeleton"));
        if (bTestFailed) { FinishReactionResourceQA(); return; }
        ReactionQAClip = Expected;
        bReactionQARequireCorrective = !ReactionQACorrectivePrefix.IsEmpty();
        Interaction->ResetState();
        Interaction->bDemo = false;
        for (int32 Index = 0; Index < Interaction->Zones.Num(); ++Index)
            if (Interaction->Zones[Index].Name == FName(*ReactionQAZone)) { ReactionQAZoneIndex = Index; break; }
        TestCheck(Interaction->Zones.IsValidIndex(ReactionQAZoneIndex) && !Interaction->Zones[ReactionQAZoneIndex].bSceneActor,
            TEXT("The requested character contact zone exists in the runtime profile"));
        if (bTestFailed) { FinishReactionResourceQA(); return; }

        // Sample cooked curve data, never editor-only raw curves. Neutral correction alone
        // cannot satisfy the dynamic correction requirement for the two new arm paths.
        for (const TPair<FName, FName>& Mapping : Profile->SemanticMorphs)
            if (!Mapping.Value.IsNone() && Expected->HasCurveData(Mapping.Value, false))
                ReactionQAFacialCurves.AddUnique(Mapping.Value);
        for (UMorphTarget* Morph : Component->GetSkeletalMeshAsset()->GetMorphTargets())
        {
            if (!Morph) continue;
            const FName Name = Morph->GetFName();
            if (bReactionQARequireCorrective && Name.ToString().StartsWith(ReactionQACorrectivePrefix)
                && !Name.ToString().Contains(TEXT("Neutral")) && Expected->HasCurveData(Name, false))
                ReactionQACorrectiveCurves.Add(Name);
        }
        bool bCurvesFinite = true;
        float BestFaceScore = -1.0f;
        const FName Head = Profile->ResolveBone(TEXT("Head"));
        const int32 HeadIndex = Expected->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Head);
        FTransform HeadStart = FTransform::Identity;
        if (HeadIndex != INDEX_NONE)
            Expected->GetBoneTransform(HeadStart, FSkeletonPoseBoneIndex(HeadIndex), FAnimExtractContext(0.0), false);
        for (int32 Sample = 0; Sample <= 60; ++Sample)
        {
            const float Time = Expected->GetPlayLength() * Sample / 60.0f;
            const FAnimExtractContext Context(Time);
            float FacePeak = 0.0f, ExpressiveScore = 0.0f;
            for (const FName Name : ReactionQAFacialCurves)
            {
                const float Value = Expected->EvaluateCurveData(Name, Context, false);
                bCurvesFinite &= FMath::IsFinite(Value);
                if (FMath::IsFinite(Value)) FacePeak = FMath::Max(FacePeak, FMath::Abs(Value));
            }
            ReactionQACookedFacialPeak = FMath::Max(ReactionQACookedFacialPeak, FacePeak);
            for (const TPair<FName, FName>& Mapping : Profile->SemanticMorphs)
            {
                const FString Semantic = Mapping.Key.ToString();
                if (Semantic.StartsWith(TEXT("Blink")) || Semantic.StartsWith(TEXT("Look")) || Mapping.Value.IsNone()) continue;
                const float Value = Expected->EvaluateCurveData(Mapping.Value, Context, false);
                if (FMath::IsFinite(Value)) ExpressiveScore += FMath::Abs(Value);
            }
            for (const FName Name : ReactionQACorrectiveCurves)
            {
                const float Value = Expected->EvaluateCurveData(Name, Context, false);
                bCurvesFinite &= FMath::IsFinite(Value);
                if (FMath::IsFinite(Value)) ReactionQACookedCorrectivePeak = FMath::Max(ReactionQACookedCorrectivePeak, FMath::Abs(Value));
            }
            if (HeadIndex != INDEX_NONE)
            {
                FTransform HeadAtTime = FTransform::Identity;
                Expected->GetBoneTransform(HeadAtTime, FSkeletonPoseBoneIndex(HeadIndex), Context, false);
                bCurvesFinite &= VerificationIsFiniteTransform(HeadAtTime) && VerificationIsFiniteTransform(HeadStart);
                ReactionQAHeadRotationDegrees = FMath::Max(ReactionQAHeadRotationDegrees,
                    float(FMath::RadiansToDegrees(HeadStart.GetRotation().AngularDistance(HeadAtTime.GetRotation()))));
            }
            // Prefer a fully blended expressive frame, rather than either fade boundary.
            if (Time >= 0.25f && Time <= Expected->GetPlayLength() - 0.3f && ExpressiveScore > BestFaceScore)
            {
                BestFaceScore = ExpressiveScore;
                ReactionQACaptureTime = Time;
            }
        }
        TestCheck(bCurvesFinite && ReactionQACookedFacialPeak > 0.01f,
            TEXT("The imported cooked clip contains finite nonzero facial morph curves mapped by this profile"));
        if (bReactionQARequireCorrective)
            TestCheck(ReactionQACookedCorrectivePeak > 0.01f,
                TEXT("The imported cooked clip contains nonzero dynamic corrective curves with the requested prefix"));
        else
            TestCheck(ReactionQAHeadRotationDegrees > 0.1f,
                TEXT("The facial response clip contains actual semantic head bone motion"));
        UE_LOG(LogGratiaVerification, Display,
            TEXT("REACTION QA RESOURCE: clip=%s duration=%.3f facial_curves=%d facial_peak=%.6f corrective_curves=%d corrective_peak=%.6f head_motion_deg=%.4f capture_time=%.3f samples=61 raw_data=false"),
            *Expected->GetPathName(), Expected->GetPlayLength(), ReactionQAFacialCurves.Num(), ReactionQACookedFacialPeak,
            ReactionQACorrectiveCurves.Num(), ReactionQACookedCorrectivePeak, ReactionQAHeadRotationDegrees, ReactionQACaptureTime);
        if (bTestFailed) { FinishReactionResourceQA(); return; }

        const FGratiaContactZone& TargetZone = Interaction->Zones[ReactionQAZoneIndex];
        const FVector Center = Interaction->GetZoneWorldPosition(ReactionQAZoneIndex);
        const double Scale = Character->GetActorScale3D().GetAbsMax();
        const double Reach = TargetZone.Radius * Scale + TargetZone.Settings.TouchPaddingCm - 0.25;
        bool bFoundPoint = false;
        double BestClearance = -UE_BIG_NUMBER;
        for (int32 X = -3; X <= 3; ++X)
            for (int32 Y = -3; Y <= 3; ++Y)
                for (int32 Z = -3; Z <= 3; ++Z)
                {
                    const FVector Offset = FVector(X, Y, Z) * (Reach / 3.0);
                    if (Offset.Size() > Reach) continue;
                    const FVector Point = Center + Offset;
                    double Clearance = Reach - Offset.Size();
                    bool bWins = true;
                    for (int32 Other = 0; Other < Interaction->Zones.Num(); ++Other)
                    {
                        const FGratiaContactZone& Zone = Interaction->Zones[Other];
                        if (Other == ReactionQAZoneIndex || Zone.bSceneActor || Zone.Priority > TargetZone.Priority
                            || Zone.Priority == TargetZone.Priority && Other > ReactionQAZoneIndex) continue;
                        const double Gap = FVector::Distance(Point, Interaction->GetZoneWorldPosition(Other))
                            - Zone.Radius * Scale - Zone.Settings.TouchPaddingCm;
                        Clearance = FMath::Min(Clearance, Gap);
                        if (Gap <= 0.25) { bWins = false; break; }
                    }
                    if (bWins && Clearance > BestClearance)
                    {
                        bFoundPoint = true; BestClearance = Clearance; ReactionQATouchOffset = Offset;
                    }
                }
        TestCheck(bFoundPoint, TEXT("A synthetic sample lies in the requested zone outside every competing higher-priority zone"));
        if (bTestFailed) { FinishReactionResourceQA(); return; }
        Interaction->OnContactReaction.AddDynamic(this, &UGratiaRuntimeVerification::OnReactionQAContact);
        UE_LOG(LogGratiaVerification, Display, TEXT("REACTION QA SAMPLE: zone=%s offset_cm=%s priority_clearance_cm=%.3f"),
            *ReactionQAZone, *ReactionQATouchOffset.ToString(), BestClearance);
    }
    if (!Character || !Profile || !Interaction || !Animation || !ReactionQAClip.IsValid())
    {
        TestCheck(false, TEXT("Reaction resource QA target and animation remain valid during playback"));
        FinishReactionResourceQA(); return;
    }
    // Runtime updates untracked desktop hands first; this explicit opt-in synthetic
    // sample is then consumed by the normal interaction tick before animation evaluation.
    const FVector Point = Interaction->GetZoneWorldPosition(ReactionQAZoneIndex) + ReactionQATouchOffset;
    const FTransform Touch(FQuat::Identity, Point);
    Interaction->SetHandSample(true, Touch, Touch, !bReactionQAContactReceived);
    Interaction->SetHandSample(false, FTransform::Identity, FTransform::Identity, false);
    if (bTestFailed) { FinishReactionResourceQA(); return; }
    if (bReactionQAContactReceived && Animation->LastReactionSerial == static_cast<uint32>(ReactionQASerial))
    {
        if (!bReactionQAClipObserved)
        {
            bReactionQAClipObserved = true;
            TestCheck(Animation->ReactionClip == ReactionQAClip.Get() && Animation->IsReactionCuePlaying(),
                TEXT("The normal animation instance selected and plays the exact requested clip after the contact serial"));
            if (Character->ReactionPresentation && Character->ReactionPresentation->bPresentCaptions && Profile->ContactSettings.CaptionSeconds > 0.0f)
                TestCheck(Character->ReactionPresentation->IsCaptionVisible()
                    && Character->ReactionPresentation->GetCaptionText().Contains(ReactionQAZone),
                    TEXT("The event subscriber presents the actual contact zone caption"));
            UE_LOG(LogGratiaVerification, Display, TEXT("REACTION QA SELECTED: zone=%s serial=%d clip=%s time=%.4f duration=%.4f"),
                *ReactionQAZone, ReactionQASerial, *GetPathNameSafe(Animation->ReactionClip), Animation->ReactionTime, Animation->ReactionClipDuration);
        }
        bool bEvaluatedFinite = true;
        for (const FName Name : ReactionQAFacialCurves)
        {
            const float Value = Animation->GetCurveValue(Name);
            bEvaluatedFinite &= FMath::IsFinite(Value);
            if (FMath::IsFinite(Value)) ReactionQAEvaluatedFacialPeak = FMath::Max(ReactionQAEvaluatedFacialPeak, FMath::Abs(Value));
        }
        for (const FName Name : ReactionQACorrectiveCurves)
        {
            const float Value = Animation->GetCurveValue(Name);
            bEvaluatedFinite &= FMath::IsFinite(Value);
            if (FMath::IsFinite(Value)) ReactionQAEvaluatedCorrectivePeak = FMath::Max(ReactionQAEvaluatedCorrectivePeak, FMath::Abs(Value));
        }
        if (!bEvaluatedFinite) TestCheck(false, TEXT("The blended reaction evaluation stays finite"));
        if (!bReactionQAScreenshotRequested && Animation->ReactionTime >= ReactionQACaptureTime)
        {
            TestCheck(FApp::CanEverRender() && GEngine && GEngine->GameViewport,
                TEXT("The packaged reaction QA has a viewport for its expressive capture"));
            if (bTestFailed) { FinishReactionResourceQA(); return; }
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/Windows/GratiaReactionQA.png")),
                false, true, false);
            ReactionQAScreenshotPath = FScreenshotRequest::GetFilename();
            bReactionQAScreenshotRequested = true;
            ReactionQAScreenshotSeconds = ReactionQASeconds;
            FName BlendObservationCurve;
            float SourceCurveSample = 0.0f, BlendedCurveSample = 0.0f;
            for (const FName Name : ReactionQAFacialCurves)
            {
                const float Source = ReactionQAClip->EvaluateCurveData(Name, FAnimExtractContext(Animation->ReactionTime), false);
                if (FMath::IsFinite(Source) && FMath::Abs(Source) > FMath::Abs(SourceCurveSample))
                {
                    BlendObservationCurve = Name; SourceCurveSample = Source; BlendedCurveSample = Animation->GetCurveValue(Name);
                }
            }
            UE_LOG(LogGratiaVerification, Display,
                TEXT("REACTION QA CAPTURE: serial=%d clip=%s sampled_time=%.4f duration=%.4f contact_response_weight=%.4f facial_blended_peak=%.6f corrective_blended_peak=%.6f curve=%s source_curve_sample=%.6f blended_curve_sample=%.6f observed_curve_ratio=%.4f screenshot=%s; desktop synthetic resource QA; observed ratio includes base pose and evaluation timing"),
                ReactionQASerial, *GetPathNameSafe(Animation->ReactionClip), Animation->ReactionTime, Animation->ReactionClipDuration,
                Interaction->Reaction, ReactionQAEvaluatedFacialPeak, ReactionQAEvaluatedCorrectivePeak, *BlendObservationCurve.ToString(),
                SourceCurveSample, BlendedCurveSample, FMath::Abs(SourceCurveSample) > 0.001f ? BlendedCurveSample / SourceCurveSample : 0.0f,
                *ReactionQAScreenshotPath);
        }
        if (bReactionQAScreenshotRequested && !Animation->IsReactionCuePlaying()
            && ReactionQASeconds >= ReactionQAScreenshotSeconds + 0.5f)
        {
            FinishReactionResourceQA(); return;
        }
    }
    if (ReactionQASeconds >= 20.0f)
    {
        UE_LOG(LogGratiaVerification, Error, TEXT("REACTION QA TIMEOUT: %s"), *Interaction->GetContactDiagnostics());
        TestCheck(false, TEXT("The ordinary contact event, clip playback and screenshot complete before the reaction QA timeout"));
        FinishReactionResourceQA();
    }
}

void UGratiaRuntimeVerification::FinishReactionResourceQA()
{
    if (bReactionQACompleted) return;
    bReactionQACompleted = true;
    if (!bTestFailed)
    {
        TestCheck(bReactionQAContactReceived && ReactionQAEventCount == 1 && ReactionQAHandIndex == 0 && ReactionQASerial > 0,
            TEXT("One ordinary contact event from the explicitly synthetic hand triggered this QA run"));
        TestCheck(bReactionQAClipObserved && ReactionQAEvaluatedFacialPeak > 0.01f,
            TEXT("The selected reaction contributes nonzero facial curves to the evaluated blended pose"));
        if (bReactionQARequireCorrective)
            TestCheck(ReactionQAEvaluatedCorrectivePeak > 0.01f,
                TEXT("The selected reaction contributes nonzero dynamic corrective curves to the evaluated blended pose"));
        TestCheck(bReactionQAScreenshotRequested && IFileManager::Get().FileSize(*ReactionQAScreenshotPath) > 0,
            TEXT("The expressive packaged desktop screenshot was saved"));
    }
    if (GetRuntime().TargetCharacter.IsValid() && GetRuntime().TargetCharacter->Interaction)
    {
        UGratiaInteraction* Interaction = GetRuntime().TargetCharacter->Interaction;
        Interaction->OnContactReaction.RemoveDynamic(this, &UGratiaRuntimeVerification::OnReactionQAContact);
        Interaction->SetHandSample(true, FTransform::Identity, FTransform::Identity, false);
        Interaction->SetHandSample(false, FTransform::Identity, FTransform::Identity, false);
        Interaction->ResetState();
        if (GetRuntime().TargetCharacter->ReactionPresentation)
            TestCheck(!GetRuntime().TargetCharacter->ReactionPresentation->IsCaptionVisible()
                && GetRuntime().TargetCharacter->ReactionPresentation->GetCaptionText().IsEmpty(),
                TEXT("Resetting the contact source clears its event subscriber caption"));
    }
    UE_LOG(LogGratiaVerification, Display,
        TEXT("REACTION QA RESULT: zone=%s expected_clip=%s serial=%d events=%d cooked_facial_peak=%.6f evaluated_facial_peak=%.6f cooked_corrective_peak=%.6f evaluated_corrective_peak=%.6f elapsed=%.3f"),
        *ReactionQAZone, *ReactionQAExpectedClip, ReactionQASerial, ReactionQAEventCount, ReactionQACookedFacialPeak,
        ReactionQAEvaluatedFacialPeak, ReactionQACookedCorrectivePeak, ReactionQAEvaluatedCorrectivePeak, ReactionQASeconds);
    if (bTestFailed)
    {
        UE_LOG(LogGratiaVerification, Error, TEXT("GRATIA_REACTION_QA_FAIL"));
    }
    else
    {
        UE_LOG(LogGratiaVerification, Display, TEXT("GRATIA_REACTION_QA_PASS"));
    }
    FPlatformMisc::RequestExitWithStatus(false, bTestFailed ? 1 : 0, TEXT("GratiaReactionResourceQA"));
}

void UGratiaRuntimeVerification::RunRequestedTests(float DeltaSeconds)
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    if (bReactionQAEnabled)
    {
        RunReactionResourceQA(DeltaSeconds);
        return;
    }
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
    if (bTestChecksDone && bSelfTest) RunInputIntegration();
    if (bCharacterMotionChecksDone && bSelfTest)
    {
        RunPhysicsResponseProbe(DeltaSeconds);
        if (PhysicsProbeGroup > 4) RunHandPhysicsIntegration();
    }
    if (bTestChecksDone && bCharacterMotionChecksDone && TestElapsedSeconds >= 8.0f && !bRecenterTestPending
        && (!bSelfTest || (bInputIntegrationDone && HandPhysicsQAPhase == 7)))
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

void UGratiaRuntimeVerification::RunPhysicsResponseProbe(float Delta)
{
    if (PhysicsProbeGroup > 4) return;
    auto* Character = GetRuntime().TargetCharacter.Get();
    if (!Character || !Character->CharacterProfile || !Character->SecondaryMotion
        || !Character->CharacterProfile->Capabilities.bSecondaryPhysics) { PhysicsProbeGroup = 5; return; }
    auto* Mesh = Character->CharacterMesh.Get();
    if (PhysicsProbeBone.IsNone())
    {
        float Longest = -1;
        for (FName Name : Character->SecondaryMotion->GetActiveBones())
        {
            const auto* Definition = Character->CharacterProfile->FindSecondaryBone(Name);
            if (Definition && Definition->Group == PhysicsProbeGroup && Definition->RestLengthCm > Longest)
            { Longest = Definition->RestLengthCm; PhysicsProbeBone = Name; }
        }
        if (PhysicsProbeBone.IsNone()) { TestSkip(TEXT("Physics group disabled by current quality")); ++PhysicsProbeGroup; return; }
        FBodyInstance* Body = Mesh->GetBodyInstance(PhysicsProbeBone);
        PhysicsProbeBodyStart = Body->GetUnrealWorldTransform().GetRotation();
        PhysicsProbeVisualStart = Mesh->GetSocketQuaternion(PhysicsProbeBone);
        PhysicsProbeBodyDegrees = PhysicsProbeVisualDegrees = 0; PhysicsProbeSeconds = 0;
        UE_LOG(LogGratiaVerification, Display, TEXT("PHYSICS_PROBE scale=%s bone_scale=%s"), *Body->Scale3D.ToString(), *Mesh->GetSocketTransform(PhysicsProbeBone).GetScale3D().ToString());
        // Explicit diagnostic angular velocity pulse, unrelated to demo reactions or XR.
        Body->AddAngularImpulseInRadians(FVector(4, 0, 0), true);
        UE_LOG(LogGratiaVerification, Display, TEXT("PHYSICS_PROBE source=synthetic group=%d bone=%s body=%s visual=%s bounds=%s blend=%.2f"),
            PhysicsProbeGroup, *PhysicsProbeBone.ToString(), *Body->GetUnrealWorldTransform().GetLocation().ToString(),
            *Mesh->GetSocketLocation(PhysicsProbeBone).ToString(), *Body->GetBodyBounds().GetSize().ToString(), Body->PhysicsBlendWeight);
        return;
    }
    PhysicsProbeSeconds += FMath::Clamp(Delta, 0.0f, 0.1f);
    FBodyInstance* Body = Mesh->GetBodyInstance(PhysicsProbeBone);
    PhysicsProbeBodyDegrees = FMath::Max(PhysicsProbeBodyDegrees, FMath::RadiansToDegrees(PhysicsProbeBodyStart.AngularDistance(Body->GetUnrealWorldTransform().GetRotation())));
    PhysicsProbeVisualDegrees = FMath::Max(PhysicsProbeVisualDegrees, FMath::RadiansToDegrees(PhysicsProbeVisualStart.AngularDistance(Mesh->GetSocketQuaternion(PhysicsProbeBone))));
    if (PhysicsProbeSeconds < 0.4f) return;
    UE_LOG(LogGratiaVerification, Display, TEXT("PHYSICS_PROBE group=%d body_degrees=%.4f visual_degrees=%.4f"), PhysicsProbeGroup, PhysicsProbeBodyDegrees, PhysicsProbeVisualDegrees);
    TestCheck(PhysicsProbeBodyDegrees > 0.25 && PhysicsProbeVisualDegrees > 0.1 && !Character->SecondaryMotion->HasFault(),
        TEXT("Physical group moves its Chaos body and visible skeletal bone after an angular pulse"));
    ++PhysicsProbeGroup; PhysicsProbeBone = NAME_None;
}

void UGratiaRuntimeVerification::RunHandPhysicsIntegration()
{
    if (HandPhysicsQAPhase == 7) return;
    auto* Character = GetRuntime().TargetCharacter.Get();
    if (!Character || !Character->SecondaryMotion || !Character->CharacterProfile) { HandPhysicsQAPhase = 7; return; }
    auto* Physics = Character->SecondaryMotion.Get();
    const auto& Settings = Character->CharacterProfile->HandPhysics;
    if (!Character->CharacterProfile->Capabilities.bSecondaryPhysics || !Settings.bEnabled)
    { TestSkip(TEXT("Hand pressure unavailable in this profile")); HandPhysicsQAPhase = 7; return; }
    if (HandPhysicsQAPhase == 0)
    {
        TestCheck(!Physics->GetActiveBones().IsEmpty(), TEXT("Hand pressure has active driven Chaos bodies"));
        if (Physics->GetActiveBones().IsEmpty()) { HandPhysicsQAPhase = 7; return; }
        HandPhysicsQABone = Physics->GetActiveBones().Last();
        FBodyInstance* Body = Character->CharacterMesh->GetBodyInstance(HandPhysicsQABone);
        if (!Body) { TestCheck(false, TEXT("Hand pressure test body exists")); HandPhysicsQAPhase = 7; return; }
        HandPhysicsBefore = Body->GetUnrealWorldTransform().GetLocation();
        HandPhysicsTargets[0] = Body->GetBodyBounds().GetCenter();
        HandPhysicsTargets[1] = HandPhysicsTargets[0];
        double Farthest = 0;
        for (FName Name : Physics->GetActiveBones())
        {
            if (FBodyInstance* Candidate = Character->CharacterMesh->GetBodyInstance(Name))
            {
                const FVector Point = Candidate->GetBodyBounds().GetCenter();
                const double Distance = FVector::DistSquared(Point, HandPhysicsTargets[0]);
                if (Distance > Farthest) { Farthest = Distance; HandPhysicsTargets[1] = Point; }
            }
        }
        UE_LOG(LogGratiaVerification, Display, TEXT("HAND_PHYSICS_QA source=synthetic bone=%s"), *HandPhysicsQABone.ToString());
        for (int32 Index = 0; Index < 2; ++Index)
        {
            HandPhysicsBaseline[Index] = Physics->GetHandPushCount(Index == 0);
            const FVector Axis = Index == 0 ? FVector::RightVector : FVector::ForwardVector;
            Physics->SubmitHand(Index == 0, HandPhysicsTargets[Index] + Axis * Settings.RadiusCm * 2, true, 1.0f / 90);
            Physics->SubmitHand(Index == 0, HandPhysicsTargets[Index] + Axis * Settings.RadiusCm * 0.25f, true, 1.0f / 90);
        }
        HandPhysicsQAPhase = 1;
    }
    else if (HandPhysicsQAPhase == 1)
    {
        for (int32 Index = 0; Index < 2; ++Index)
        {
            TestCheck(Physics->GetHandPushCount(Index == 0) > HandPhysicsBaseline[Index],
                Index == 0 ? TEXT("Left synthetic hand swept and pushed Chaos bodies") : TEXT("Right synthetic hand swept and pushed Chaos bodies"));
            HandPhysicsBaseline[Index] = Physics->GetHandPushCount(Index == 0);
            Physics->SubmitHand(Index == 0, HandPhysicsBefore, true, 1.0f / 90);
            Physics->SubmitHand(Index == 0, HandPhysicsBefore, false, 1.0f / 90);
        }
        FBodyInstance* Body = Character->CharacterMesh->GetBodyInstance(HandPhysicsQABone);
        const double Travel = Body ? FVector::Distance(HandPhysicsBefore, Body->GetUnrealWorldTransform().GetLocation()) : 0;
        TestCheck(FMath::IsFinite(Travel) && Travel > 0.000001 && !Physics->HasFault(), TEXT("Chaos test body moved after hand pressure without a safety fault"));
        UE_LOG(LogGratiaVerification, Display, TEXT("HAND_PHYSICS_QA displacement_cm=%.6f"), Travel);
        HandPhysicsQAPhase = 2;
    }
    else if (HandPhysicsQAPhase == 2)
    {
        TestCheck(Physics->GetHandPushCount(true) == HandPhysicsBaseline[0] && Physics->GetHandPushCount(false) == HandPhysicsBaseline[1],
            TEXT("Revoked hand samples cancel pending physical pressure on both hands"));
        Physics->ClearHands();
        if (!Settings.bAllowGrab) { TestSkip(TEXT("Grab disabled by profile")); HandPhysicsQAPhase = 7; return; }
        for (bool Left : {true, false}) Physics->SubmitHand(Left, HandPhysicsTargets[Left ? 0 : 1], true, 1.0f / 90, 0);
        auto* PC = GetRuntime().PlayerController.Get();
        if (!PC) { TestCheck(false, TEXT("Grab QA has player input")); HandPhysicsQAPhase = 7; return; }
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Z, IE_Pressed, 1));
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::X, IE_Pressed, 1));
        HandPhysicsQAPhase = 3;
    }
    else if (HandPhysicsQAPhase == 3)
    {
        for (bool Left : {true, false})
        {
            const float Trigger = GetRuntime().HandInput->GetTrigger(Left);
            TestCheck(Trigger >= 0.65f, Left ? TEXT("Z maps through bound left grab action") : TEXT("X maps through bound right grab action"));
            Physics->SubmitHand(Left, HandPhysicsTargets[Left ? 0 : 1], true, 1.0f / 90, Trigger);
        }
        HandPhysicsQAPhase = 4;
    }
    else if (HandPhysicsQAPhase == 4)
    {
        TestCheck(!Physics->GetGrabbedBone(true).IsNone() && !Physics->GetGrabbedBone(false).IsNone()
            && Physics->GetGrabbedBone(true) != Physics->GetGrabbedBone(false), TEXT("Both triggers acquire distinct active physical bodies"));
        for (bool Left : {true, false}) Physics->SubmitHand(Left, HandPhysicsTargets[Left ? 0 : 1] + FVector(0, 0, 1), true, 1.0f / 90, 1);
        GetRuntime().PlayerController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Z, IE_Released, 0));
        GetRuntime().PlayerController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::X, IE_Released, 0));
        HandPhysicsQAPhase = 5;
    }
    else if (HandPhysicsQAPhase == 5)
    {
        TestCheck(!Physics->GetGrabbedBone(true).IsNone() && !Physics->GetGrabbedBone(false).IsNone(), TEXT("Both grabs persist while hand target moves"));
        TestCheck(GetRuntime().HandInput->GetTrigger(true) == 0 && GetRuntime().HandInput->GetTrigger(false) == 0, TEXT("Released keys clear both bound trigger actions"));
        Physics->SubmitHand(true, HandPhysicsBefore, true, 1.0f / 90, GetRuntime().HandInput->GetTrigger(true));
        Physics->SubmitHand(false, HandPhysicsBefore, false, 1.0f / 90, 1);
        TestCheck(Physics->GetGrabbedBone(true).IsNone() && Physics->GetGrabbedBone(false).IsNone(), TEXT("Trigger release and tracking loss clear grabs immediately"));
        Physics->SubmitHand(false, HandPhysicsBefore, true, 1.0f / 90, 1);
        Physics->SubmitHand(false, HandPhysicsBefore, true, 1.0f / 90, 1);
        HandPhysicsQAPhase = 6;
    }
    else
    {
        TestCheck(Physics->GetGrabbedBone(false).IsNone(), TEXT("Tracking recovery with a held trigger cannot reacquire"));
        Physics->ClearHands(); HandPhysicsQAPhase = 7;
    }
}

void UGratiaRuntimeVerification::RunInputIntegration()
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    if (bInputIntegrationDone) return;
    if (Runtime.bXRActive)
    {
        // Never inject player movement into a real headset test or soak session.
        TestSkip(TEXT("Synthetic keyboard movement is disabled in XR; this integration test does not prove OpenXR controller input"));
        bInputIntegrationDone = true;
        return;
    }
    if (bRecenterTestPending || Runtime.bPendingRecenter) return;
    APlayerController* PC = Runtime.PlayerController.Get();
    APawn* Pawn = Runtime.PlayerPawn.Get();
    UGratiaLocomotion* Movement = Runtime.Locomotion;
    if (!PC || !Pawn || !Movement || !Movement->IsReady())
    {
        if (PC && bInputIntegrationStarted)
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.0f));
        if (Pawn && bInputIntegrationStarted)
            Pawn->SetActorTransform(InputOriginalPawn, false, nullptr, ETeleportType::TeleportPhysics);
        TestCheck(false, TEXT("Enhanced-input integration has a ready player, pawn and locomotion component"));
        bInputIntegrationDone = true;
        return;
    }
    if (!bInputIntegrationStarted)
    {
        InputOriginalPawn = Pawn->GetActorTransform();
        bInputEventAccepted = PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
        bInputIntegrationStarted = true;
        UE_LOG(LogGratiaVerification, Display, TEXT("INPUT INTEGRATION: simulated W -> cooked mapping -> bound IA_Walk -> normal locomotion; OpenXR hardware is outside this test"));
        return;
    }
    ++InputIntegrationFrames;
    if (!bInputIntegrationReleased)
    {
        bObservedKeyboardKey |= PC->IsInputKeyDown(EKeys::W);
        bObservedMappedWalk |= Movement->MappedStick.Y > Movement->StickDeadZone;
        ObservedInputTravelCm = FMath::Max(ObservedInputTravelCm,
            FVector::Dist2D(Pawn->GetActorLocation(), InputOriginalPawn.GetLocation()));
        if (InputIntegrationFrames >= 6)
        {
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.0f));
            bInputIntegrationReleased = true;
            InputIntegrationFrames = 0;
        }
        return;
    }
    // Allow the release event to travel through PlayerInput and both tick orders.
    if (InputIntegrationFrames < 3) return;
    TestCheck(bInputEventAccepted && bObservedKeyboardKey, TEXT("A simulated keyboard press enters the player's real key state"));
    TestCheck(bObservedMappedWalk, TEXT("The cooked keyboard mapping produces the bound IA_Walk forward value"));
    TestCheck(ObservedInputTravelCm > 0.1, TEXT("Bound IA_Walk moves the real pawn through normal locomotion across several frames"));
    TestCheck(!PC->IsInputKeyDown(EKeys::W) && Movement->MappedStick.IsNearlyZero(0.001)
        && Movement->LastPawnDelta.IsNearlyZero(0.001), TEXT("Keyboard release clears the bound walk action and stops normal pawn movement"));
    UE_LOG(LogGratiaVerification, Display, TEXT("INPUT INTEGRATION RESULT: key=%d mapped=%d distance_cm=%.3f release_reason=%s"),
        bObservedKeyboardKey, bObservedMappedWalk, ObservedInputTravelCm, *Movement->MovementReason);
    Pawn->SetActorTransform(InputOriginalPawn, false, nullptr, ETeleportType::TeleportPhysics);
    bInputIntegrationDone = true;
}

void UGratiaRuntimeVerification::RunSoakAndMetrics(float DeltaSeconds)
{
    AGratiaStage1Runtime& Runtime = GetRuntime();
    if (bMetricsFinished || (SoakDuration <= 0.0f && PerfDuration <= 0.0f)
        || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
    MetricsSeconds += DeltaSeconds;
    MetricsInterval += DeltaSeconds;
    if (SoakDuration > 0.0f && Runtime.TargetCharacter.IsValid())
    {
        auto* Character = Runtime.TargetCharacter.Get();
        const auto* Profile = Character->CharacterProfile.Get();
        auto* Contact = Character->Interaction.Get();
        if (Contact)
        {
            // Keep demonstration events out of actual synthetic-contact coverage.
            Contact->bDemo = false;
            const int32 Serial = Contact->ReactionSerial;
            if (bSoakReactionBaselineSet && Serial >= LastSoakReactionSerial)
                ActualSoakReactions += Serial - LastSoakReactionSerial;
            LastSoakReactionSerial = Serial;
            bSoakReactionBaselineSet = true;
        }
        if (Profile && Profile->Capabilities.bContacts && Contact && !Contact->Zones.IsEmpty())
        {
            const int32 Cycle = FMath::FloorToInt(FMath::Max(0.0f, MetricsSeconds - 3.0f) / 3.0f);
            const int32 ZoneIndex = Cycle % Contact->Zones.Num();
            if (Cycle != SoakCycle)
            {
                SoakCycle = Cycle;
                Contact->Quality = (Cycle / Contact->Zones.Num()) % 3;
                Contact->Mood = Cycle % 3;
                Contact->bHairMotion = Contact->bClothMotion = Contact->bBodyMotion = true;
                Contact->bPhysicalMotion = Profile->Capabilities.bSecondaryPhysics && Cycle % 9 != 8;
                Contact->bLocalSpring = Profile->Capabilities.bLocalSprings && Cycle % 7 != 6;
            }
            const auto& Zone = Contact->Zones[ZoneIndex];
            FVector Target = Contact->GetZoneWorldPosition(ZoneIndex);
            Target += FVector(-Zone.Radius - Profile->ContactSettings.HandRadiusCm, 0, 0);
            const FTransform Raw(FQuat::Identity, Target);
            const FTransform Start(FQuat::Identity, Target + FVector(-100, 0, 0));
            const FTransform VisualLeft = Contact->ConstrainHand(Start, Raw, true);
            const FTransform VisualRight = Contact->ConstrainHand(Start, Raw, false);
            const float ReleaseSeconds = FMath::Max(5.0f,
                Profile->ContactSettings.ReactionSeconds + Profile->ContactSettings.CooldownSeconds + 1.0f);
            const bool Held = MetricsSeconds >= 3.0f && MetricsSeconds < SoakDuration - ReleaseSeconds
                && FMath::Fmod(MetricsSeconds - 3.0f, 3.0f) < 2.0f;
            const int32 ContactHand = (Cycle / Contact->Zones.Num()) % 2;
            Contact->SetHandSample(true, Raw, VisualLeft, Held && (ContactHand == 0 || Cycle % 3 == 0));
            Contact->SetHandSample(false, Raw, VisualRight, Held && (ContactHand == 1 || Cycle % 3 == 0));
        }
        else if (!bSoakContactAvailabilityReported)
        {
            bSoakContactAvailabilityReported = true;
            if (!Profile)
                TestCheck(false, TEXT("Soak target has a CharacterProfile"));
            else if (Profile->Capabilities.bContacts)
                TestCheck(false, TEXT("An enabled contact capability supplies a component and at least one runtime zone for soak"));
            else
                TestSkip(TEXT("Soak profile has Contacts disabled; no synthetic contact cycles are generated"));
        }
        if (Character->SecondaryMotion && Character->SecondaryMotion->HasFault()) bTestFailed = true;
        if (Character->ClothInteraction && Character->ClothInteraction->HasFault()) bTestFailed = true;
        if (Character->CharacterMesh)
        {
            for (const FTransform& Bone : Character->CharacterMesh->GetComponentSpaceTransforms())
                if (!VerificationIsFiniteTransform(Bone)) bTestFailed = true;
        }
        else bTestFailed = true;
    }
    if (MetricsInterval >= 1.0f)
    {
        MetricsInterval = 0.0f;
        const auto* Contact = Runtime.TargetCharacter.IsValid() ? Runtime.TargetCharacter->Interaction.Get() : nullptr;
        const auto* Physics = Runtime.TargetCharacter.IsValid() ? Runtime.TargetCharacter->SecondaryMotion.Get() : nullptr;
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
    if (FFileHelper::SaveStringToFile(PerfRows, *MetricsPath))
    {
        UE_LOG(LogGratiaVerification, Display, TEXT("METRICS saved=%s duration=%.1f"), *MetricsPath, MetricsSeconds);
    }
    else
    {
        bTestFailed = true;
        UE_LOG(LogGratiaVerification, Error, TEXT("METRICS could not save=%s"), *MetricsPath);
    }
    if (SoakDuration > 0.0f)
    {
        if (Runtime.TargetCharacter.IsValid() && Runtime.TargetCharacter->CharacterProfile)
        {
            const auto* Profile = Runtime.TargetCharacter->CharacterProfile.Get();
            const auto* Contact = Runtime.TargetCharacter->Interaction.Get();
            if (Profile->Capabilities.bContacts && Contact && !Contact->Zones.IsEmpty())
            {
                int32 ZoneTouchEntries = 0;
                for (const auto& Zone : Contact->Zones)
                {
                    ZoneTouchEntries += Zone.Reactions;
                    UE_LOG(LogGratiaVerification, Display, TEXT("SOAK zone=%s reactions=%d state=%d"), *Zone.Name.ToString(), Zone.Reactions, int32(Zone.State));
                }
                const float ReleaseSeconds = FMath::Max(5.0f,
                    Profile->ContactSettings.ReactionSeconds + Profile->ContactSettings.CooldownSeconds + 1.0f);
                if (SoakDuration >= 3.0f + ReleaseSeconds + 1.0f)
                    TestCheck(ActualSoakReactions > 0, TEXT("Soak generated at least one actual runtime contact reaction"));
                else TestSkip(TEXT("Soak is too short for acquisition and final release; positive contact-reaction coverage is not asserted"));
                TestCheck(Contact->Reaction < 0.01f && Contact->ActiveZone == INDEX_NONE, TEXT("Soak returns to neutral after final release"));
                UE_LOG(LogGratiaVerification, Display, TEXT("SOAK CONTACT COVERAGE: generated_cycles=%d zone_touch_entries=%d actual_reaction_serial_events=%d; a generated cycle or zone entry is not proof that its target reaction ran"),
                    SoakCycle == INDEX_NONE ? 0 : SoakCycle + 1, ZoneTouchEntries, ActualSoakReactions);
            }
            if (Profile->Capabilities.bSecondaryPhysics)
                TestCheck(Runtime.TargetCharacter->SecondaryMotion && !Runtime.TargetCharacter->SecondaryMotion->HasFault(),
                    TEXT("Soak enabled secondary physics remains finite and bounded"));
            else TestSkip(TEXT("SecondaryPhysics is disabled in this soak profile"));
        }
        else bTestFailed = true;
        UE_LOG(LogGratiaVerification, Display, TEXT("GRATIA_SOAK_%s seconds=%.1f cycles=%d"), bTestFailed ? TEXT("FAIL") : TEXT("PASS"), MetricsSeconds, SoakCycle);
        FPlatformMisc::RequestExitWithStatus(false, bTestFailed ? 1 : 0, TEXT("GratiaSoak"));
    }
}
