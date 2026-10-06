#include "GratiaMenu.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaStage1Runtime.h"
#include "GratiaLocomotion.h"
#include "Camera/CameraComponent.h"
#include "Components/TextRenderComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedInputComponent.h"
#include "GratiaCharacterProfile.h"
#include "GratiaPerformanceStage.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaMenu, Log, All);
constexpr int32 MenuRows = 16;
UGratiaMenu::UGratiaMenu()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    static ConstructorHelpers::FObjectFinder<UInputAction> A(TEXT("/Game/Gratia/Input/IA_MenuToggle.IA_MenuToggle"));
    static ConstructorHelpers::FObjectFinder<UInputAction> B(TEXT("/Game/Gratia/Input/IA_MenuNext.IA_MenuNext"));
    static ConstructorHelpers::FObjectFinder<UInputAction> C(TEXT("/Game/Gratia/Input/IA_MenuApply.IA_MenuApply"));
    static ConstructorHelpers::FObjectFinder<UInputMappingContext> D(TEXT("/Game/Gratia/Input/IMC_GratiaMenu.IMC_GratiaMenu"));
    ToggleAction=A.Object; NextAction=B.Object; ApplyAction=C.Object; MenuMapping=D.Object;
}
void UGratiaMenu::BeginPlay()
{
    Super::BeginPlay();
    Text = NewObject<UTextRenderComponent>(GetOwner(), TEXT("GratiaSettingsMenu"));
    GetOwner()->AddInstanceComponent(Text);
    Text->SetupAttachment(GetOwner()->GetRootComponent());
    Text->SetWorldSize(3.2f); Text->SetTextRenderColor(FColor(210,245,240));
    Text->SetHorizontalAlignment(EHTA_Center); Text->SetVerticalAlignment(EVRTA_TextTop);
    Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Text->RegisterComponent(); Text->SetVisibility(false);
    UE_LOG(LogGratiaMenu,Display,TEXT("World menu: Y/B or F4=open; A/X=next/previous; right trigger or Enter=apply."));
}
void UGratiaMenu::SetCharacter(AGratiaPreviewCharacter* Value)
{
    Character = Value;
    if (Character.IsValid()) ApplyQuality(Character->Interaction->Quality, false);
    Refresh();
}
void UGratiaMenu::BindInput(APlayerController* Controller)
{
    if (Controller == BoundController.Get()) return;
    if (ActionInput && BoundController.IsValid()) BoundController->PopInputComponent(ActionInput);
    if (ActionInput) ActionInput->DestroyComponent();
    ActionInput = nullptr; BoundController = Controller;
    if (!Controller || !ToggleAction || !NextAction || !ApplyAction) return;
    ActionInput = NewObject<UEnhancedInputComponent>(Controller, TEXT("GratiaMenuActions"));
    ActionInput->Priority = 110; ActionInput->bBlockInput = false;
    ActionInput->BindActionValue(ToggleAction); ActionInput->BindActionValue(NextAction); ActionInput->BindActionValue(ApplyAction);
    ActionInput->RegisterComponent(); Controller->PushInputComponent(ActionInput);
}
void UGratiaMenu::EndPlay(const EEndPlayReason::Type Reason)
{
    BindInput(nullptr);
    Super::EndPlay(Reason);
}
const TCHAR* UGratiaMenu::ViewLabel() const
{
    const auto* R = Cast<AGratiaStage1Runtime>(GetOwner());
    if (R && R->IsPartnerView()) return TEXT("partner eyes (lie down, Recenter)");
    FTransform Eye;
    const bool bAvailable = Character.IsValid() && Character->PerformanceStage && Character->PerformanceStage->GetViewpoint(Eye);
    return bAvailable ? TEXT("free (apply: partner eyes)") : TEXT("free (no partner in this pose)");
}
FString UGratiaMenu::QualityLabel() const
{
    int32 P = Character.IsValid() ? Character->Interaction->Quality : 1;
    return P==0 ? TEXT("Low") : P==2 ? TEXT("High") : TEXT("Medium");
}
void UGratiaMenu::Toggle()
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this,0);
    APawn* P = PC ? PC->GetPawn() : nullptr;
    UCameraComponent* Camera = P ? P->FindComponentByClass<UCameraComponent>() : nullptr;
    if (!Text || !Camera) return;
    bOpen=!bOpen; Text->SetVisibility(bOpen);
    if (bOpen)
    {
        FRotator Yaw(0,Camera->GetComponentRotation().Yaw,0);
        FVector Position=Camera->GetComponentLocation()+Yaw.Vector()*180.0f+FVector(0,0,25);
        Text->SetWorldLocation(Position); Text->SetWorldRotation((Camera->GetComponentLocation()-Position).Rotation());
    }
    if (PC->GetLocalPlayer() && MenuMapping)
    {
        auto* Input=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
        if (Input) { if (bOpen) Input->AddMappingContext(MenuMapping,100); else Input->RemoveMappingContext(MenuMapping); }
    }
    if (auto* Runtime=Cast<AGratiaStage1Runtime>(GetOwner())) Runtime->Locomotion->bEnabled=!bOpen;
    Refresh();
}
void UGratiaMenu::ApplyQuality(int32 Profile, bool bResetMotion)
{
    if (!Character.IsValid()) return;
    auto* I=Character->Interaction.Get(); I->Quality=Profile;
    if (bResetMotion) { I->bBodyMotion=true; I->bEarMotion=true; I->bHairMotion=Profile>0; I->bClothMotion=Profile>0; I->bLocalSpring=true; }
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    if (PC)
    {
        PC->ConsoleCommand(FString::Printf(TEXT("r.ScreenPercentage %d"),Profile==0?70:Profile==1?85:100),false);
        PC->ConsoleCommand(FString::Printf(TEXT("sg.EffectsQuality %d"),Profile),false);
    }
    if (GEngine && GEngine->XRSystem.IsValid())
        if (IHeadMountedDisplay* HMD = GEngine->XRSystem->GetHMDDevice())
            HMD->SetPixelDensity(Profile==0?0.70f:Profile==1?0.85f:1.0f);
    UE_LOG(LogGratiaMenu,Display,TEXT("Quality=%s; hardware performance acceptance pending"),*QualityLabel());
}
void UGratiaMenu::ApplySelected()
{
    if (!Character.IsValid()) return;
    auto* I=Character->Interaction.Get();
    auto* R=Cast<AGratiaStage1Runtime>(GetOwner());
    switch(Selected)
    {
    case 0: Character->CyclePreviewPose(); break;
    case 1: I->Mood=(I->Mood+1)%3; break;
    case 2: I->bDemo=!I->bDemo; break;
    case 3: if(R) R->Recenter(); break;
    case 4: if(R) R->AdjustHeight(R->HeightStepCm); break;
    case 5: if(R) R->AdjustHeight(-R->HeightStepCm); break;
    case 6: Character->ResetToIdle(); if(R) R->ResetHeight(); break;
    case 7: ApplyQuality((I->Quality+1)%3); break;
    case 8: I->bHairMotion=!I->bHairMotion; break;
    case 9: I->bClothMotion=!I->bClothMotion; break;
    case 10: I->bBodyMotion=!I->bBodyMotion; break;
    case 11: I->bLocalSpring=!I->bLocalSpring; break;
    case 12: I->bPhysicalMotion=!I->bPhysicalMotion; break;
    case 13: I->bSound=!I->bSound; break;
    case 14: I->bEarMotion=!I->bEarMotion; break;
    case 15: if(R) R->SetPartnerView(!R->IsPartnerView()); break;
    }
    Refresh();
}
void UGratiaMenu::Refresh()
{
    if(!Text || !Character.IsValid()) return;
    auto* I=Character->Interaction.Get();
    const TCHAR* Mood=I->Mood==0?TEXT("Calm"):I->Mood==1?TEXT("Cheerful"):TEXT("Reserved");
    const FString Pose=Character->GetPreviewPoseLabel();
    TArray<FString> Rows={
        FString::Printf(TEXT("Pose: %s"),*Pose),FString::Printf(TEXT("Mood: %s"),Mood),FString::Printf(TEXT("Demo: %s"),I->bDemo?TEXT("ON"):TEXT("OFF")),
        TEXT("Recenter"),TEXT("Eye height +2 cm"),TEXT("Eye height -2 cm"),TEXT("Reset pose / contacts / height"),TEXT("Quality: ")+QualityLabel(),
        FString::Printf(TEXT("Hair motion: %s"),I->bHairMotion?TEXT("ON"):TEXT("OFF")),FString::Printf(TEXT("Cloth motion: %s"),I->bClothMotion?TEXT("ON"):TEXT("OFF")),
        FString::Printf(TEXT("Body motion: %s"),I->bBodyMotion?TEXT("ON"):TEXT("OFF")),FString::Printf(TEXT("Local springs: %s"),I->bLocalSpring?TEXT("ON"):TEXT("OFF")),
        FString::Printf(TEXT("Physical animation: %s"),I->bPhysicalMotion?TEXT("ON"):TEXT("OFF")), FString::Printf(TEXT("Sound: %s"),I->bSound?TEXT("ON"):TEXT("OFF")),
        FString::Printf(TEXT("Ears / tail motion: %s"),I->bEarMotion?TEXT("ON"):TEXT("OFF")),
        FString::Printf(TEXT("View: %s"),ViewLabel())
    };
    if (Character->CharacterProfile)
    {
        const auto& Caps = Character->CharacterProfile->Capabilities;
        const bool Secondary = Caps.bSecondaryPhysics || Caps.bLocalSprings;
        if (!Secondary || !Character->CharacterProfile->SecondaryBones.ContainsByPredicate([](const auto& Bone){return Bone.Group == 1;})) Rows[8]=TEXT("Hair motion: unavailable");
        if (!Secondary || !Character->CharacterProfile->SecondaryBones.ContainsByPredicate([](const auto& Bone){return Bone.Group == 2;})) Rows[9]=TEXT("Cloth motion: unavailable");
        if (!Secondary || !Character->CharacterProfile->SecondaryBones.ContainsByPredicate([](const auto& Bone){return Bone.Group == 3;})) Rows[10]=TEXT("Body motion: unavailable");
        if (!Caps.bLocalSprings) Rows[11]=TEXT("Local springs: unavailable");
        if (!Caps.bSecondaryPhysics) Rows[12]=TEXT("Physical animation: unavailable");
        if (!Caps.bSound) Rows[13]=TEXT("Sound: unavailable");
        if (!Secondary || !Character->CharacterProfile->SecondaryBones.ContainsByPredicate([](const auto& Bone){return Bone.Group == 4;})) Rows[14]=TEXT("Ears / tail: unavailable");
    }
    FString Label=TEXT("GRATIA / SETTINGS\nY/B: close | A/X: next/previous\nRight trigger: apply | F4/arrows/Enter\n\n");
    for(int32 Row=0;Row<Rows.Num();++Row) Label+=(Row==Selected?TEXT("> "):TEXT("  "))+Rows[Row]+TEXT("\n");
    Text->SetText(FText::FromString(Label));
}
void UGratiaMenu::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta,Type,Tick);
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    BindInput(PC);
    if(!ActionInput || !PC) return;
    bool ToggleInput=ToggleAction && ActionInput->GetBoundActionValue(ToggleAction).Get<bool>();
    if(!ToggleInput) bToggleArmed=true;
    if((ToggleInput && bToggleArmed)||PC->WasInputKeyJustPressed(EKeys::F4)){Toggle();bToggleArmed=false;}
    if(!bOpen) return;
    float Next=NextAction?ActionInput->GetBoundActionValue(NextAction).Get<float>():0.0f;
    float Apply=ApplyAction?ActionInput->GetBoundActionValue(ApplyAction).Get<float>():0.0f;
    if(FMath::Abs(Next)<0.2f) bNextArmed=true;
    if(Apply<0.2f) bApplyArmed=true;
    if((bNextArmed && FMath::Abs(Next)>=0.5f)||PC->WasInputKeyJustPressed(EKeys::Down)||PC->WasInputKeyJustPressed(EKeys::Up))
    {
        int32 Direction=Next<0 || PC->WasInputKeyJustPressed(EKeys::Up)?-1:1;
        Selected=(Selected+Direction+MenuRows)%MenuRows;bNextArmed=false;Refresh();
    }
    if((bApplyArmed && Apply>0.65f)||PC->WasInputKeyJustPressed(EKeys::Enter)){ApplySelected();bApplyArmed=false;}
}
bool UGratiaMenu::RunChecks()
{
    if(!Character.IsValid()||!ToggleAction||!NextAction||!ApplyAction||!MenuMapping) return false;
    for(int32 I=0;I<10;++I){Selected=0;ApplySelected();Selected=1;ApplySelected();}
    Selected=6;ApplySelected();
    bool Pass=Character->IsIdlePreview() && Character->Interaction->ActiveZone==INDEX_NONE;
    for(int32 Profile=0;Profile<3;++Profile){ApplyQuality(Profile);Pass&=Character->Interaction->Quality==Profile;}
    ApplyQuality(1);Character->Interaction->Mood=0;
    Toggle();FVector Position=Text->GetComponentLocation();Pass&=bOpen && !Cast<AGratiaStage1Runtime>(GetOwner())->Locomotion->bEnabled;
    Toggle();Pass&=!bOpen && Text->GetComponentLocation().Equals(Position,0.001);Selected=0;
    UE_LOG(LogGratiaMenu,Display,TEXT("MENU SELFTEST: pose/mood/reset/quality/world anchor=%s"),Pass?TEXT("PASS"):TEXT("FAIL"));
    return Pass;
}
