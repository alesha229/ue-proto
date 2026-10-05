#include "GratiaSoftBodyVerification.h"

#include "GratiaAnimInstance.h"
#include "GratiaBodySurface.h"
#include "GratiaCharacterProfile.h"
#include "GratiaHandAnimInstance.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaSoftBodyInteraction.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include "Engine/SkeletalMesh.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaSoftBodyQA, Log, All);

UGratiaSoftBodyVerification::UGratiaSoftBodyVerification()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGratiaSoftBodyVerification::BeginPlay()
{
    Super::BeginPlay();
    bRequested = FParse::Param(FCommandLine::Get(), TEXT("GratiaSoftBodyQA"));
    SetComponentTickEnabled(bRequested);
    if (!bRequested) return;
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    if (!Character.IsValid() || !Character->Interaction || !Character->CharacterMesh || !Character->SoftBodyInteraction)
    { Check(false, TEXT("character soft body components exist")); Finish(); return; }
    AddTickPrerequisiteComponent(Character->CharacterMesh);
    bSavedContactTick = Character->Interaction->IsComponentTickEnabled();
    bSavedDemo = Character->Interaction->bDemo;
    // Reactions are excluded so they cannot manufacture the measured bone motion.
    Character->Interaction->SetComponentTickEnabled(false);
    Character->Interaction->bDemo = false;
    UE_LOG(LogGratiaSoftBodyQA, Display, TEXT("SOFT_BODY_QA_START source=synthetic hands; explicit trigger bypasses input actions; desktop only"));
}

void UGratiaSoftBodyVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    if (bRequested && Character.IsValid() && Character->Interaction)
    {
        Character->Interaction->bDemo = bSavedDemo;
        Character->Interaction->SetComponentTickEnabled(bSavedContactTick);
        Character->Interaction->bBodyMotion = true;
        if (Character->SoftBodyInteraction) Character->SoftBodyInteraction->ClearHands();
    }
    Super::EndPlay(Reason);
}

bool UGratiaSoftBodyVerification::Check(bool bPass, const FString& Description)
{
    if (bPass) { UE_LOG(LogGratiaSoftBodyQA, Display, TEXT("SOFT_BODY_QA_CHECK PASS %s"), *Description); }
    else { bFailed = true; UE_LOG(LogGratiaSoftBodyQA, Error, TEXT("SOFT_BODY_QA_CHECK FAIL %s"), *Description); }
    return bPass;
}

void UGratiaSoftBodyVerification::Finish()
{
    if (bFinished) return;
    bFinished = true;
    SetComponentTickEnabled(false);
    if (Character.IsValid() && Character->SoftBodyInteraction) Character->SoftBodyInteraction->ClearHands();
    UE_LOG(LogGratiaSoftBodyQA, Display, TEXT("GRATIA_SOFT_BODY_QA %s zones=%d conformed_fingers=%d elapsed=%.2fs source=synthetic"),
        bFailed ? TEXT("FAIL") : TEXT("PASS"), ZoneIndex, ConformedFingers, Elapsed);
    FPlatformMisc::RequestExitWithStatus(false, bFailed ? 1 : 0);
}

FVector UGratiaSoftBodyVerification::CurrentTip() const
{
    const auto& Zones = Character->SoftBodyInteraction->GetZones();
    return Zones.IsValidIndex(ZoneIndex) ? Character->GetActorTransform().InverseTransformPosition(Zones[ZoneIndex].Tip) : FVector::ZeroVector;
}

TArray<FVector> UGratiaSoftBodyVerification::TipsInParentFrame() const
{
    TArray<FVector> Result;
    const auto* Mesh = Character->CharacterMesh.Get();
    const double Scale = Mesh->GetComponentTransform().GetScale3D().GetAbsMax();
    for (const auto& Zone : Character->SoftBodyInteraction->GetZones())
    {
        const int32 Parent = Mesh->GetBoneIndex(Mesh->GetParentBone(Zone.Bone));
        const FTransform Frame = Parent == INDEX_NONE ? Mesh->GetComponentTransform() : Mesh->GetBoneTransform(Parent);
        Result.Add(FTransform(Frame.GetRotation(), Frame.GetLocation()).InverseTransformPosition(Zone.Tip) / Scale);
    }
    return Result;
}

void UGratiaSoftBodyVerification::AimCamera(const FVector& Center, const FVector& OutwardDir, bool bFrontal)
{
    APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
    if (!Controller || !FApp::CanEverRender()) return;
    if (!ShotCamera) ShotCamera = GetWorld()->SpawnActor<ACameraActor>();
    if (!ShotCamera) return;
    // Side view: the press shows in the silhouette (flattening, dent, spread).
    const FVector Up = Character->GetActorUpVector();
    FVector Side = FVector::CrossProduct(Up, OutwardDir).GetSafeNormal();
    // Look from the body's outer side so the other soft part does not hide this one.
    if (FVector::DotProduct(Side, Center - Character->GetActorLocation()) < 0) Side = -Side;
    const FVector Focus = Center + OutwardDir * 4.0;
    // Frontal: from outside along the surface normal, slightly to the side and above.
    const FVector Eye = bFrontal ? Focus + OutwardDir * 42.0 + Side * 14.0 + Up * 10.0 : Focus + Side * 48.0 + OutwardDir * 12.0 + Up * 4.0;
    ShotCamera->SetActorLocationAndRotation(Eye, (Focus - Eye).Rotation());
    ShotCamera->GetCameraComponent()->FieldOfView = 55.0f;
    Controller->SetViewTarget(ShotCamera);
}

