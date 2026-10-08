#include "GratiaHandInput.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

UGratiaHandInput::UGratiaHandInput()
{
    PrimaryComponentTick.bCanEverTick = false;
    static ConstructorHelpers::FObjectFinder<UInputAction> Left(TEXT("/Game/Gratia/Input/IA_GrabLeft.IA_GrabLeft"));
    static ConstructorHelpers::FObjectFinder<UInputAction> Right(TEXT("/Game/Gratia/Input/IA_GrabRight.IA_GrabRight"));
    static ConstructorHelpers::FObjectFinder<UInputMappingContext> Context(TEXT("/Game/Gratia/Input/IMC_GratiaLocomotion.IMC_GratiaLocomotion"));
    LeftAction = Left.Object; RightAction = Right.Object; Mapping = Context.Object;
    static ConstructorHelpers::FObjectFinder<UInputAction> GL(TEXT("/Game/XRFramework/Input/Actions/Hands/IA_Hand_Grasp_Left.IA_Hand_Grasp_Left"));
    static ConstructorHelpers::FObjectFinder<UInputAction> GR(TEXT("/Game/XRFramework/Input/Actions/Hands/IA_Hand_Grasp_Right.IA_Hand_Grasp_Right"));
    static ConstructorHelpers::FObjectFinder<UInputAction> IL(TEXT("/Game/XRFramework/Input/Actions/Hands/IA_Hand_IndexCurl_Left.IA_Hand_IndexCurl_Left"));
    static ConstructorHelpers::FObjectFinder<UInputAction> IR(TEXT("/Game/XRFramework/Input/Actions/Hands/IA_Hand_IndexCurl_Right.IA_Hand_IndexCurl_Right"));
    GraspLeft = GL.Object; GraspRight = GR.Object; IndexLeft = IL.Object; IndexRight = IR.Object;
    static ConstructorHelpers::FObjectFinder<UInputAction> PL(TEXT("/Game/Gratia/Input/IA_GripLeft.IA_GripLeft"));
    static ConstructorHelpers::FObjectFinder<UInputAction> PR(TEXT("/Game/Gratia/Input/IA_GripRight.IA_GripRight"));
    GripLeft = PL.Object; GripRight = PR.Object;
    static ConstructorHelpers::FObjectFinder<UInputAction> RC(TEXT("/Game/Gratia/Input/IA_Recenter.IA_Recenter"));
    RecenterAction = RC.Object;
}
void UGratiaHandInput::UpdateInput()
{
    auto* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (PC != Controller.Get())
    {
        if (Input && Controller.IsValid()) Controller->PopInputComponent(Input);
        if (Input) Input->DestroyComponent();
        Input = nullptr; Controller = PC;
        if (PC && LeftAction && RightAction)
        {
            Input = NewObject<UEnhancedInputComponent>(PC, TEXT("GratiaHandActions"));
            Input->Priority = 70; Input->bBlockInput = false;
            Input->BindActionValue(LeftAction); Input->BindActionValue(RightAction);
            for (UInputAction* Action : {GripLeft.Get(), GripRight.Get(), GraspLeft.Get(), GraspRight.Get(), IndexLeft.Get(), IndexRight.Get(), RecenterAction.Get()})
                if (Action) Input->BindActionValue(Action);
            Input->RegisterComponent(); PC->PushInputComponent(Input);
        }
    }
    auto* Subsystem = PC && PC->GetLocalPlayer() ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
    bReady = Input && Subsystem && Mapping && Subsystem->HasMappingContext(Mapping);
}
float UGratiaHandInput::GetTrigger(bool bLeft) const
{
    UInputAction* Action = bLeft ? LeftAction.Get() : RightAction.Get();
    const float Value = bReady && Action ? Input->GetBoundActionValue(Action).Get<float>() : 0;
    return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : 0;
}
float UGratiaHandInput::ReadAction(UInputAction* Action, bool bLeft) const
{
    if (!bReady || !Action || !Input) return GetTrigger(bLeft);
    const FInputActionValue Value = Input->GetBoundActionValue(Action);
    const float Scalar = Value.GetValueType() == EInputActionValueType::Boolean ? (Value.Get<bool>() ? 1.0f : 0.0f) : Value.Get<float>();
    // Desktop Z/X drive the trigger only; use it so the hand pose can be tested there.
    return FMath::IsFinite(Scalar) ? FMath::Max(FMath::Clamp(Scalar, 0.0f, 1.0f), GetTrigger(bLeft)) : GetTrigger(bLeft);
}
float UGratiaHandInput::GetGrip(bool bLeft) const
{
    UInputAction* Action = bLeft ? GripLeft.Get() : GripRight.Get();
    if (!bReady || !Action || !Input) return 0.0f;
    const FInputActionValue Value = Input->GetBoundActionValue(Action);
    const float Scalar = Value.GetValueType() == EInputActionValueType::Boolean ? (Value.Get<bool>() ? 1.0f : 0.0f) : Value.Get<float>();
    return FMath::IsFinite(Scalar) ? FMath::Clamp(Scalar, 0.0f, 1.0f) : 0.0f;
}
bool UGratiaHandInput::IsRecenterPressed() const
{
    return bReady && RecenterAction && Input && Input->GetBoundActionValue(RecenterAction).Get<bool>();
}
float UGratiaHandInput::GetGrasp(bool bLeft) const { return FMath::Max(GetGrip(bLeft), ReadAction(bLeft ? GraspLeft.Get() : GraspRight.Get(), bLeft)); }
float UGratiaHandInput::GetIndexCurl(bool bLeft) const { return ReadAction(bLeft ? IndexLeft.Get() : IndexRight.Get(), bLeft); }
FString UGratiaHandInput::GetDiagnostics() const
{
    return FString::Printf(TEXT("Grab input: %s trigger L=%.2f R=%.2f grip L=%.2f R=%.2f"), bReady ? TEXT("ready") : TEXT("missing player/action/context"),
        GetTrigger(true), GetTrigger(false), GetGrip(true), GetGrip(false));
}
void UGratiaHandInput::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Input && Controller.IsValid()) Controller->PopInputComponent(Input);
    if (Input) Input->DestroyComponent();
    Input = nullptr; bReady = false;
    Super::EndPlay(Reason);
}
