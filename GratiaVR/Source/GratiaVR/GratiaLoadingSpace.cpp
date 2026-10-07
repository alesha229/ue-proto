#include "GratiaLoadingSpace.h"
#include "GratiaLoadingWidget.h"
#include "GratiaSceneLibrary.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
constexpr int32 GratiaFloatingMotes = 160;
// The engine sphere, cylinder and plane are 100 cm across.
constexpr float GratiaShapeCm = 100.0f;

template <typename T>
T* GratiaQuietComponent(AActor* Owner, FName Name, USceneComponent* Parent)
{
    T* Component = Owner->CreateDefaultSubobject<T>(Name);
    Component->SetupAttachment(Parent);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCastShadow(false);
    return Component;
}
}

AGratiaLoadingSpace::AGratiaLoadingSpace()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;
    Sky = GratiaQuietComponent<UStaticMeshComponent>(this, TEXT("Sky"), Root);
    Grid = GratiaQuietComponent<UStaticMeshComponent>(this, TEXT("Grid"), Root);
    Halo = GratiaQuietComponent<UStaticMeshComponent>(this, TEXT("Halo"), Root);
    Motes = GratiaQuietComponent<UInstancedStaticMeshComponent>(this, TEXT("Motes"), Root);
    Card = CreateDefaultSubobject<UWidgetComponent>(TEXT("Card"));
    Card->SetupAttachment(Root);
    Card->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Card->SetWidgetSpace(EWidgetSpace::World);
    Card->SetDrawSize(FVector2D(1200, 1000));
    Card->SetPivot(FVector2D(0.5, 0.5));
    Card->SetTwoSided(false);
    Card->SetWindowFocusable(false);
    // 2.6 m ahead at eye height, facing the player standing at the centre.
    Card->SetRelativeLocation(FVector(260, 0, 150));
    Card->SetRelativeRotation(FRotator(0, 180, 0));
    Card->SetRelativeScale3D(FVector(0.12f));
    // Sky: 120 m dome around the player; grid: 1.2 km floor so the lines reach the horizon.
    Sky->SetRelativeLocation(FVector(0, 0, 150));
    Sky->SetRelativeScale3D(FVector(24000.0f / GratiaShapeCm));
    Grid->SetRelativeLocation(FVector(0, 0, -0.5f));
    Grid->SetRelativeScale3D(FVector(120000.0f / GratiaShapeCm, 120000.0f / GratiaShapeCm, 1.0f));
    // Glowing disc the player stands on.
    Halo->SetRelativeLocation(FVector(0, 0, 0.3f));
    Halo->SetRelativeScale3D(FVector(150.0f / GratiaShapeCm, 150.0f / GratiaShapeCm, 0.004f));
    SetActorHiddenInGame(true);
}

bool AGratiaLoadingSpace::Configure(const UGratiaSceneLibrary* Library)
{
    UMaterialInterface* SkyMaterial = Library ? Library->LoadingSkyMaterial.LoadSynchronous() : nullptr;
    UMaterialInterface* GlowMaterial = Library ? Library->GlowMaterial.LoadSynchronous() : nullptr;
    UMaterialInterface* GridMaterial = Library ? Library->GridMaterial.LoadSynchronous() : nullptr;
    if (!Library || !Library->SphereMesh || !Library->CylinderMesh || !SkyMaterial || !GlowMaterial) return false;
    Sky->SetStaticMesh(Library->SphereMesh);
    Motes->SetStaticMesh(Library->SphereMesh);
    Halo->SetStaticMesh(Library->CylinderMesh);
    SkyInstance = UMaterialInstanceDynamic::Create(SkyMaterial, this);
    Sky->SetMaterial(0, SkyInstance);
    MoteInstance = UMaterialInstanceDynamic::Create(GlowMaterial, this);
    Motes->SetMaterial(0, MoteInstance);
    HaloInstance = UMaterialInstanceDynamic::Create(GlowMaterial, this);
    Halo->SetMaterial(0, HaloInstance);
    if (Library->PlaneMesh && GridMaterial)
    {
        Grid->SetStaticMesh(Library->PlaneMesh);
        GridInstance = UMaterialInstanceDynamic::Create(GridMaterial, this);
        Grid->SetMaterial(0, GridInstance);
    }
    else Grid->SetVisibility(false);
    CardWidget = NewObject<UGratiaLoadingWidget>(this);
    CardWidget->Library = Library;
    Card->SetWidget(CardWidget);
    // Motes drift up in a shell 3..40 m around.
    MoteData.Reset();
    Motes->ClearInstances();
    FRandomStream Random(466);
    for (int32 I = 0; I < GratiaFloatingMotes; ++I)
    {
        FMote& Mote = MoteData.AddDefaulted_GetRef();
        Mote.Phase = Random.FRandRange(0.0f, 2.0f * PI);
        Mote.Speed = Random.FRandRange(0.3f, 1.0f);
        const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
        const float Distance = Random.FRandRange(300.0f, 4000.0f);
        Mote.Base = FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, Random.FRandRange(0.0f, 1200.0f));
        Mote.Size = Random.FRandRange(2.0f, 6.0f) * (0.6f + Distance / 2500.0f);
        Motes->AddInstance(FTransform(FQuat::Identity, Mote.Base, FVector(Mote.Size / GratiaShapeCm)));
    }
    return true;
}

