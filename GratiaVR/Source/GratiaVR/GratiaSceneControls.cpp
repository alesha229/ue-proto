#include "GratiaSceneControls.h"
#include "GratiaAmbience.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaSceneDirector.h"
#include "GratiaStage1Runtime.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MotionControllerComponent.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaSceneControls, Log, All);

namespace
{
    AGratiaStage1Runtime* FindRuntime(UWorld* World)
    {
        for (TActorIterator<AGratiaStage1Runtime> It(World); It; ++It) return *It;
        return nullptr;
    }
}

AGratiaSceneControls::AGratiaSceneControls()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Movable);
    RootComponent = Root;
}

void AGratiaSceneControls::BeginPlay()
{
    Super::BeginPlay();
    BuildButtons();
    SetActorHiddenInGame(true);
}

void AGratiaSceneControls::AddButton(EAction Action, int32 Slot, const FLinearColor& Color)
{
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    FButton& Button = Buttons.AddDefaulted_GetRef();
    Button.Action = Action;
    Button.Slot = Slot;
    Button.Color = Color;
    Button.Ball = NewObject<UStaticMeshComponent>(this);
    Button.Ball->SetupAttachment(Root);
    Button.Ball->SetStaticMesh(Sphere);
    Button.Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Button.Ball->SetCastShadow(false);
    Button.Ball->SetRelativeScale3D(FVector(ButtonRadiusCm * 2.0f / 100.0f));
    if (Basic)
    {
        Button.Material = UMaterialInstanceDynamic::Create(Basic, this);
        Button.Material->SetVectorParameterValue(TEXT("Color"), Color);
        Button.Ball->SetMaterial(0, Button.Material);
        Materials.Add(Button.Material);
    }
    Button.Ball->RegisterComponent();
    Balls.Add(Button.Ball);
    Button.Label = NewObject<UWidgetComponent>(this);
    Button.Label->SetupAttachment(Root);
    Button.Label->SetWidgetSpace(EWidgetSpace::World);
    Button.Label->SetDrawSize(FVector2D(240.0f, 64.0f));
    Button.Label->SetRelativeScale3D(FVector(0.045f));
    Button.Label->SetTwoSided(true);
    Button.Label->SetBlendMode(EWidgetBlendMode::Transparent);
    Button.Label->SetBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.55f));
    Button.Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Button.Text = SNew(STextBlock)
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
        .ColorAndOpacity(FLinearColor::White)
        .Justification(ETextJustify::Center)
        .AutoWrapText(true);
    Button.Label->SetSlateWidget(Button.Text);
    Button.Label->RegisterComponent();
    Labels.Add(Button.Label);
}

void AGratiaSceneControls::BuildButtons()
{
    for (UStaticMeshComponent* Ball : Balls) if (Ball) Ball->DestroyComponent();
    for (UWidgetComponent* Label : Labels) if (Label) Label->DestroyComponent();
    Balls.Reset(); Labels.Reset(); Materials.Reset(); Buttons.Reset();
    const UGratiaAmbience* Ambience = UGratiaAmbience::Get(this);
    const int32 Slots = Ambience ? Ambience->GetOutfitSlotCount() : 0;
    BuiltSlots = Slots;
    AddButton(EAction::Light, 0, FLinearColor(1.0f, 0.75f, 0.3f));
    AddButton(EAction::Mirror, 0, FLinearColor(0.6f, 0.85f, 1.0f));
    AddButton(EAction::Pose, 0, FLinearColor(0.55f, 1.0f, 0.6f));
    AddButton(EAction::Archetype, 0, FLinearColor(1.0f, 0.4f, 0.65f));
    for (int32 Slot = 0; Slot < Slots; ++Slot) AddButton(EAction::Outfit, Slot, FLinearColor(0.8f, 0.6f, 1.0f));
    // A row facing the player (actor +X): balls 12 cm apart, labels above them.
    const float Spacing = 12.0f;
    for (int32 I = 0; I < Buttons.Num(); ++I)
    {
        const float Y = (I - (Buttons.Num() - 1) * 0.5f) * Spacing;
        Buttons[I].Ball->SetRelativeLocation(FVector(0.0f, Y, 0.0f));
        Buttons[I].Label->SetRelativeLocation(FVector(0.0f, Y, 6.5f));
    }
    LabelRefresh = 0.0f;
}

