#include "GratiaPreviewCharacter.h"
#include "GratiaInteraction.h"
#include "GratiaAnimInstance.h"
#include "GratiaSecondaryMotion.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaPreview, Log, All);

AGratiaPreviewCharacter::AGratiaPreviewCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    CharacterMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh"));
    RootComponent = CharacterMesh;
    Interaction = CreateDefaultSubobject<UGratiaInteraction>(TEXT("Interaction"));
    SecondaryMotion = CreateDefaultSubobject<UGratiaSecondaryMotion>(TEXT("SecondaryMotion"));
    CharacterMesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
    CharacterMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    CharacterMesh->SetGenerateOverlapEvents(false);
    CharacterMesh->SetCastShadow(false);
    CharacterMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    CharacterMesh->bEnableUpdateRateOptimizations = false;

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(TEXT("/Game/Gratia/GameRig/SK_Gratia_Game.SK_Gratia_Game"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleAsset(TEXT("/Game/Gratia/GameRig/A_Gratia_Game_Idle.A_Gratia_Game_Idle"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> ArmsAsset(TEXT("/Game/Gratia/GameRig/A_Gratia_Game_TestArms.A_Gratia_Game_TestArms"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> HeadAsset(TEXT("/Game/Gratia/GameRig/A_Gratia_Game_TestHead.A_Gratia_Game_TestHead"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> SoftAsset(TEXT("/Game/Gratia/GameRig/A_Gratia_Game_ReactSoft.A_Gratia_Game_ReactSoft"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> BrightAsset(TEXT("/Game/Gratia/GameRig/A_Gratia_Game_ReactBright.A_Gratia_Game_ReactBright"));
    CharacterMesh->SetSkeletalMesh(MeshAsset.Object);
    IdleAnimation = IdleAsset.Object;
    ArmsAnimation = ArmsAsset.Object;
    HeadAnimation = HeadAsset.Object;
    SoftReaction = SoftAsset.Object;
    BrightReaction = BrightAsset.Object;
}

void AGratiaPreviewCharacter::BeginPlay()
{
    Super::BeginPlay();
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

UAnimSequence* AGratiaPreviewCharacter::GetExpectedAnimation() const
{
    return GetPreviewAnimation(PreviewPose);
}

UAnimSequence* AGratiaPreviewCharacter::GetPreviewAnimation(EGratiaPreviewPose Pose) const
{
    switch (Pose)
    {
    case EGratiaPreviewPose::Arms: return ArmsAnimation.Get();
    case EGratiaPreviewPose::Head: return HeadAnimation.Get();
    default: return IdleAnimation.Get();
    }
}

void AGratiaPreviewCharacter::SetPreviewPose(EGratiaPreviewPose Pose)
{
    PreviewPose = Pose;
    BlinkElapsed = -1.0f;
    UntilNextBlink = 2.2f;
    CharacterMesh->ClearMorphTargets();
    UAnimSequence* Animation = GetExpectedAnimation();
    if (!CharacterMesh->GetSkeletalMeshAsset() || !Animation)
    {
        UE_LOG(LogGratiaPreview, Error, TEXT("Missing cooked preview mesh or animation for pose %d."), static_cast<int32>(Pose));
        return;
    }

    const bool bIdle = IsIdlePreview();
    // The diagnostic clips hold their largest pose at the midpoint, avoiding screenshot timing races.
    const float Position = bIdle ? 0.0f : Animation->GetPlayLength() * 0.5f;
    CharacterMesh->SetAnimInstanceClass(UGratiaAnimInstance::StaticClass());
    // OverrideAnimationData serializes state but does not refresh an existing SingleNode instance.
    if (UAnimSingleNodeInstance* Instance = CharacterMesh->GetSingleNodeInstance())
    {
        Instance->SetAnimationAsset(Animation, bIdle, 1.0f);
        Instance->SetPlaying(bIdle);
        Instance->SetPosition(Position, false);
        CharacterMesh->TickAnimation(0.0f, false);
        CharacterMesh->RefreshBoneTransforms();
    }
    if (SecondaryMotion) SecondaryMotion->ResetPhysics();
    UE_LOG(LogGratiaPreview, Display, TEXT("Preview pose=%s animation=%s time=%.3f mode=%s"),
        bIdle ? TEXT("Idle") : Pose == EGratiaPreviewPose::Arms ? TEXT("Arms") : TEXT("Head"),
        *Animation->GetPathName(), Position, bIdle ? TEXT("loop") : TEXT("fixed pose"));
}

void AGratiaPreviewCharacter::ResetToIdle()
{
    if (Interaction) Interaction->ResetState();
    SetPreviewPose(EGratiaPreviewPose::Idle);
}

void AGratiaPreviewCharacter::CyclePreviewPose()
{
    switch (PreviewPose)
    {
    case EGratiaPreviewPose::Idle: SetPreviewPose(EGratiaPreviewPose::Arms); break;
    case EGratiaPreviewPose::Arms: SetPreviewPose(EGratiaPreviewPose::Head); break;
    default: ResetToIdle(); break;
    }
}

void AGratiaPreviewCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
    {
        if (Controller->WasInputKeyJustPressed(EKeys::F2)) CyclePreviewPose();
        if (Controller->WasInputKeyJustPressed(EKeys::F3)) ResetToIdle();
    }
    if (IsIdlePreview()) UpdateBlink(DeltaSeconds);
}

void AGratiaPreviewCharacter::UpdateBlink(float DeltaSeconds)
{
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
    CharacterMesh->SetMorphTarget(TEXT("Eye L close"), Weight);
    CharacterMesh->SetMorphTarget(TEXT("Eye R close"), Weight);
}
