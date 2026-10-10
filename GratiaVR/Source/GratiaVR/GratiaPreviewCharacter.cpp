#include "GratiaPreviewCharacter.h"
#include "GratiaBodySurface.h"
#include "GratiaInteraction.h"
#include "GratiaReactionPresentation.h"
#include "GratiaAnimInstance.h"
#include "GratiaSecondaryMotion.h"
#include "GratiaCharacterProfile.h"
#include "GratiaPenetration.h"
#include "GratiaPerformanceStage.h"
#include "GratiaProceduralFace.h"
#include "GratiaSoftBodyInteraction.h"
#include "GratiaSoftBodyVerification.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogGratiaPreview, Log, All);

AGratiaPreviewCharacter::AGratiaPreviewCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    CharacterMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh"));
    RootComponent = CharacterMesh;
    Interaction = CreateDefaultSubobject<UGratiaInteraction>(TEXT("Interaction"));
    ReactionPresentation = CreateDefaultSubobject<UGratiaReactionPresentation>(TEXT("ReactionPresentation"));
    SecondaryMotion = CreateDefaultSubobject<UGratiaSecondaryMotion>(TEXT("SecondaryMotion"));
    SoftBodyInteraction = CreateDefaultSubobject<UGratiaSoftBodyInteraction>(TEXT("SoftBodyInteraction"));
    SoftBodyVerification = CreateDefaultSubobject<UGratiaSoftBodyVerification>(TEXT("SoftBodyVerification"));
    BodySurface = CreateDefaultSubobject<UGratiaBodySurface>(TEXT("BodySurface"));
    Penetration = CreateDefaultSubobject<UGratiaPenetration>(TEXT("Penetration"));
    PerformanceStage = CreateDefaultSubobject<UGratiaPerformanceStage>(TEXT("PerformanceStage"));
    ProceduralFace = CreateDefaultSubobject<UGratiaProceduralFace>(TEXT("ProceduralFace"));
    CharacterMesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
    CharacterMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    CharacterMesh->SetGenerateOverlapEvents(false);
    CharacterMesh->SetCastShadow(false);
    CharacterMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    CharacterMesh->bEnableUpdateRateOptimizations = false;

}

void AGratiaPreviewCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
#if WITH_EDITOR
    if (!GetWorld() || !GetWorld()->IsGameWorld()) RefreshEditorProfilePreview();
#endif
}

#if WITH_EDITOR
void AGratiaPreviewCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AGratiaPreviewCharacter, CharacterProfile)
        && (!GetWorld() || !GetWorld()->IsGameWorld()))
        RefreshEditorProfilePreview();
}

void AGratiaPreviewCharacter::RefreshEditorProfilePreview()
{
    if (!CharacterMesh || !CharacterProfile) return;
    TArray<FString> Errors, Warnings;
    if (!CharacterProfile->ValidateProfile(Errors, Warnings))
    {
        UE_LOG(LogGratiaPreview, Warning, TEXT("Editor profile preview rejected %s: %s"),
            *CharacterProfile->GetName(), *FString::Join(Errors, TEXT("; ")));
        return;
    }
    // Details edits preview resources only. Gameplay reactions and physical drives
    // are initialized by SetCharacterProfile/BeginPlay in the actual game world.
    if (CharacterMesh->GetSkeletalMeshAsset() != CharacterProfile->Mesh)
        CharacterMesh->SetSkeletalMesh(CharacterProfile->Mesh);
    if (CharacterMesh->GetPhysicsAsset() != CharacterProfile->PhysicsAsset)
        CharacterMesh->SetPhysicsAsset(CharacterProfile->PhysicsAsset);
}
#endif

