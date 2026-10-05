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
FString UGratiaHandInput::GetDiagnostics() const
{
    return FString::Printf(TEXT("Grab input: %s L=%.2f R=%.2f"), bReady ? TEXT("ready") : TEXT("missing player/action/context"), GetTrigger(true), GetTrigger(false));
}
void UGratiaHandInput::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Input && Controller.IsValid()) Controller->PopInputComponent(Input);
    if (Input) Input->DestroyComponent();
    Input = nullptr; bReady = false;
    Super::EndPlay(Reason);
}