void UGratiaSoftBodyVerification::Shoot(const FString& Name)
{
    if (!FApp::CanEverRender() || !GEngine || !GEngine->GameViewport) return;
    FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/Windows"), Name + TEXT(".png")), false, false);
    UE_LOG(LogGratiaSoftBodyQA, Display, TEXT("SOFT_BODY_QA_SHOT %s"), *Name);
}

void UGratiaSoftBodyVerification::Submit(const FVector& Hand, float Delta, float Trigger, bool bAllowed)
{
    static const TArray<FVector> NoFingers;
    Character->SoftBodyInteraction->SubmitHand(true, Hand, Hand, bAllowed, Delta, Trigger, NoFingers);
    Character->SoftBodyInteraction->SubmitHand(false, FVector::ZeroVector, FVector::ZeroVector, false, Delta, 0, NoFingers);
}

void UGratiaSoftBodyVerification::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    if (!bRequested || bFinished) return;
    Elapsed += Delta;
    auto* SoftBody = Character.IsValid() ? Character->SoftBodyInteraction.Get() : nullptr;
    if (Elapsed > 90 || !SoftBody || SoftBody->HasFault()) { Check(false, TEXT("soft body QA timeout or safety fault")); Finish(); return; }
    if (!FMath::IsFinite(Delta) || Delta <= 0 || Delta > 0.1f) return;
    PhaseSeconds += Delta;
    SoftBody->UpdateZones();
    const auto& Zones = SoftBody->GetZones();
    const auto* Zone = Zones.IsValidIndex(ZoneIndex) ? &Zones[ZoneIndex] : nullptr;
    const auto& Settings = Character->CharacterProfile->SoftBody;
    const FTransform Actor = Character->GetActorTransform();
    switch (Phase)
    {
    case EPhase::Settle:
    {
#if WITH_EDITOR
        // Editor runs compile material shaders asynchronously (default material meanwhile);
        // wait so captures and timing use the real character materials.
        if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
        {
            Elapsed = PhaseSeconds = 0;
            break;
        }
#endif
        if (PhaseSeconds < 2.5f) break;
        FString Failure;
        if (!Check(Settings.bEnabled, TEXT("profile enables KawaiiPhysics soft body"))
            || !Check(SoftBody->RunChecks(Failure), Failure.IsEmpty() ? TEXT("zones resolve and KawaiiPhysics chains evaluate") : Failure)
            || !Check(!Zones.IsEmpty(), TEXT("soft body zones exist"))) { Finish(); return; }
        // Settled with physics on and no hands: compared later with the plain animation pose.
        RestTipsLocal = TipsInParentFrame();
        Advance(EPhase::Baseline);
        break;
    }
    case EPhase::Baseline:
        if (!Zone) { Advance(EPhase::Conform); break; }
        if (PhaseSeconds <= Delta)
        {
            BaselineTip = FVector::ZeroVector; BaselineSamples = 0; DepthAmplitude.Reset(); bSawGrab = false; PressTipCm = PullTipCm = 0;
            OnsetGapCm = 100.0; FrontScale = FVector::OneVector;
            AimCamera(Zone->Center, (Zone->Tip - Zone->Center).GetSafeNormal());
        }
        Submit(FVector(0, 0, -1.0e5), Delta, 0, false);
        BaselineTip += CurrentTip(); ++BaselineSamples;
        if (PhaseSeconds < 0.5f) break;
        BaselineTip /= BaselineSamples;
        Outward = (Zone->Tip - Zone->Center).GetSafeNormal();
        Start = Zone->Center + Outward * (Zone->Radius + Settings.PalmRadiusCm + 6);
        Pressed = Zone->Center + Outward * (Zone->Radius + Settings.PalmRadiusCm - Settings.HapticFullDepthCm);
        UE_LOG(LogGratiaSoftBodyQA, Display, TEXT("SOFT_BODY_QA_ZONE %s bone=%s radius=%.2f"), *Zone->Chain.ToString(), *Zone->Bone.ToString(), Zone->Radius);
        Shoot(FString::Printf(TEXT("GratiaSoftPress_%s_rest"), *Zone->Chain.ToString()));
        Advance(EPhase::Approach);
        break;
    case EPhase::Approach:
        Submit(Start, Delta, 0);
        if (PhaseSeconds >= 0.2f)
        {
            Check(SoftBody->GetHapticAmplitude(true) == 0 && SoftBody->GetDepthCm(true) == 0, TEXT("no vibration outside the soft zone"));
            Advance(EPhase::Press);
        }
        break;
    case EPhase::Press:
    {
        const float T = FMath::Clamp(PhaseSeconds / 0.8f, 0.0f, 1.0f);
        const FVector HandNow = FMath::Lerp(Start, Pressed, T);
        Submit(HandNow, Delta, 0);
        DepthAmplitude.Emplace(SoftBody->GetDepthCm(true), SoftBody->GetHapticAmplitude(true));
        // Gap between the palm skin and the zone surface when anything first reacts.
        if (OnsetGapCm >= 100.0 && (SoftBody->GetDepthCm(true) > 0 || SoftBody->GetSquash(Zone->Bone) > 0.01f))
            OnsetGapCm = FVector::Distance(HandNow, Zone->Center) - Zone->Radius - Character->CharacterProfile->HandSurface.PalmContactRadiusCm;
        PressTipCm = FMath::Max(PressTipCm, FVector::Distance(CurrentTip(), BaselineTip));
        if (PhaseSeconds < 1.0f) break;
        // Ignore the short first-contact tap; afterwards vibration must follow depth.
        // The soft part yields under the hand, so the reached depth is below the hand travel.
        bool bMonotonic = true;
        double LastDepth = -1, LastAmplitude = -1;
        for (const FVector2D& Sample : DepthAmplitude)
        {
            // The first ~1 cm carries the short first-contact tap; the depth curve starts after it.
            if (Sample.X > 1.0 && Sample.X > LastDepth + 0.05)
            {
                bMonotonic &= Sample.Y >= LastAmplitude - 0.02 || LastAmplitude < 0;
                LastDepth = Sample.X; LastAmplitude = Sample.Y;
            }
        }
        const double Expected = Settings.HapticMaxAmplitude * FMath::Pow(FMath::Clamp(LastDepth / Settings.HapticFullDepthCm, 0.0, 1.0), Settings.HapticDepthExponent);
        Check(bMonotonic && LastDepth >= 1.0 && FMath::IsNearlyEqual(LastAmplitude, Expected, 0.05),
            FString::Printf(TEXT("%s vibration rises with depth: %.2f at %.1fcm (expected %.2f)"), *Zone->Chain.ToString(), LastAmplitude, LastDepth, Expected));
        Check(PressTipCm >= 0.3, FString::Printf(TEXT("%s pressed tip moved %.2fcm (min 0.3)"), *Zone->Bone.ToString(), PressTipCm));
        Check(OnsetGapCm <= 0.3 && OnsetGapCm >= -1.0, FString::Printf(TEXT("%s squeeze and vibration start when the palm reaches the skin (gap %.2fcm)"),
            *Zone->Chain.ToString(), OnsetGapCm));
        Advance(EPhase::Squeeze);
        break;
    }
    case EPhase::Squeeze:
    {
        // Same hold, surface press on then off: the image difference is the dent alone.
        Submit(Pressed, Delta, 0);
        if (!bShotOn && PhaseSeconds >= 0.3f)
        {
            Check(SoftBody->GetSquash(Zone->Bone) >= 0.05f,
                FString::Printf(TEXT("%s squeezes under the press (squash %.2f, min 0.05)"), *Zone->Chain.ToString(), SoftBody->GetSquash(Zone->Bone)));
            FrontScale = SoftBody->GetSquashScale(Zone->Bone);
            Check(SoftBody->GetPressSphereCount() >= 1 && Settings.PressCollection,
                FString::Printf(TEXT("%s palm and finger spheres reach the press collection (%d)"), *Zone->Chain.ToString(), SoftBody->GetPressSphereCount()));
            Shoot(FString::Printf(TEXT("GratiaSoftPress_%s_on"), *Zone->Chain.ToString()));
            bShotOn = true;
        }
        if (bShotOn && PhaseSeconds >= 0.45f) SoftBody->bPressDeformation = false;
        if (!bShotOff && PhaseSeconds >= 0.65f)
        {
            Shoot(FString::Printf(TEXT("GratiaSoftPress_%s_off"), *Zone->Chain.ToString()));
            bShotOff = true;
        }
        if (PhaseSeconds < 0.8f) break;
        SoftBody->bPressDeformation = true;
        bShotOn = bShotOff = false;
        Advance(EPhase::SideSqueeze);
        break;
    }
    case EPhase::SideSqueeze:
    {
        // Pressed from the side the part compresses across, not along its front axis.
        const FVector Side = FVector::CrossProduct(Outward, FVector::UpVector).GetSafeNormal();
        Submit(Zone->Center + Side * (Zone->Radius + Settings.PalmRadiusCm - Settings.HapticFullDepthCm), Delta, 0);
        if (PhaseSeconds < 0.6f) break;
        const FVector SideScale = SoftBody->GetSquashScale(Zone->Bone);
        auto MostCompressed = [](const FVector& Scale) { return Scale.X <= Scale.Y && Scale.X <= Scale.Z ? 0 : Scale.Y <= Scale.Z ? 1 : 2; };
        auto Dominant = [](const FVector& Direction) { const FVector A = Direction.GetAbs(); return A.X >= A.Y && A.X >= A.Z ? 0 : A.Y >= A.Z ? 1 : 2; };
        const int32 FrontAxis = MostCompressed(FrontScale), SideAxis = MostCompressed(SideScale);
        const int32 ExpectedFront = Dominant(Zone->Rotation.UnrotateVector(Outward)), ExpectedSide = Dominant(Zone->Rotation.UnrotateVector(Side));
        Check(FrontAxis == ExpectedFront && SideAxis == ExpectedSide && SideScale[SideAxis] < 0.98f,
            FString::Printf(TEXT("%s squashes along the press direction: front axis %d (expected %d), side axis %d (expected %d, scale %.3f)"),
                *Zone->Chain.ToString(), FrontAxis, ExpectedFront, SideAxis, ExpectedSide, SideScale[SideAxis]));
        Advance(EPhase::Arm);
        break;
    }
    case EPhase::Arm:
        Submit(Pressed, Delta, 0);
        if (PhaseSeconds >= 0.1f) Advance(EPhase::Grab);
        break;
    case EPhase::Grab:
        Submit(Pressed, Delta, 1);
        bSawGrab |= SoftBody->GetGrabbedBone(true) == Zone->Bone;
        if (PhaseSeconds >= 0.2f) { Check(bSawGrab, FString::Printf(TEXT("trigger grabs %s"), *Zone->Bone.ToString())); Advance(EPhase::Pull); }
        break;
    case EPhase::Pull:
    {
        // Lift across the bone: a pull along the bone axis cannot rotate a fixed-length bone.
        const FVector Lift = FVector::VectorPlaneProject(Actor.GetRotation().GetUpVector(), Outward).GetSafeNormal();
        const float T = FMath::Clamp(PhaseSeconds / 0.6f, 0.0f, 1.0f);
        Submit(Pressed + Lift * 5.0 * T, Delta, 1);
        PullTipCm = FMath::Max(PullTipCm, FVector::DotProduct(CurrentTip() - BaselineTip, Actor.InverseTransformVectorNoScale(Lift)));
        if (PhaseSeconds < 0.8f) break;
        Check(PullTipCm >= 1.0, FString::Printf(TEXT("%s held tip follows a 5cm pull by %.2fcm (min 1.0)"), *Zone->Bone.ToString(), PullTipCm));
        Advance(EPhase::Release);
        break;
    }
    case EPhase::Release:
        Submit(Start, Delta, 0);
        if (PhaseSeconds < 1.5f) break;
        ReturnTipCm = FVector::Distance(CurrentTip(), BaselineTip);
        Check(SoftBody->GetGrabbedBone(true).IsNone(), TEXT("trigger release frees the tip"));
        Check(ReturnTipCm <= FMath::Max(0.5, PullTipCm * 0.4), FString::Printf(TEXT("%s returns to %.2fcm of rest after release"), *Zone->Bone.ToString(), ReturnTipCm));
        Advance(EPhase::Lost);
        break;
    case EPhase::Lost:
        if (PhaseSeconds < 0.1f) Submit(Pressed, Delta, 0);
        else if (PhaseSeconds < 0.3f) Submit(Pressed, Delta, 1);
        else
        {
            Submit(Pressed, Delta, 1, false);
            Check(SoftBody->GetGrabbedBone(true).IsNone() && SoftBody->GetHapticAmplitude(true) == 0,
                TEXT("tracking loss clears grab and vibration"));
            Advance(EPhase::NextZone);
        }
        break;
    case EPhase::NextZone:
        if (PhaseSeconds <= Delta)
            Check(SoftBody->GetPressSphereCount() == 0, TEXT("a lost hand leaves no press spheres"));
        Submit(FVector(0, 0, -1.0e5), Delta, 0, false);
        if (PhaseSeconds < 0.8f) break;
        if (Zone) Check(FMath::Abs(SoftBody->GetSquash(Zone->Bone)) <= 0.03f,
            FString::Printf(TEXT("%s springs back after the hand leaves (squash %.3f)"), *Zone->Chain.ToString(), SoftBody->GetSquash(Zone->Bone)));
        UE_LOG(LogGratiaSoftBodyQA, Display, TEXT("SOFT_BODY_QA_RESULT %s press_tip=%.2fcm pull_tip=%.2fcm return=%.2fcm"),
            Zone ? *Zone->Bone.ToString() : TEXT("?"), PressTipCm, PullTipCm, ReturnTipCm);
        ++ZoneIndex;
        Advance(ZoneIndex < Zones.Num() ? EPhase::Baseline : EPhase::Conform);
        break;
    case EPhase::Conform:
    {
        const auto& First = Zones[0];
        if (!TestHand)
        {
            TestHand = NewObject<USkeletalMeshComponent>(Character.Get(), TEXT("SoftBodyQAHand"), RF_Transient);
            TestHand->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/XRMannequins/Meshes/SKM_MannyXR_right.SKM_MannyXR_right")));
            TestHand->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            TestHand->SetAnimInstanceClass(UGratiaHandAnimInstance::StaticClass());
            TestHand->RegisterComponent();
            auto* HandAnim = Cast<UGratiaHandAnimInstance>(TestHand->GetAnimInstance());
            if (!Check(TestHand->GetSkeletalMeshAsset() && HandAnim && HandAnim->LoadDefaultPoses(), TEXT("XR hand mesh and poses load for conform test")))
            { Finish(); return; }
            HandAnim->bLeftHand = false;
            TestHand->SetWorldLocation(First.Center + (First.Tip - First.Center).GetSafeNormal() * 60.0);
            PhaseSeconds = 0;
            break;
        }
        auto* HandAnim = Cast<UGratiaHandAnimInstance>(TestHand->GetAnimInstance());
        if (!HandAnim->HasConformSamples()) { if (PhaseSeconds > 3) { Check(false, TEXT("hand conform samples are computed")); Finish(); } break; }
        // Orient so the curl direction (tips toward the palm) faces the zone centre.
        if (PhaseSeconds < 0.6f)
        {
            FVector Curl = FVector::ZeroVector, Palm = FVector::ZeroVector;
            for (int32 F = 1; F < UGratiaHandAnimInstance::NumFingers; ++F)
            {
                Curl += HandAnim->Samples[F][UGratiaHandAnimInstance::NumSamples][3] - HandAnim->Samples[F][0][3];
                Palm += HandAnim->Samples[F][0][0];
            }
            Palm /= UGratiaHandAnimInstance::NumFingers - 1;
            const FVector Out = (First.Tip - First.Center).GetSafeNormal();
            const FQuat Rotation = FQuat::FindBetweenNormals(Curl.GetSafeNormal(), -Out);
            const FVector PalmWorld = Rotation.RotateVector(Palm);
            TestHand->SetWorldRotation(Rotation);
            TestHand->SetWorldLocation(First.Center + Out * (First.Radius + 1.0) - PalmWorld);
            HandAnim->bConform = true; HandAnim->FingerRadiusCm = Settings.FingerRadiusCm; HandAnim->ConformMarginCm = Settings.FingerConformMarginCm;
            for (float& Input : HandAnim->FingerInput) Input = 1.0f;
            SoftBody->GetConformSpheres(First.Center, 40, HandAnim->ConformSpheres);
            break;
        }
        SoftBody->GetConformSpheres(First.Center, 40, HandAnim->ConformSpheres);
        if (PhaseSeconds < 1.4f) break;
        TArray<FVector> Points;
        HandAnim->GetFingerPoints(Points);
        ConformedFingers = 0;
        double Deepest = 0;
        for (int32 F = 0; F < UGratiaHandAnimInstance::NumFingers; ++F)
            if (HandAnim->FingerCap[F] < 0.95f && FMath::IsNearlyEqual(HandAnim->FingerAlpha[F], HandAnim->FingerCap[F], 0.05f)) ++ConformedFingers;
        for (const FVector& Point : Points)
            Deepest = FMath::Max(Deepest, First.Radius - FVector::Distance(Point, First.Center));
        UE_LOG(LogGratiaSoftBodyQA, Display, TEXT("SOFT_BODY_QA_CONFORM %s"), *HandAnim->GetDiagnostics());
        Check(ConformedFingers >= 3, FString::Printf(TEXT("fingers stop on the soft surface (%d of 5 capped below a full fist)"), ConformedFingers));
        Check(Deepest <= 1.0, FString::Printf(TEXT("finger tips stay outside the contact volume (deepest %.2fcm)"), Deepest));
        Advance(EPhase::BodyGrip);
        break;
    }
    case EPhase::BodyGrip:
    {
        // A real XR hand starts off-axis near an arm, a leg and the waist; the grip helper must
        // detect the part, put the palm on it across the part's axis and wrap the fingers.
        auto* Surface = Character->BodySurface.Get();
        auto* HandAnim = TestHand ? Cast<UGratiaHandAnimInstance>(TestHand->GetAnimInstance()) : nullptr;
        const auto* Profile = Character->CharacterProfile.Get();
        auto Done = [this]()
        {
            if (TestHand) { TestHand->DestroyComponent(); TestHand = nullptr; }
            Character->Interaction->bBodyMotion = false;
            Advance(EPhase::Disabled);
        };
        if (GripIndex == 0 && GripParts.IsEmpty() && PhaseSeconds <= Delta)
        {
            for (const TCHAR* Token : {TEXT("forearm_L"), TEXT("thigh_L"), TEXT("spine_001")})
                for (const FGratiaSurfaceCapsule& Capsule : Profile->BodySurface)
                    if (Capsule.Bone.ToString().Contains(Token)) { GripParts.Add(Capsule.Bone); GripSqueeze.Add(0.0f); break; }
            // Cupping a breast with a light and a full trigger: the full one must squeeze deeper.
            for (const FGratiaSurfaceCapsule& Capsule : Profile->BodySurface)
                if (Capsule.bSoft && Capsule.Bone.ToString().Contains(TEXT("breast")))
                {
                    for (const float Amount : {0.3f, 1.0f}) { GripParts.Add(Capsule.Bone); GripSqueeze.Add(Amount); }
                    break;
                }
            if (!Check(Surface && HandAnim && Surface->HasSurface() && GripParts.Num() >= 2,
                FString::Printf(TEXT("body surface has grip targets (%d of forearm/thigh/waist)"), GripParts.Num()))) { Done(); break; }
            // Pass-through: a hand driven from outside deep into the body stops at the surface.
            struct FProbe { FString Label; FVector Point; FVector Direction; };
            TArray<FProbe> Probes;
            const FVector Forward = Character->GetActorForwardVector(), Up = Character->GetActorUpVector();
            for (const FGratiaSurfaceCapsule& Capsule : Profile->BodySurface)
                if (Capsule.Bone.ToString().Contains(TEXT("spine_001")))
                {
                    FVector A, B; float R;
                    if (Surface->GetBoneCapsule(Capsule.Bone, A, B, R)) Probes.Add({TEXT("belly"), (A + B) * 0.5, Forward});
                    break;
                }
            const FName Head = Profile->ResolveBone(TEXT("Head"));
            if (!Head.IsNone() && Character->CharacterMesh->GetBoneIndex(Head) != INDEX_NONE)
                Probes.Add({TEXT("head"), Character->CharacterMesh->GetBoneLocation(Head) + Up * 8.0, Forward});
            for (const auto& SoftZone : SoftBody->GetZones())
                Probes.Add({SoftZone.Chain.ToString(), SoftZone.Center, (SoftZone.Tip - SoftZone.Center).GetSafeNormal()});
            for (const FProbe& Probe : Probes)
            {
                const FTransform Constrained = Character->Interaction->ConstrainHand(
                    FTransform(Probe.Point + Probe.Direction * 40.0), FTransform(Probe.Point - Probe.Direction * 5.0), true);
                FGratiaSurfaceHit Near;
                const bool bNear = Surface->FindNearest(Constrained.GetLocation(), 100.0f, Near, true);
                Check(bNear && Near.Gap >= Profile->HandSurface.PalmContactRadiusCm - 0.3f,
                    FString::Printf(TEXT("palm stops at the %s surface (gap %.2fcm at %s)"), *Probe.Label, bNear ? Near.Gap : -99.0f,
                        bNear ? *Near.Bone.ToString() : TEXT("nothing")));
            }
        }
        if (GripIndex >= GripParts.Num()) { Done(); break; }
        const FName Bone = GripParts[GripIndex];
        FVector A, B; float Radius;
        FGratiaPalmFrame Palm;
        if (!Surface->GetBoneCapsule(Bone, A, B, Radius) || !HandAnim->GetPalmFrame(Palm))
        {
            Check(false, FString::Printf(TEXT("%s capsule and palm frame available"), *Bone.ToString()));
            ++GripIndex; Advance(EPhase::BodyGrip); break;
        }
        const float Thickness = Profile->HandSurface.PalmThicknessCm;
        const float Squeeze = GripSqueeze.IsValidIndex(GripIndex) ? GripSqueeze[GripIndex] : 0.0f;
        const float Shrink = Settings.SquishDepthCm * Squeeze;
        if (PhaseSeconds <= Delta)
        {
            // Upper thigh (near the hip): lower down the hanging hand is in front of it.
            const FVector Mid = FMath::Lerp(A, B, Bone.ToString().Contains(TEXT("thigh")) ? 0.25 : 0.5);
            GripAxis = (B - A).GetSafeNormal();
            // Approach horizontally from outside the body (the actor origin is at the feet).
            const FVector Up = Character->GetActorUpVector();
            FVector Out = FVector::VectorPlaneProject(FVector::VectorPlaneProject(Mid - Character->GetActorLocation(), Up),
                GripAxis.IsZero() ? Up : GripAxis).GetSafeNormal();
            // Legs and torso from the front: from the side the hanging arm is nearer.
            if (Out.Size() < 0.5 || !Bone.ToString().Contains(TEXT("arm")))
                Out = FVector::VectorPlaneProject(Character->GetActorForwardVector(), GripAxis.IsZero() ? Up : GripAxis).GetSafeNormal();
            // A soft part (breast front, butt back): approach from its own side.
            if (Squeeze > 0)
                Out = FVector::DotProduct(Mid - Character->GetActorLocation(), Character->GetActorForwardVector()) >= 0
                    ? Character->GetActorForwardVector() : -Character->GetActorForwardVector();
            // The idle hands hang in front of the thighs: hold the thigh from behind and outside.
            if (Bone.ToString().Contains(TEXT("thigh")))
            {
                const FVector Lateral = FVector::VectorPlaneProject(Mid - Character->GetActorLocation(), Up).GetSafeNormal();
                Out = FVector::VectorPlaneProject(Lateral - Out, GripAxis.IsZero() ? Up : GripAxis).GetSafeNormal();
            }
            // Worst case start: palm tilted 35 degrees, fingers along the part, 3.5 cm away.
            const FVector Along = GripAxis.IsZero() ? FVector::UpVector : GripAxis;
            const FVector TiltAxis = FVector::CrossProduct(Out, Along).GetSafeNormal();
            const FVector StartNormal = FQuat(TiltAxis, FMath::DegreesToRadians(35.0f)).RotateVector(-Out);
            const FVector StartFinger = FVector::VectorPlaneProject(Along, StartNormal).GetSafeNormal();
            const FQuat Rotation = FRotationMatrix::MakeFromXZ(StartFinger, StartNormal).ToQuat()
                * FRotationMatrix::MakeFromXZ(Palm.Finger, Palm.Normal).ToQuat().Inverse();
            const FVector PalmStart = Mid + Out * (Radius + Thickness + 3.5);
            const FTransform StartPose(Rotation, PalmStart - Rotation.RotateVector(Palm.Point));
            FGratiaSurfaceHit Hit;
            const bool bFound = Squeeze > 0
                ? Surface->FindNearest(PalmStart, Profile->HandSurface.GripReachCm + Thickness, Hit, true) && Hit.bSoftZone
                : Surface->FindNearest(PalmStart, Profile->HandSurface.GripReachCm + Thickness, Hit, false, true);
            // The torso is several slices; any of them is the waist/torso part.
            const FString Name = Bone.ToString();
            const bool bTorso = Name.Contains(TEXT("spine")) || Name.Contains(TEXT("pelvis"));
            const FString Found = bFound ? Hit.Bone.ToString() : FString();
            const bool bSamePart = Squeeze > 0 ? Found == Name : bTorso ? (Found.Contains(TEXT("spine")) || Found.Contains(TEXT("pelvis")))
                : Found.Contains(Name.Contains(TEXT("thigh")) ? TEXT("thigh") : TEXT("forearm"));
            Check(bFound && bSamePart, FString::Printf(TEXT("grip near %s detects that part (found %s)"), *Name, bFound ? *Found : TEXT("nothing")));
            if (bFound) GripParts[GripIndex] = Hit.Bone;
            TArray<FVector4> NearSpheres;
            TArray<FGratiaConformCapsule> NearCapsules;
            Surface->GatherConformShapes(PalmStart, 25.0f, NearSpheres, NearCapsules, Shrink);
            TestHand->SetWorldTransform(bFound ? UGratiaBodySurface::SolveWrap(StartPose, Palm, UGratiaBodySurface::Squeezed(Hit, 0.5f * Shrink), Thickness,
                Settings.FingerRadiusCm + Settings.FingerConformMarginCm, NearCapsules) : StartPose);
            if (bFound) AimCamera(Hit.Point, Hit.Normal, true);
            HandAnim->bConform = true; HandAnim->FingerRadiusCm = Settings.FingerRadiusCm; HandAnim->ConformMarginCm = Settings.FingerConformMarginCm;
            for (int32 F = 0; F < UGratiaHandAnimInstance::NumFingers; ++F) { HandAnim->FingerInput[F] = 1.0f; HandAnim->FingerAlpha[F] = 0.0f; }
        }
        Surface->GatherConformShapes(TestHand->GetComponentTransform().TransformPosition(Palm.Point), 30.0f, HandAnim->ConformSpheres, HandAnim->ConformCapsules, Shrink);
        if (PhaseSeconds < 1.3f) break;
        FVector PalmPoint, PalmNormal, PalmFinger;
        UGratiaBodySurface::PalmWorld(TestHand->GetComponentTransform(), Palm, PalmPoint, PalmNormal, PalmFinger);
        FGratiaSurfaceHit OnPart;
        Surface->FindOnBone(Bone, PalmPoint, OnPart);
        const float Facing = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(PalmNormal, -OnPart.Normal), -1.0, 1.0)));
        const float Across = OnPart.WrapAxis.IsZero() ? 0.0f : FMath::Abs(FVector::DotProduct(PalmFinger, OnPart.WrapAxis));
        int32 Wrapped = 0;
        for (int32 F = 1; F < UGratiaHandAnimInstance::NumFingers; ++F)
            if (HandAnim->FingerCap[F] < 0.98f && FMath::IsNearlyEqual(HandAnim->FingerAlpha[F], HandAnim->FingerCap[F], 0.06f)) ++Wrapped;
        TArray<FVector> Points;
        HandAnim->GetFingerPoints(Points);
        double Deepest = -100.0;
        int32 DeepestPoint = INDEX_NONE;
        constexpr double FingerFlesh = 0.8;
        for (int32 I = 0; I < Points.Num(); ++I)
        {
            const FVector& Point = Points[I];
            double Depth = -100.0;
            for (const FGratiaConformCapsule& Capsule : HandAnim->ConformCapsules)
                Depth = FMath::Max(Depth, Capsule.Radius + FingerFlesh - FMath::PointDistToSegment(Point, Capsule.A, Capsule.B));
            for (const FVector4& Sphere : HandAnim->ConformSpheres)
                Depth = FMath::Max(Depth, Sphere.W + FingerFlesh - FVector::Distance(Point, FVector(Sphere.X, Sphere.Y, Sphere.Z)));
            if (Depth > Deepest) { Deepest = Depth; DeepestPoint = I; }
        }
        Shoot(Squeeze > 0 ? FString::Printf(TEXT("GratiaBodyGrip_%s_squeeze%02d"), *Bone.ToString(), FMath::RoundToInt(Squeeze * 10))
            : FString::Printf(TEXT("GratiaBodyGrip_%s"), *Bone.ToString()));
        if (Squeeze > 0)
        {
            // How far the fingers sink into the (unsqueezed) soft surface.
            double Sink = -100.0;
            for (const FVector& Point : Points) Sink = FMath::Max(Sink, Radius - FMath::PointDistToSegment(Point, A, B));
            CupSinks.Add(Sink);
            UE_LOG(LogGratiaSoftBodyQA, Display, TEXT("BODY_GRIP_QA cup %s squeeze=%.1f sink=%.2fcm"), *Bone.ToString(), Squeeze, Sink);
            if (CupSinks.Num() == 2)
                Check(CupSinks[1] >= CupSinks[0] + 0.5, FString::Printf(TEXT("%s full trigger squeezes deeper than a light one (%.2f vs %.2f cm)"),
                    *Bone.ToString(), CupSinks[1], CupSinks[0]));
        }
        // Points are (distal joint, tip) per finger: thumb, index, middle, ring, pinky.
        UE_LOG(LogGratiaSoftBodyQA, Display, TEXT("BODY_GRIP_QA deepest at finger %d %s"), DeepestPoint / 2, DeepestPoint % 2 ? TEXT("tip") : TEXT("joint"));
        UE_LOG(LogGratiaSoftBodyQA, Display, TEXT("BODY_GRIP_QA part=%s radius=%.1fcm palm_gap=%.2fcm facing=%.1fdeg across=%.2f wrapped=%d deepest=%.2fcm %s"),
            *Bone.ToString(), Radius, OnPart.Gap, Facing, Across, Wrapped, Deepest, *HandAnim->GetDiagnostics());
        Check(OnPart.Gap >= Thickness - 0.5f * Shrink - 0.6f && OnPart.Gap <= Thickness + 3.0f,
            FString::Printf(TEXT("%s palm rests on the surface (gap %.2fcm, palm %.2fcm, open fingers may lift it up to 3cm)"), *Bone.ToString(), OnPart.Gap, Thickness));
        Check(Facing <= 12.0f, FString::Printf(TEXT("%s palm faces the part (%.1f deg)"), *Bone.ToString(), Facing));
        Check(Across <= 0.35f, FString::Printf(TEXT("%s fingers run around the part (axis dot %.2f)"), *Bone.ToString(), Across));
        Check(Wrapped >= 3, FString::Printf(TEXT("%s fingers wrap and stop on the surface (%d of 4)"), *Bone.ToString(), Wrapped));
        Check(Deepest <= 1.0, FString::Printf(TEXT("%s fingers stay outside the body (deepest %.2fcm)"), *Bone.ToString(), Deepest));
        ++GripIndex;
        Advance(EPhase::BodyGrip);
        break;
    }
    case EPhase::Disabled:
    {
        Submit(FVector(0, 0, -1.0e5), Delta, 0, false);
        if (PhaseSeconds < 0.5f) break;
        const auto* Anim = Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance());
        Check(Anim && !Anim->bSoftBody && Anim->GetActiveSoftBodyChainCount() == 0, TEXT("body motion setting disables KawaiiPhysics chains"));
        // Physics off = animation pose. At rest the soft parts must stay on it, so the skin
        // keeps its authored place under the clothing (no gravity sag or collider push).
        const TArray<FVector> PoseTips = TipsInParentFrame();
        double RestOffset = PoseTips.Num() == RestTipsLocal.Num() ? 0.0 : 1.0e3;
        for (int32 I = 0; I < PoseTips.Num() && I < RestTipsLocal.Num(); ++I)
            RestOffset = FMath::Max(RestOffset, FVector::Distance(PoseTips[I], RestTipsLocal[I]));
        Check(RestOffset <= 0.3, FString::Printf(TEXT("at rest soft parts stay on the animation pose (max %.2fcm, limit 0.30)"), RestOffset));
        Character->Interaction->bBodyMotion = true;
        Advance(EPhase::Resumed);
        break;
    }
    case EPhase::Resumed:
    {
        Submit(FVector(0, 0, -1.0e5), Delta, 0, false);
        if (PhaseSeconds < 1.0f) break;
        FString Failure;
        Check(SoftBody->RunChecks(Failure), Failure.IsEmpty() ? TEXT("body motion re-enables KawaiiPhysics chains") : Failure);
        RestTipsLocal = TipsInParentFrame();
        if (Settings.Chains.ContainsByPredicate([](const FGratiaSoftBodyChain& Chain) { return Chain.GravityScale > 0.0f; }))
        {
            // Lean the whole character forward: gravity relative to the pose must now act.
            SavedRotation = Character->GetActorRotation();
            Character->SetActorRotation(SavedRotation + FRotator(-35, 0, 0));
            Advance(EPhase::Tilt);
        }
        else Finish();
        break;
    }
    case EPhase::Tilt:
    {
        Submit(FVector(0, 0, -1.0e5), Delta, 0, false);
        if (PhaseSeconds < 1.5f) break;
        const TArray<FVector> Tilted = TipsInParentFrame();
        double Moved = 0.0;
        for (int32 I = 0; I < Tilted.Num() && I < RestTipsLocal.Num(); ++I)
            Moved = FMath::Max(Moved, FVector::Distance(Tilted[I], RestTipsLocal[I]));
        Check(Moved >= 0.1, FString::Printf(TEXT("gravity moves soft parts when the body leans 35 degrees (%.2fcm, min 0.10; rest noise <0.02)"), Moved));
        Character->SetActorRotation(SavedRotation);
        Advance(EPhase::Upright);
        break;
    }
    case EPhase::Upright:
    {
        Submit(FVector(0, 0, -1.0e5), Delta, 0, false);
        if (PhaseSeconds < 2.0f) break;
        const TArray<FVector> Back = TipsInParentFrame();
        double Offset = 0.0;
        for (int32 I = 0; I < Back.Num() && I < RestTipsLocal.Num(); ++I)
            Offset = FMath::Max(Offset, FVector::Distance(Back[I], RestTipsLocal[I]));
        Check(Offset <= 0.3, FString::Printf(TEXT("upright again, soft parts return to the pose (%.2fcm, limit 0.30)"), Offset));
        Finish();
        break;
    }
    }
}