void AGratiaPreviewCharacter::BeginPlay()
{
    Super::BeginPlay();
    FString RequestedProfile;
    if (FParse::Value(FCommandLine::Get(), TEXT("GratiaCharacterProfile="), RequestedProfile))
        SetCharacterProfile(LoadObject<UGratiaCharacterProfile>(nullptr, *RequestedProfile));
    else if (CharacterProfile) SetCharacterProfile(CharacterProfile);
    else SetCharacterProfile(LoadObject<UGratiaCharacterProfile>(nullptr, TEXT("/Game/Characters/Profiles/DA_Gratia.DA_Gratia")));
    // Existing placed actors may have serialized the old preview collision mode.
    CharacterMesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
    CharacterMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    CharacterMesh->AddTickPrerequisiteActor(this);
    FString RequestedPose;
    FParse::Value(FCommandLine::Get(), TEXT("GratiaPoseTest="), RequestedPose);
    if (RequestedPose.Equals(TEXT("Arms"), ESearchCase::IgnoreCase))
    {
        SetPreviewPose(EGratiaPreviewPose::Arms);
    }
    else if (RequestedPose.Equals(TEXT("Head"), ESearchCase::IgnoreCase))
    {
        SetPreviewPose(EGratiaPreviewPose::Head);
    }
    else if (RequestedPose.Equals(TEXT("Performance"), ESearchCase::IgnoreCase))
    {
        int32 RequestedIndex = 0;
        FParse::Value(FCommandLine::Get(), TEXT("GratiaPerformance="), RequestedIndex);
        if (!SetPerformance(RequestedIndex)) ResetToIdle();
    }
    else
    {
        if (!RequestedPose.IsEmpty())
        {
            UE_LOG(LogGratiaPreview, Warning, TEXT("Unknown GratiaPoseTest=%s; starting neutral idle."), *RequestedPose);
        }
        ResetToIdle();
    }
    int32 Quality = 1;
    FParse::Value(FCommandLine::Get(), TEXT("GratiaQuality="), Quality);
    Interaction->Quality = FMath::Clamp(Quality, 0, 2);
    Interaction->bHairMotion = Interaction->bClothMotion = Quality > 0;
    if (FParse::Param(FCommandLine::Get(), TEXT("GratiaDemo"))) Interaction->bDemo = true;
    UE_LOG(LogGratiaPreview, Display, TEXT("Gratia preview ready. F2=idle/arms/head, F3=reset to idle."));
}

bool AGratiaPreviewCharacter::SetCharacterProfile(UGratiaCharacterProfile* Profile)
{
    if (!Profile) { UE_LOG(LogGratiaPreview, Error, TEXT("Character profile unavailable")); return false; }
    TArray<FString> Errors, Warnings;
    if (!Profile->ValidateProfile(Errors, Warnings))
    {
        for (const FString& Error : Errors) UE_LOG(LogGratiaPreview, Error, TEXT("PROFILE %s: %s"), *Profile->GetName(), *Error);
        return false;
    }
    for (const FString& Warning : Warnings) UE_LOG(LogGratiaPreview, Warning, TEXT("PROFILE %s: %s"), *Profile->GetName(), *Warning);
    if (CharacterProfile && CharacterProfile != Profile)
    {
        const FVector Facing = GetActorRotation().RotateVector(CharacterProfile->ForwardAxis).GetSafeNormal2D();
        const float LocalYaw = Profile->ForwardAxis.Rotation().Yaw;
        SetActorRotation(FRotator(0.0f, Facing.Rotation().Yaw - LocalYaw, 0.0f));
    }
    CharacterMesh->SetAllBodiesSimulatePhysics(false);
    CharacterProfile = Profile;
    CharacterMesh->SetSkeletalMesh(Profile->Mesh);
    CharacterMesh->SetPhysicsAsset(Profile->PhysicsAsset);
    IdleAnimation = Profile->Idle; ArmsAnimation = Profile->Arms; HeadAnimation = Profile->Head;
    SoftReaction = Profile->ReactSoft; BrightReaction = Profile->ReactBright;
    if (Interaction) Interaction->RebuildProfileZones();
    ResetToIdle();
    UE_LOG(LogGratiaPreview, Display, TEXT("CHARACTER PROFILE id=%s mesh=%s bones=%d morphs=%d anim_mode=%s"),
        *Profile->ProfileId.ToString(), *Profile->Mesh->GetPathName(), Profile->Mesh->GetRefSkeleton().GetNum(),
        Profile->Mesh->GetMorphTargets().Num(), Profile->AnimationClass && !Profile->AnimationClass->IsChildOf(UGratiaAnimInstance::StaticClass()) ? TEXT("AnimationBlueprint") : TEXT("Native"));
    return true;
}

