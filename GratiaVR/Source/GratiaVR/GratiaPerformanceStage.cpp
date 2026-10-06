#include "GratiaPerformanceStage.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/AudioComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaStage, Log, All);

UGratiaPerformanceStage::UGratiaPerformanceStage()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

AGratiaPreviewCharacter* UGratiaPerformanceStage::GetCharacter() const
{
    return Cast<AGratiaPreviewCharacter>(GetOwner());
}

const FGratiaPerformanceClip* UGratiaPerformanceStage::Current() const
{
    const AGratiaPreviewCharacter* Character = GetCharacter();
    return Character && Character->PreviewPose == EGratiaPreviewPose::Performance ? Character->GetPerformance() : nullptr;
}

float UGratiaPerformanceStage::GetPerformanceTime() const
{
    const AGratiaPreviewCharacter* Character = GetCharacter();
    const FGratiaPerformanceClip* Performance = Current();
    const UAnimSingleNodeInstance* Instance = Character && Character->CharacterMesh ? Character->CharacterMesh->GetSingleNodeInstance() : nullptr;
    if (!Performance || !Instance || Instance->GetAnimationAsset() != Performance->GetPart(Character->PerformancePart)) return -1.0f;
    // Parts share their boundary frame: each part starts where the previous one ends.
    float Time = 0.0f;
    for (int32 Part = 0; Part < Character->PerformancePart; ++Part)
        if (const UAnimSequence* Clip = Performance->GetPart(Part)) Time += Clip->GetPlayLength();
    return Time + Instance->GetCurrentTime();
}

bool UGratiaPerformanceStage::GetViewpoint(FTransform& OutWorld) const
{
    const FGratiaPerformanceClip* Performance = Current();
    if (!Performance || !Performance->Scene.bHasViewpoint) return false;
    OutWorld = Performance->Scene.Viewpoint * GetOwner()->GetActorTransform();
    return !OutWorld.ContainsNaN();
}

void UGratiaPerformanceStage::SetViewpointActive(bool bActive)
{
    bViewpointActive = bActive;
    UpdateHiddenBones();
}

float UGratiaPerformanceStage::GetMusicTime() const
{
    if (!IsMusicPlaying() || !GetWorld()) return -1.0f;
    return MusicStartOffset + float(GetWorld()->GetTimeSeconds() - MusicStartTime);
}

bool UGratiaPerformanceStage::IsMusicPlaying() const
{
    return Music && Music->IsPlaying();
}

void UGratiaPerformanceStage::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    UpdateStage();
}

void UGratiaPerformanceStage::EndPlay(const EEndPlayReason::Type Reason)
{
    StopMusic();
    Super::EndPlay(Reason);
}

void UGratiaPerformanceStage::UpdateStage()
{
    AGratiaPreviewCharacter* Character = GetCharacter();
    const FGratiaPerformanceClip* Performance = Current();
    if (!Character || !Performance) { HidePartner(); StopMusic(); return; }
    const FGratiaPerformanceScene& Scene = Performance->Scene;
    if (Scene.HasPartner()) { if (PosedFor != Performance) PosePartner(*Performance); }
    else HidePartner();

    // Music follows the performance clock; a jump (loop, segment seek, hitch) re-syncs it.
    const float Time = GetPerformanceTime();
    const bool bSound = Character->Interaction && Character->Interaction->bSound && Character->CharacterProfile
        && Character->CharacterProfile->Capabilities.bSound;
    if (!Scene.Music || !bSound || Time < 0.0f || Time >= Scene.Music->GetDuration()) { StopMusic(); return; }
    if (!Music)
    {
        Music = NewObject<UAudioComponent>(Character, TEXT("PerformanceMusic"));
        Music->bAutoActivate = false;
        Music->bAllowSpatialization = false;
        Music->bIsUISound = false;
        Music->SetupAttachment(Character->GetRootComponent());
        Music->RegisterComponent();
    }
    const float Expected = GetMusicTime();
    if (Music->Sound != Scene.Music || Expected < 0.0f || FMath::Abs(Expected - Time) > MusicResyncSeconds)
    {
        Music->SetSound(Scene.Music);
        Music->SetVolumeMultiplier(Scene.MusicVolume);
        Music->Play(Time);
        MusicStartOffset = Time;
        MusicStartTime = GetWorld()->GetTimeSeconds();
        UE_LOG(LogGratiaStage, Display, TEXT("PERFORMANCE_MUSIC %s at %.2fs (was %.2fs)"), *Scene.Music->GetName(), Time, Expected);
    }
}

