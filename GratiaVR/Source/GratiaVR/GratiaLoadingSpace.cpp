#include "GratiaLoadingSpace.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
constexpr int32 GratiaHaloBeads = 48;
constexpr int32 GratiaFloatingMotes = 140;
constexpr int32 GratiaRingBeads = 24;
// The engine sphere and cylinder are 100 cm across.
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
    Floor = GratiaQuietComponent<UStaticMeshComponent>(this, TEXT("Floor"), Root);
    Motes = GratiaQuietComponent<UInstancedStaticMeshComponent>(this, TEXT("Motes"), Root);
    Ring = GratiaQuietComponent<UInstancedStaticMeshComponent>(this, TEXT("Ring"), Root);
    TitleText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Title"));
    TitleText->SetupAttachment(Root);
    SubtitleText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Subtitle"));
    SubtitleText->SetupAttachment(Root);
    for (UTextRenderComponent* Text : {TitleText.Get(), SubtitleText.Get()})
    {
        Text->SetHorizontalAlignment(EHTA_Center);
        Text->SetVerticalAlignment(EVRTA_TextCenter);
        Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Text->SetCastShadow(false);
        // Faces the player standing at the centre.
        Text->SetRelativeRotation(FRotator(0, 180, 0));
    }
    TitleText->SetRelativeLocation(FVector(230, 0, 178));
    TitleText->SetWorldSize(17.0f);
    SubtitleText->SetRelativeLocation(FVector(230, 0, 156));
    SubtitleText->SetWorldSize(6.5f);
    // Sky: 15 m radius around the head height of a standing player.
    Sky->SetRelativeLocation(FVector(0, 0, 150));
    Sky->SetRelativeScale3D(FVector(3000.0f / GratiaShapeCm));
    // Floor halo the player stands on: a disc 2.4 m across, 1 cm thick.
    Floor->SetRelativeLocation(FVector(0, 0, 0.5f));
    Floor->SetRelativeScale3D(FVector(240.0f / GratiaShapeCm, 240.0f / GratiaShapeCm, 0.01f));
    SetActorHiddenInGame(true);
}

void AGratiaLoadingSpace::Configure(UStaticMesh* Sphere, UStaticMesh* Cylinder, UMaterialInterface* SkyMaterial, UMaterialInterface* GlowMaterial)
{
    Sky->SetStaticMesh(Sphere);
    Floor->SetStaticMesh(Cylinder);
    Motes->SetStaticMesh(Sphere);
    Ring->SetStaticMesh(Sphere);
    if (SkyMaterial) { SkyInstance = UMaterialInstanceDynamic::Create(SkyMaterial, this); Sky->SetMaterial(0, SkyInstance); }
    if (GlowMaterial)
    {
        MoteInstance = UMaterialInstanceDynamic::Create(GlowMaterial, this);
        RingInstance = UMaterialInstanceDynamic::Create(GlowMaterial, this);
        FloorInstance = UMaterialInstanceDynamic::Create(GlowMaterial, this);
        Motes->SetMaterial(0, MoteInstance);
        Ring->SetMaterial(0, RingInstance);
        Floor->SetMaterial(0, FloorInstance);
    }
    // Motes: a halo of beads on the floor, then motes drifting up in a shell 2.5..9 m around.
    MoteData.Reset();
    Motes->ClearInstances();
    FRandomStream Random(466);
    for (int32 I = 0; I < GratiaHaloBeads + GratiaFloatingMotes; ++I)
    {
        FMote& Mote = MoteData.AddDefaulted_GetRef();
        Mote.Phase = Random.FRandRange(0.0f, 2.0f * PI);
        Mote.Speed = Random.FRandRange(0.3f, 1.0f);
        if (I < GratiaHaloBeads)
        {
            const float Angle = 2.0f * PI * I / GratiaHaloBeads;
            Mote.Base = FVector(FMath::Cos(Angle) * 150.0f, FMath::Sin(Angle) * 150.0f, 1.0f);
            Mote.Size = 2.4f;
        }
        else
        {
            const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
            const float Distance = Random.FRandRange(250.0f, 900.0f);
            Mote.Base = FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, Random.FRandRange(-50.0f, 450.0f));
            Mote.Size = Random.FRandRange(1.5f, 4.5f);
        }
        Motes->AddInstance(FTransform(FQuat::Identity, Mote.Base, FVector(Mote.Size / GratiaShapeCm)));
    }
    Ring->ClearInstances();
    for (int32 I = 0; I < GratiaRingBeads; ++I) Ring->AddInstance(FTransform::Identity);
}