FVector AGratiaPreviewCharacter::GetLookTarget() const { return Interaction ? Interaction->LookTarget : GetActorLocation(); }
float AGratiaPreviewCharacter::GetReactionWeight() const { return Interaction ? Interaction->Reaction : 0.0f; }
int32 AGratiaPreviewCharacter::GetReactionSerial() const { return Interaction ? static_cast<int32>(Interaction->ReactionSerial) : 0; }

UAnimSequence* AGratiaPreviewCharacter::GetExpectedAnimation() const
{
    return GetPreviewAnimation(PreviewPose);
}

FName AGratiaPreviewCharacter::MoodName(int32 Mood)
{
    return Mood == 1 ? FName(TEXT("Cheerful")) : Mood == 2 ? FName(TEXT("Reserved")) : FName(TEXT("Calm"));
}

UAnimSequence* AGratiaPreviewCharacter::GetReactionAnimation(FName ZoneName, float HandSpeed, int32 Mood) const
{
    if (!CharacterProfile || !CharacterProfile->Capabilities.bReactionAnimations) return nullptr;
    if (CharacterProfile->StrongReactionClip && FMath::IsFinite(HandSpeed)
        && HandSpeed > CharacterProfile->ContactSettings.StrongReactionSpeedCmPerSecond)
        return CharacterProfile->StrongReactionClip;
    if (const auto* Clip = CharacterProfile->MoodReactionClips.Find(MoodName(Mood)))
        if (Clip->Get()) return Clip->Get();
    return GetReactionAnimationForZone(ZoneName);
}

UAnimSequence* AGratiaPreviewCharacter::GetReactionAnimationForZone(FName ZoneName) const
{
    if (!CharacterProfile || !CharacterProfile->Capabilities.bReactionAnimations) return nullptr;
    if (const auto* Clip = CharacterProfile->ReactionClips.Find(ZoneName)) return Clip->Get();
    if (const auto* Clip = CharacterProfile->ReactionClips.Find(TEXT("Default"))) return Clip->Get();
    // An unconfigured zone uses a restrained cue; approach speed never chooses an open-arm gesture.
    return SoftReaction;
}

const FGratiaPerformanceClip* AGratiaPreviewCharacter::GetPerformance() const
{
    return CharacterProfile && CharacterProfile->PerformanceClips.IsValidIndex(PerformanceIndex)
        && CharacterProfile->PerformanceClips[PerformanceIndex].Clip ? &CharacterProfile->PerformanceClips[PerformanceIndex] : nullptr;
}

UAnimSequence* AGratiaPreviewCharacter::GetPreviewAnimation(EGratiaPreviewPose Pose) const
{
    switch (Pose)
    {
    case EGratiaPreviewPose::Arms: return ArmsAnimation ? ArmsAnimation.Get() : IdleAnimation.Get();
    case EGratiaPreviewPose::Head: return HeadAnimation ? HeadAnimation.Get() : IdleAnimation.Get();
    case EGratiaPreviewPose::Performance:
        if (const FGratiaPerformanceClip* Performance = GetPerformance())
            if (UAnimSequence* Part = Performance->GetPart(PerformancePart)) return Part;
        return IdleAnimation.Get();
    default: return IdleAnimation.Get();
    }
}

FString AGratiaPreviewCharacter::GetPreviewPoseLabel() const
{
    switch (PreviewPose)
    {
    case EGratiaPreviewPose::Arms: return TEXT("Arms");
    case EGratiaPreviewPose::Head: return TEXT("Head");
    case EGratiaPreviewPose::Performance:
        if (const FGratiaPerformanceClip* Performance = GetPerformance())
        {
            const FString Name = Performance->Name.IsNone() ? Performance->Clip->GetName() : Performance->Name.ToString();
            return Performance->Segments.IsEmpty() ? Name : FString::Printf(TEXT("%s %d/%d"), *Name, PerformancePart + 1, Performance->NumParts());
        }
        return TEXT("Performance (missing)");
    default: return TEXT("Idle");
    }
}

FString AGratiaPreviewCharacter::GetPoseMenuLabel() const
{
    switch (PreviewPose)
    {
    case EGratiaPreviewPose::Arms: return TEXT("проверка рук");
    case EGratiaPreviewPose::Head: return TEXT("проверка головы");
    case EGratiaPreviewPose::Performance:
        if (const FGratiaPerformanceClip* Performance = GetPerformance(); Performance && !Performance->Label.IsEmpty())
            return Performance->Label.ToString();
        return GetPreviewPoseLabel();
    default: return TEXT("обычная");
    }
}

