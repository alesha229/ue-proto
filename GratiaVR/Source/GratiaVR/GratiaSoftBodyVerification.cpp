#include "GratiaSoftBodyVerification.h"

#include "GratiaAnimInstance.h"
#include "GratiaCharacterProfile.h"
#include "GratiaHandAnimInstance.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaSoftBodyInteraction.h"
#include "Components/SkeletalMeshComponent.h"
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
        if (PhaseSeconds < 2.5f) break;
        FString Failure;
        if (!Check(Settings.bEnabled, TEXT("profile enables KawaiiPhysics soft body"))
            || !Check(SoftBody->RunChecks(Failure), Failure.IsEmpty() ? TEXT("zones resolve and KawaiiPhysics chains evaluate") : Failure)
            || !Check(!Zones.IsEmpty(), TEXT("soft body zones exist"))) { Finish(); return; }
        Advance(EPhase::Baseline);
        break;
    }
    case EPhase::Baseline:
        if (!Zone) { Advance(EPhase::Conform); break; }
        if (PhaseSeconds <= Delta) { BaselineTip = FVector::ZeroVector; BaselineSamples = 0; DepthAmplitude.Reset(); bSawGrab = false; PressTipCm = PullTipCm = 0; }
        Submit(FVector(0, 0, -1.0e5), Delta, 0, false);
        BaselineTip += CurrentTip(); ++BaselineSamples;
        if (PhaseSeconds < 0.5f) break;
        BaselineTip /= BaselineSamples;
        Outward = (Zone->Tip - Zone->Center).GetSafeNormal();
        Start = Zone->Center + Outward * (Zone->Radius + Settings.PalmRadiusCm + 6);
        Pressed = Zone->Center + Outward * (Zone->Radius + Settings.PalmRadiusCm - Settings.HapticFullDepthCm);
        UE_LOG(LogGratiaSoftBodyQA, Display, TEXT("SOFT_BODY_QA_ZONE %s bone=%s radius=%.2f"), *Zone->Chain.ToString(), *Zone->Bone.ToString(), Zone->Radius);
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
        Submit(FMath::Lerp(Start, Pressed, T), Delta, 0);
        DepthAmplitude.Emplace(SoftBody->GetDepthCm(true), SoftBody->GetHapticAmplitude(true));
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
        Submit(FVector(0, 0, -1.0e5), Delta, 0, false);
        if (PhaseSeconds < 0.8f) break;
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
        TestHand->DestroyComponent(); TestHand = nullptr;
        Character->Interaction->bBodyMotion = false;
        Advance(EPhase::Disabled);
        break;
    }
    case EPhase::Disabled:
    {
        Submit(FVector(0, 0, -1.0e5), Delta, 0, false);
        if (PhaseSeconds < 0.5f) break;
        const auto* Anim = Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance());
        Check(Anim && !Anim->bSoftBody && Anim->GetActiveSoftBodyChainCount() == 0, TEXT("body motion setting disables KawaiiPhysics chains"));
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
        Finish();
        break;
    }
    }
}