void AGratiaLoadingSpace::ShowAt(const FVector& Location, float Yaw, const FText& Title, const FText& Subtitle, const FLinearColor& InAccent)
{
    SetActorLocationAndRotation(Location, FRotator(0, Yaw, 0));
    Accent = InAccent;
    bLobby = Title.IsEmpty();
    TitleText->SetText(Title);
    SubtitleText->SetText(Subtitle);
    TitleText->SetVisibility(!bLobby);
    SubtitleText->SetVisibility(!bLobby);
    Ring->SetVisibility(!bLobby);
    Progress = 0.0f;
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
    TitleText->SetText(FText::FromString(TEXT("Scene unavailable")));
    SubtitleText->SetText(Message);
    TitleText->SetVisibility(!Message.IsEmpty());
    SubtitleText->SetVisibility(!Message.IsEmpty());
    Ring->SetVisibility(false);
}

void AGratiaLoadingSpace::ApplyColors()
{
    const FLinearColor Soft = FLinearColor::LerpUsingHSV(Accent, FLinearColor(0.35f, 0.55f, 1.0f), 0.35f);
    TitleText->SetTextRenderColor(FLinearColor(1.0f, 0.97f, 0.99f).ToFColor(true));
    SubtitleText->SetTextRenderColor(Soft.ToFColor(true));
    if (SkyInstance) SkyInstance->SetVectorParameterValue(TEXT("Accent"), Accent);
    if (MoteInstance) MoteInstance->SetVectorParameterValue(TEXT("Color"), Soft * 6.0f);
    if (RingInstance) RingInstance->SetVectorParameterValue(TEXT("Color"), Accent * 10.0f);
    if (FloorInstance) FloorInstance->SetVectorParameterValue(TEXT("Color"), Accent * 0.6f);
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
    if (SkyInstance)
    {
        SkyInstance->SetScalarParameterValue(TEXT("Energy"), Bass);
        SkyInstance->SetScalarParameterValue(TEXT("Pulse"), Pulse);
    }
    if (FloorInstance) FloorInstance->SetVectorParameterValue(TEXT("Color"), Accent * (0.45f + 1.6f * Pulse + 0.8f * Bass));

    TArray<FTransform> MoteTransforms;
    MoteTransforms.Reserve(MoteData.Num());
    for (int32 I = 0; I < MoteData.Num(); ++I)
    {
        const FMote& Mote = MoteData[I];
        FVector Position = Mote.Base;
        float Size = Mote.Size;
        if (I < GratiaHaloBeads)
        {
            // Halo beads swell with the bass in a wave running around the floor.
            Size *= 1.0f + 1.4f * Bass * (0.5f + 0.5f * FMath::Sin(I * 0.7f + Time * 3.0f)) + 0.8f * Pulse;
        }
        else
        {
            // Motes drift up (4 m loop) and sway; they flare on the beat.
            Position.X += FMath::Sin(Time * Mote.Speed + Mote.Phase) * 25.0f;
            Position.Y += FMath::Cos(Time * Mote.Speed * 0.8f + Mote.Phase) * 25.0f;
            Position.Z = FMath::Fmod(Mote.Base.Z + 50.0f + Time * Mote.Speed * 18.0f, 500.0f) - 50.0f;
            Size *= 0.7f + 0.6f * Bass + 0.9f * Pulse;
        }
        MoteTransforms.Emplace(FQuat::Identity, Position, FVector(Size / GratiaShapeCm));
    }
    Motes->BatchUpdateInstancesTransforms(0, MoteTransforms, false, true, true);

    if (!bLobby)
    {
        // Progress ring in front, under the title: lit beads fill, the ring spins while waiting.
        RingAngle += Delta * (0.6f + 2.4f * (1.0f - Progress));
        TArray<FTransform> RingTransforms;
        for (int32 I = 0; I < GratiaRingBeads; ++I)
        {
            const float Angle = RingAngle + 2.0f * PI * I / GratiaRingBeads;
            const bool bLit = I < FMath::RoundToInt(Progress * GratiaRingBeads);
            const float Size = (bLit ? 3.2f : 1.4f) * (1.0f + 0.5f * Pulse);
            RingTransforms.Emplace(FQuat::Identity, FVector(230, FMath::Cos(Angle) * 22.0f, 112.0f + FMath::Sin(Angle) * 22.0f), FVector(Size / GratiaShapeCm));
        }
        Ring->BatchUpdateInstancesTransforms(0, RingTransforms, false, true, true);
    }
}