bool AGratiaPreviewCharacter::IsStance(int32 Index) const
{
    if (!CharacterProfile || !CharacterProfile->PerformanceClips.IsValidIndex(Index)) return false;
    const FGratiaPerformanceClip& Clip = CharacterProfile->PerformanceClips[Index];
    return Clip.Clip && Clip.bLoop && Clip.Segments.IsEmpty() && !Clip.Scene.Music && !Clip.Scene.PartnerMesh && !Clip.Scene.bHasViewpoint;
}

void AGratiaPreviewCharacter::CycleStance()
{
    int32 Next = PreviewPose == EGratiaPreviewPose::Performance ? PerformanceIndex + 1 : 0;
    while (CharacterProfile && CharacterProfile->PerformanceClips.IsValidIndex(Next) && !IsStance(Next)) ++Next;
    if (IsStance(Next) && SetPerformance(Next)) return;
    SetPreviewPose(EGratiaPreviewPose::Idle);
}

bool AGratiaPreviewCharacter::SetPerformance(int32 Index)
{
    if (!CharacterProfile || !CharacterProfile->PerformanceClips.IsValidIndex(Index) || !CharacterProfile->PerformanceClips[Index].Clip)
    {
        UE_LOG(LogGratiaPreview, Warning, TEXT("Performance %d is not available in profile %s"), Index, *GetNameSafe(CharacterProfile));
        return false;
    }
    PerformanceIndex = Index;
    PerformancePart = 0;
    SetPreviewPose(EGratiaPreviewPose::Performance);
    SetPerformancePlayback(false, PerformanceRate);
    return true;
}

int32 AGratiaPreviewCharacter::FindPerformance(FName Name) const
{
    if (!CharacterProfile || Name.IsNone()) return INDEX_NONE;
    return CharacterProfile->PerformanceClips.IndexOfByPredicate([Name](const FGratiaPerformanceClip& Clip) { return Clip.Name == Name; });
}

int32 AGratiaPreviewCharacter::GetPerformancePartCount() const
{
    const FGratiaPerformanceClip* Performance = PreviewPose == EGratiaPreviewPose::Performance ? GetPerformance() : nullptr;
    return Performance ? Performance->NumParts() : 0;
}

void AGratiaPreviewCharacter::SetPerformancePlayback(bool bPaused, float Rate)
{
    bPerformancePaused = bPaused;
    PerformanceRate = FMath::Clamp(FMath::IsFinite(Rate) ? Rate : 1.0f, 0.25f, 2.0f);
    UAnimSingleNodeInstance* Instance = CharacterMesh ? CharacterMesh->GetSingleNodeInstance() : nullptr;
    if (Instance && PreviewPose == EGratiaPreviewPose::Performance)
    {
        Instance->SetPlayRate(PerformanceRate);
        Instance->SetPlaying(!bPerformancePaused);
    }
    if (PerformanceStage) PerformanceStage->SetPlayback(bPerformancePaused, PerformanceRate);
}

bool AGratiaPreviewCharacter::SeekPerformancePart(int32 Part)
{
    const FGratiaPerformanceClip* Performance = PreviewPose == EGratiaPreviewPose::Performance ? GetPerformance() : nullptr;
    UAnimSingleNodeInstance* Instance = CharacterMesh ? CharacterMesh->GetSingleNodeInstance() : nullptr;
    if (!Performance || !Instance) return false;
    Part = FMath::Clamp(Part, 0, Performance->NumParts() - 1);
    UAnimSequence* Clip = Performance->GetPart(Part);
    if (!Clip) return false;
    PerformancePart = Part;
    Instance->SetAnimationAsset(Clip, Performance->bLoop && Performance->Segments.IsEmpty(), PerformanceRate);
    Instance->SetPosition(0.0f, false);
    Instance->SetPlaying(!bPerformancePaused);
    if (SoftBodyInteraction) SoftBodyInteraction->ResetSoftBody();
    UE_LOG(LogGratiaPreview, Display, TEXT("Performance %s seek part %d/%d"), *GetPreviewPoseLabel(), Part + 1, Performance->NumParts());
    return true;
}