void UGratiaPerformanceStage::PosePartner(const FGratiaPerformanceClip& Performance)
{
    AGratiaPreviewCharacter* Character = GetCharacter();
    const FGratiaPerformanceScene& Scene = Performance.Scene;
    if (!Partner)
    {
        Partner = NewObject<UPoseableMeshComponent>(Character, TEXT("PerformancePartner"));
        Partner->SetupAttachment(Character->GetRootComponent());
        Partner->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Partner->SetCastShadow(false);
        Partner->RegisterComponent();
    }
    Partner->SetSkinnedAssetAndUpdate(Scene.PartnerMesh);
    Partner->SetRelativeTransform(Scene.PartnerTransform);
    for (int32 Bone = 0; Bone < Scene.PartnerMesh->GetRefSkeleton().GetNum(); ++Bone)
        Partner->ResetBoneTransformByName(Scene.PartnerMesh->GetRefSkeleton().GetBoneName(Bone));
    // Parents first: each bone turns (shortest arc) so its reference child lies along the
    // target segment; children keep their local transforms and follow.
    for (const FGratiaPartnerAim& Aim : Scene.PartnerPose)
    {
        if (Aim.bPlace) Partner->SetBoneLocationByName(Aim.Bone, Aim.From, EBoneSpaces::ComponentSpace);
        const FTransform Bone = Partner->GetBoneTransformByName(Aim.Bone, EBoneSpaces::ComponentSpace);
        const FVector Have = (Partner->GetBoneLocationByName(Aim.Child, EBoneSpaces::ComponentSpace) - Bone.GetLocation()).GetSafeNormal();
        const FVector Want = (Aim.To - Aim.From).GetSafeNormal();
        if (Have.IsNearlyZero() || Want.IsNearlyZero()) continue;
        const FQuat Rotation = FQuat::FindBetweenNormals(Have, Want) * Bone.GetRotation();
        Partner->SetBoneRotationByName(Aim.Bone, Rotation.Rotator(), EBoneSpaces::ComponentSpace);
    }
    PartnerPoseError = 0.0f;
    for (const FGratiaPartnerAim& Aim : Scene.PartnerPose)
    {
        const FVector Have = (Partner->GetBoneLocationByName(Aim.Child, EBoneSpaces::ComponentSpace)
            - Partner->GetBoneLocationByName(Aim.Bone, EBoneSpaces::ComponentSpace)).GetSafeNormal();
        const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Have, (Aim.To - Aim.From).GetSafeNormal()), -1.0, 1.0)));
        PartnerPoseError = FMath::Max(PartnerPoseError, Angle);
    }
    Partner->SetVisibility(true);
    Partner->RefreshBoneTransforms();
    PosedFor = &Performance;
    UpdateHiddenBones();
    UE_LOG(LogGratiaStage, Display, TEXT("PERFORMANCE_PARTNER %s mesh=%s aims=%d pose_error=%.2fdeg"), *Performance.Name.ToString(),
        *Scene.PartnerMesh->GetName(), Scene.PartnerPose.Num(), PartnerPoseError);
}

void UGratiaPerformanceStage::UpdateHiddenBones()
{
    const FGratiaPerformanceClip* Performance = Current();
    if (!Partner || !Performance || Performance != PosedFor) return;
    for (const FName Bone : Performance->Scene.PartnerHiddenInViewpoint)
    {
        if (bViewpointActive) Partner->HideBoneByName(Bone, EPhysBodyOp::PBO_None);
        else Partner->UnHideBoneByName(Bone);
    }
}

void UGratiaPerformanceStage::HidePartner()
{
    if (Partner && Partner->IsVisible()) Partner->SetVisibility(false);
    PosedFor = nullptr;
}

void UGratiaPerformanceStage::StopMusic()
{
    if (Music && Music->IsPlaying()) Music->Stop();
}

FString UGratiaPerformanceStage::GetDiagnostics() const
{
    return FString::Printf(TEXT("time=%.2f partner=%d pose_error=%.2f music=%d music_time=%.2f viewpoint=%d"),
        GetPerformanceTime(), Partner && Partner->IsVisible() ? 1 : 0, PartnerPoseError, IsMusicPlaying() ? 1 : 0,
        GetMusicTime(), bViewpointActive ? 1 : 0);
}