void AGratiaLoadingSpace::ShowAt(const FVector& Location, float Yaw, const FText& Title, const FText& Subtitle, const FLinearColor& InAccent, UTexture2D* Picture)
{
    SetActorLocationAndRotation(Location, FRotator(0, Yaw, 0));
    Accent = InAccent;
    if (CardWidget) CardWidget->ShowScene(Title, Subtitle, Accent, Picture);
    Card->SetVisibility(!Title.IsEmpty());
    bShown = true;
    ApplyColors();
    SetActorHiddenInGame(false);
    SetActorTickEnabled(true);
    Tick(0.0f);
}

void AGratiaLoadingSpace::HideSpace()
{
    bShown = false;
    SetActorHiddenInGame(true);
    SetActorTickEnabled(false);
}

void AGratiaLoadingSpace::ShowNotice(const FText& Message)
{
    if (!CardWidget) return;
    CardWidget->ShowNotice(Message);
    if (!Message.IsEmpty()) Card->SetVisibility(true);
}

void AGratiaLoadingSpace::SetProgress(float Value)
{
    if (CardWidget) CardWidget->SetProgress(Value);
}

void AGratiaLoadingSpace::ApplyColors()
{
    if (SkyInstance) SkyInstance->SetVectorParameterValue(TEXT("Accent"), Accent);
    if (GridInstance) GridInstance->SetVectorParameterValue(TEXT("Accent"), Accent);
    if (MoteInstance) MoteInstance->SetVectorParameterValue(TEXT("Color"), FLinearColor::LerpUsingHSV(Accent, FLinearColor(0.2f, 0.5f, 1.0f), 0.4f) * 8.0f);
}

void AGratiaLoadingSpace::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bShown) return;
    const float Delta = FMath::IsFinite(DeltaSeconds) ? FMath::Clamp(DeltaSeconds, 0.0f, 0.1f) : 0.0f;
    Time += Delta;
    // Beat pulse: jumps with the music's beat, decays in ~0.2 s.
    Pulse = FMath::Max(Pulse * FMath::Exp(-Delta * 6.0f), Music.W);
    const float Bass = FMath::Clamp(Music.X, 0.0f, 1.0f);
    if (HaloInstance) HaloInstance->SetVectorParameterValue(TEXT("Color"), Accent * (0.8f + 3.0f * Pulse + 1.5f * Bass));
    TArray<FTransform> MoteTransforms;
    MoteTransforms.Reserve(MoteData.Num());
    for (const FMote& Mote : MoteData)
    {
        // Motes drift up (12 m loop) and sway; they flare on the beat.
        FVector Position = Mote.Base;
        Position.X += FMath::Sin(Time * Mote.Speed + Mote.Phase) * 40.0f;
        Position.Y += FMath::Cos(Time * Mote.Speed * 0.8f + Mote.Phase) * 40.0f;
        Position.Z = FMath::Fmod(Mote.Base.Z + Time * Mote.Speed * 25.0f, 1200.0f);
        const float Size = Mote.Size * (0.7f + 0.5f * Bass + 0.8f * Pulse);
        MoteTransforms.Emplace(FQuat::Identity, Position, FVector(Size / GratiaShapeCm));
    }
    Motes->BatchUpdateInstancesTransforms(0, MoteTransforms, false, true, true);
}