void AGratiaPreviewCharacter::UpdatePerformance()
{
    const FGratiaPerformanceClip* Performance = PreviewPose == EGratiaPreviewPose::Performance ? GetPerformance() : nullptr;
    UAnimSingleNodeInstance* Instance = CharacterMesh ? CharacterMesh->GetSingleNodeInstance() : nullptr;
    if (!Performance || Performance->Segments.IsEmpty() || !Instance || !Instance->GetAnimationAsset() || bPerformancePaused) return;
    const UAnimSequence* Current = Performance->GetPart(PerformancePart);
    if (Instance->GetAnimationAsset() != Current) return;
    if (Instance->IsPlaying() && Instance->GetCurrentTime() < Current->GetPlayLength() - 1.0e-3f) return;
    // Next part; parts share their boundary frame, so the pose continues without a jump.
    int32 Next = PerformancePart + 1;
    if (Next >= Performance->NumParts())
    {
        if (!Performance->bLoop) return;
        Next = 0;
    }
    UAnimSequence* Part = Performance->GetPart(Next);
    if (!Part) return;
    PerformancePart = Next;
    Instance->SetAnimationAsset(Part, false, PerformanceRate);
    Instance->SetPlaying(true);
    Instance->SetPosition(0.0f, false);
    UE_LOG(LogGratiaPreview, Display, TEXT("Performance %s part %d/%d %s"), *GetPreviewPoseLabel(), PerformancePart + 1,
        Performance->NumParts(), *Part->GetName());
}

void AGratiaPreviewCharacter::SetPreviewPose(EGratiaPreviewPose Pose)
{
    if (Pose == EGratiaPreviewPose::Performance && !GetPerformance()) Pose = EGratiaPreviewPose::Idle;
    PreviewPose = Pose;
    if (SoftBodyInteraction) SoftBodyInteraction->ResetSoftBody();
    BlinkElapsed = -1.0f;
    UntilNextBlink = 2.2f;
    CharacterMesh->ClearMorphTargets();
    UAnimSequence* Animation = GetExpectedAnimation();
    if (!CharacterProfile || !CharacterMesh->GetSkeletalMeshAsset()) return;
    if (CharacterProfile->AnimationClass && !CharacterProfile->AnimationClass->IsChildOf(UGratiaAnimInstance::StaticClass()))
    {
        CharacterMesh->SetAnimInstanceClass(CharacterProfile->AnimationClass);
        if (SecondaryMotion) SecondaryMotion->ResetPhysics();
        return;
    }
    if (!Animation)
    {
        CharacterMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        CharacterMesh->SetAnimation(nullptr);
        CharacterMesh->TickAnimation(0.0f, false); CharacterMesh->RefreshBoneTransforms();
        if (SecondaryMotion) SecondaryMotion->ResetPhysics();
        UE_LOG(LogGratiaPreview, Display, TEXT("Profile %s uses reference pose: no preview clip"), *CharacterProfile->GetName());
        return;
    }

    const bool bPlaying = IsAnimatedPreview();
    const FGratiaPerformanceClip* Performance = Pose == EGratiaPreviewPose::Performance ? GetPerformance() : nullptr;
    // A segmented performance loops as a whole; each part plays once.
    const bool bLoop = IsIdlePreview() || (Performance && Performance->bLoop && Performance->Segments.IsEmpty());
    // The diagnostic clips hold their largest pose at the midpoint, avoiding screenshot timing races.
    const float Position = bPlaying ? 0.0f : Animation->GetPlayLength() * 0.5f;
    CharacterMesh->SetAnimInstanceClass(CharacterProfile->AnimationClass ? CharacterProfile->AnimationClass.Get() : UGratiaAnimInstance::StaticClass());
    // OverrideAnimationData serializes state but does not refresh an existing SingleNode instance.
    if (UAnimSingleNodeInstance* Instance = CharacterMesh->GetSingleNodeInstance())
    {
        Instance->SetAnimationAsset(Animation, bLoop, 1.0f);
        Instance->SetPlaying(bPlaying);
        Instance->SetPosition(Position, false);
        CharacterMesh->TickAnimation(0.0f, false);
        CharacterMesh->RefreshBoneTransforms();
    }
    if (SecondaryMotion) SecondaryMotion->ResetPhysics();
    UE_LOG(LogGratiaPreview, Display, TEXT("Preview pose=%s animation=%s time=%.3f mode=%s"), *GetPreviewPoseLabel(),
        *Animation->GetPathName(), Position, bPlaying ? (bLoop ? TEXT("loop") : TEXT("once")) : TEXT("fixed pose"));
}