FText AGratiaSceneControls::LabelFor(const FButton& Button) const
{
    const UGratiaAmbience* Ambience = UGratiaAmbience::Get(this);
    if (!Ambience) return FText::GetEmpty();
    auto Line = [](const TCHAR* Title, const FText& Value) { return FText::FromString(FString::Printf(TEXT("%s\n%s"), Title, *Value.ToString())); };
    switch (Button.Action)
    {
    case EAction::Light: return Line(TEXT("Свет"), Ambience->GetLightingLabel());
    case EAction::Mirror: return Line(TEXT("Зеркало"), Ambience->GetMirrorLabel());
    case EAction::Pose: return Line(TEXT("Поза"), Ambience->GetPoseLabel());
    case EAction::Archetype: return Line(TEXT("Характер"), Ambience->GetArchetypeLabel());
    case EAction::Outfit: return Line(TEXT("Наряд"), Ambience->GetOutfitLabel(Button.Slot));
    }
    return FText::GetEmpty();
}

void AGratiaSceneControls::Press(FButton& Button, bool bLeft)
{
    UGratiaAmbience* Ambience = UGratiaAmbience::Get(this);
    if (!Ambience) return;
    switch (Button.Action)
    {
    case EAction::Light: Ambience->NextLightingPreset(); break;
    case EAction::Mirror: Ambience->NextMirrorMode(); break;
    case EAction::Pose: Ambience->NextPose(); break;
    case EAction::Archetype: Ambience->NextArchetype(); break;
    case EAction::Outfit: Ambience->NextOutfit(Button.Slot); break;
    }
    Button.Flash = 1.0f;
    Cooldown = 0.35f;
    LabelRefresh = 0.0f;
    // A short click in the pressing hand.
    if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
    {
        const EControllerHand Hand = bLeft ? EControllerHand::Left : EControllerHand::Right;
        Controller->SetHapticsByValue(0.3f, 0.6f, Hand);
        FTimerHandle Timer;
        TWeakObjectPtr<APlayerController> WeakController = Controller;
        GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateLambda([WeakController, Hand]()
        {
            if (WeakController.IsValid()) WeakController->SetHapticsByValue(0.0f, 0.0f, Hand);
        }), 0.08f, false);
    }
    UE_LOG(LogGratiaSceneControls, Display, TEXT("Scene control pressed: %s"), *LabelFor(Button).ToString().Replace(TEXT("\n"), TEXT(": ")));
}

bool AGratiaSceneControls::ShouldShow() const
{
    AGratiaStage1Runtime* Runtime = FindRuntime(GetWorld());
    if (!Runtime) return false;
    if (Runtime->SceneDirector && Runtime->SceneDirector->IsActive() && !Runtime->SceneDirector->IsInScene()) return false;
    const UGratiaAmbience* Ambience = UGratiaAmbience::Get(this);
    const AGratiaPreviewCharacter* Character = Ambience ? Ambience->GetCharacter() : nullptr;
    if (Character && Character->PreviewPose == EGratiaPreviewPose::Performance && !Character->IsStance(Character->PerformanceIndex)) return false;
    return true;
}