void AGratiaPreviewCharacter::ResetToIdle()
{
    if (Interaction) Interaction->ResetState();
    if (Penetration) Penetration->ResetPenetration();
    SetPreviewPose(EGratiaPreviewPose::Idle);
}

void AGratiaPreviewCharacter::CyclePreviewPose()
{
    switch (PreviewPose)
    {
    case EGratiaPreviewPose::Idle: SetPreviewPose(EGratiaPreviewPose::Arms); break;
    case EGratiaPreviewPose::Arms: SetPreviewPose(EGratiaPreviewPose::Head); break;
    // After the diagnostic poses come the profile's performance clips, then idle again.
    case EGratiaPreviewPose::Head:
        if (!CharacterProfile || CharacterProfile->PerformanceClips.IsEmpty() || !SetPerformance(0)) ResetToIdle();
        break;
    case EGratiaPreviewPose::Performance:
    {
        int32 Next = PerformanceIndex + 1;
        while (CharacterProfile && CharacterProfile->PerformanceClips.IsValidIndex(Next) && !CharacterProfile->PerformanceClips[Next].Clip) ++Next;
        if (!CharacterProfile || !CharacterProfile->PerformanceClips.IsValidIndex(Next) || !SetPerformance(Next)) ResetToIdle();
        break;
    }
    default: ResetToIdle(); break;
    }
}

void AGratiaPreviewCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (IsIdlePreview()) UpdateBlink(DeltaSeconds);
    UpdatePerformance();
}

void AGratiaPreviewCharacter::UpdateBlink(float DeltaSeconds)
{
    if (!CharacterProfile || !CharacterProfile->Capabilities.bBlink) return;
    if (const auto* Animation = CharacterProfile->bAuthoredReactionFacialCurves ? Cast<UGratiaAnimInstance>(CharacterMesh->GetAnimInstance()) : nullptr)
        if (Animation->IsReactionCuePlaying())
        {
            CharacterMesh->SetMorphTarget(CharacterProfile->ResolveMorph(TEXT("BlinkLeft")), 0.0f, true);
            CharacterMesh->SetMorphTarget(CharacterProfile->ResolveMorph(TEXT("BlinkRight")), 0.0f, true);
            BlinkElapsed = -1.0f; UntilNextBlink = 1.0f;
            return;
        }
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
    const float Step = FMath::Min(DeltaSeconds, 0.1f);
    if (BlinkElapsed < 0.0f)
    {
        UntilNextBlink -= Step;
        if (UntilNextBlink > 0.0f) return;
        BlinkElapsed = 0.0f;
    }
    BlinkElapsed += Step;
    constexpr float CloseSeconds = 0.075f;
    constexpr float HoldSeconds = 0.025f;
    constexpr float OpenSeconds = 0.13f;
    const float OpenStart = CloseSeconds + HoldSeconds;
    float Weight = 0.0f;
    if (BlinkElapsed < CloseSeconds)
    {
        Weight = FMath::SmoothStep(0.0f, CloseSeconds, BlinkElapsed);
    }
    else if (BlinkElapsed < OpenStart)
    {
        Weight = 1.0f;
    }
    else if (BlinkElapsed < OpenStart + OpenSeconds)
    {
        Weight = 1.0f - FMath::SmoothStep(OpenStart, OpenStart + OpenSeconds, BlinkElapsed);
    }
    else
    {
        BlinkElapsed = -1.0f;
        UntilNextBlink = 3.7f;
    }
    CharacterMesh->SetMorphTarget(CharacterProfile->ResolveMorph(TEXT("BlinkLeft")), Weight);
    CharacterMesh->SetMorphTarget(CharacterProfile->ResolveMorph(TEXT("BlinkRight")), Weight);
}