void AGratiaSceneControls::Follow(bool bSnap, float DeltaSeconds)
{
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    if (!Controller || !Controller->PlayerCameraManager) return;
    const FVector Head = Controller->PlayerCameraManager->GetCameraLocation();
    FVector Forward = Controller->PlayerCameraManager->GetCameraRotation().Vector();
    Forward = FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
    if (Forward.IsNearlyZero()) return;
    const FVector Left = -FVector::CrossProduct(FVector::UpVector, Forward);
    // Left hip, within reach of either hand.
    const FVector Desired = Head + Forward * 32.0 + Left * 30.0 - FVector(0, 0, 52.0);
    const FRotator Facing = FVector(Head.X - Desired.X, Head.Y - Desired.Y, 0.0).Rotation();
    if (bSnap) { SetActorLocationAndRotation(Desired, Facing); return; }
    // Re-anchor only after the player moved or turned away; then glide there.
    const float Distance = FVector::Dist(GetActorLocation(), Desired);
    const float Turn = FMath::Abs(FRotator::NormalizeAxis(GetActorRotation().Yaw - Facing.Yaw));
    if (Distance > 45.0f || Turn > 50.0f || (Distance > 2.0f && GetActorLocation().Z - Desired.Z > 30.0f))
    {
        SetActorLocation(FMath::VInterpTo(GetActorLocation(), Desired, DeltaSeconds, 3.0f));
        SetActorRotation(FMath::RInterpTo(GetActorRotation(), Facing, DeltaSeconds, 3.0f));
    }
}

void AGratiaSceneControls::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const bool bShow = ShouldShow();
    if (bShow == IsHidden()) SetActorHiddenInGame(!bShow);
    if (!bShow) { bPlaced = false; return; }
    const UGratiaAmbience* Ambience = UGratiaAmbience::Get(this);
    if (Ambience && Ambience->GetOutfitSlotCount() != BuiltSlots) BuildButtons();
    Follow(!bPlaced, DeltaSeconds);
    bPlaced = true;
    Cooldown = FMath::Max(0.0f, Cooldown - DeltaSeconds);

    // Fingertips: a little ahead of each tracked controller.
    TArray<TPair<FVector, bool>> Tips;
    if (AGratiaStage1Runtime* Runtime = FindRuntime(GetWorld()))
        if (APawn* Pawn = Runtime->GetPlayerPawn())
        {
            TArray<UMotionControllerComponent*> Controllers;
            Pawn->GetComponents(Controllers);
            for (const UMotionControllerComponent* Controller : Controllers)
            {
                if (!Controller || !Controller->IsTracked()) continue;
                const FString Source = Controller->MotionSource.ToString();
                if (Source.Contains(TEXT("Aim"))) continue;
                Tips.Emplace(Controller->GetComponentLocation() + Controller->GetForwardVector() * 6.0, Source.Contains(TEXT("Left")));
            }
        }
    for (FButton& Button : Buttons)
    {
        const FVector Center = Button.Ball->GetComponentLocation();
        for (const TPair<FVector, bool>& Tip : Tips)
        {
            const int32 Hand = Tip.Value ? 0 : 1;
            const float Distance = FVector::Dist(Tip.Key, Center);
            if (!Button.bInside[Hand] && Distance < ButtonRadiusCm + TouchMarginCm)
            {
                Button.bInside[Hand] = true;
                if (Cooldown <= 0.0f) Press(Button, Tip.Value);
            }
            else if (Button.bInside[Hand] && Distance > ButtonRadiusCm + TouchMarginCm + 2.0f) Button.bInside[Hand] = false;
        }
        if (Button.Flash > 0.0f)
        {
            Button.Flash = FMath::Max(0.0f, Button.Flash - DeltaSeconds * 4.0f);
            if (Button.Material) Button.Material->SetVectorParameterValue(TEXT("Color"), Button.Color * (1.0f + 2.0f * Button.Flash));
            Button.Ball->SetRelativeScale3D(FVector(ButtonRadiusCm * 2.0f / 100.0f * (1.0f - 0.25f * Button.Flash)));
        }
    }
    LabelRefresh -= DeltaSeconds;
    if (LabelRefresh <= 0.0f)
    {
        LabelRefresh = 0.3f;
        for (const FButton& Button : Buttons) if (Button.Text.IsValid()) Button.Text->SetText(LabelFor(Button));
    }
}
