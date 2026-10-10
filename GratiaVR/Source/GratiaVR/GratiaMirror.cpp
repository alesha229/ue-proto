#include "GratiaMirror.h"
#include "GratiaCharacterProfile.h"
#include "GratiaPreviewCharacter.h"

#include "Components/PlanarReflectionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogGratiaMirror, Log, All);

namespace
{
    const TCHAR* GratiaMirrorMaterialPath = TEXT("/Game/Gratia/Environment/M_GratiaMirror.M_GratiaMirror");
    // The plane's normal is +Z; pitched -90 it faces the actor's +X and its X runs up.
    const FRotator GlassRotation(-90.0f, 0.0f, 0.0f);
}

AGratiaMirror::AGratiaMirror()
{
    PrimaryActorTick.bCanEverTick = false;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Movable);
    RootComponent = Root;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Glass = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Glass"));
    Glass->SetupAttachment(Root);
    Glass->SetStaticMesh(PlaneMesh.Object);
    Glass->SetRelativeRotation(GlassRotation);
    Glass->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Glass->SetCastShadow(false);
    Reflection = CreateDefaultSubobject<UPlanarReflectionComponent>(TEXT("Reflection"));
    Reflection->SetupAttachment(Root);
    Reflection->SetRelativeRotation(GlassRotation);
    for (int32 I = 0; I < 4; ++I)
    {
        UStaticMeshComponent* Bar = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Frame%d"), I));
        Bar->SetupAttachment(Root);
        Bar->SetStaticMesh(CubeMesh.Object);
        Bar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Bar->SetCastShadow(false);
        Frame.Add(Bar);
    }
}

void AGratiaMirror::BeginPlay()
{
    Super::BeginPlay();
    // Only pixels on the glass (within a few cm of the plane, facing like it) take the reflection.
    Reflection->ScreenPercentage = FMath::RoundToInt(ScreenPercentage);
    Reflection->PrefilterRoughness = 0.0f;
    Reflection->NormalDistortionStrength = 0.0f;
    Reflection->DistanceFromPlaneFadeoutStart = 2.0f;
    Reflection->DistanceFromPlaneFadeoutEnd = 6.0f;
    Reflection->AngleFromPlaneFadeStart = 15.0f;
    Reflection->AngleFromPlaneFadeEnd = 25.0f;
    if (UMaterialInterface* Material = ResolveMaterial()) Glass->SetMaterial(0, Material);
    if (UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
    {
        UMaterialInstanceDynamic* Dark = UMaterialInstanceDynamic::Create(Basic, this);
        Dark->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.035f, 0.03f, 0.03f));
        for (UStaticMeshComponent* Bar : Frame) Bar->SetMaterial(0, Dark);
    }
    SetSize(SizeCm);
}

UMaterialInterface* AGratiaMirror::ResolveMaterial()
{
    if (MirrorMaterial) return MirrorMaterial;
    MirrorMaterial = LoadObject<UMaterialInterface>(nullptr, GratiaMirrorMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    MaterialSource = MirrorMaterial ? TEXT("asset") : TEXT("missing");
#if WITH_EDITOR
    if (!MirrorMaterial)
    {
        // Editor and -game from the editor: a transient perfect mirror (white metal, zero roughness).
        UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_GratiaMirror_Transient"));
        UMaterialExpressionConstant3Vector* Color = NewObject<UMaterialExpressionConstant3Vector>(Material);
        Color->Constant = FLinearColor(0.93f, 0.93f, 0.95f);
        UMaterialExpressionConstant* Metallic = NewObject<UMaterialExpressionConstant>(Material);
        Metallic->R = 1.0f;
        UMaterialExpressionConstant* Roughness = NewObject<UMaterialExpressionConstant>(Material);
        Roughness->R = 0.02f;
        Material->GetExpressionCollection().AddExpression(Color);
        Material->GetExpressionCollection().AddExpression(Metallic);
        Material->GetExpressionCollection().AddExpression(Roughness);
        Material->GetEditorOnlyData()->BaseColor.Expression = Color;
        Material->GetEditorOnlyData()->Metallic.Expression = Metallic;
        Material->GetEditorOnlyData()->Roughness.Expression = Roughness;
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        MirrorMaterial = Material;
        MaterialSource = TEXT("transient (run Scripts/setup_scene_extras.py for packaged builds)");
    }
#endif
    if (!MirrorMaterial)
        UE_LOG(LogGratiaMirror, Error, TEXT("Mirror material %s is missing: the glass shows the default material. Run GratiaVR/Scripts/setup_scene_extras.py."), GratiaMirrorMaterialPath);
    return MirrorMaterial;
}

void AGratiaMirror::SetSize(const FVector2D& Size)
{
    SizeCm = Size;
    const float Width = Size.X, Height = Size.Y, Bar = 4.0f, Depth = 3.0f;
    Glass->SetRelativeScale3D(FVector(Height / 100.0f, Width / 100.0f, 1.0f));
    // Frame bars slightly behind the glass: left, right, top, bottom.
    const FVector Offsets[4] = {FVector(-Depth * 0.5f, -(Width + Bar) * 0.5f, 0.0f), FVector(-Depth * 0.5f, (Width + Bar) * 0.5f, 0.0f),
        FVector(-Depth * 0.5f, 0.0f, (Height + Bar) * 0.5f), FVector(-Depth * 0.5f, 0.0f, -(Height + Bar) * 0.5f)};
    const FVector Scales[4] = {FVector(Depth, Bar, Height + 2.0f * Bar), FVector(Depth, Bar, Height + 2.0f * Bar),
        FVector(Depth, Width, Bar), FVector(Depth, Width, Bar)};
    for (int32 I = 0; I < Frame.Num(); ++I)
    {
        Frame[I]->SetRelativeLocation(Offsets[I]);
        Frame[I]->SetRelativeScale3D(Scales[I] / 100.0f);
    }
}

void AGratiaMirror::SetReflectionEnabled(bool bEnabled)
{
    // An unregistered reflection costs nothing; a registered one renders the scene again every frame.
    if (bEnabled && !Reflection->IsRegistered()) Reflection->RegisterComponent();
    else if (!bEnabled && Reflection->IsRegistered()) Reflection->UnregisterComponent();
}

void AGratiaMirror::PlaceFor(const AGratiaPreviewCharacter* Character, const FVector& Viewer, bool bOnWall)
{
    if (!Character) return;
    auto Flat = [](const FVector& V) { return FVector(V.X, V.Y, 0.0).GetSafeNormal(); };
    const FVector Feet = Character->GetActorLocation();
    FVector Forward = Character->GetActorRightVector();
    if (const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get())
        Forward = Character->GetActorTransform().TransformVectorNoScale(Profile->ForwardAxis.GetSafeNormal());
    FVector ToViewer = Flat(Viewer - Feet);
    if (ToViewer.IsNearlyZero()) ToViewer = Flat(Forward);
    if (ToViewer.IsNearlyZero()) ToViewer = FVector::ForwardVector;
    const FVector Side = FVector::CrossProduct(FVector::UpVector, ToViewer);
    FVector Location, Normal;
    bool bPlaced = false;
    if (bOnWall)
    {
        SetSize(FVector2D(70.0f, 60.0f));
        // Nearest wall behind or beside her (as seen from the player).
        FCollisionQueryParams Params(TEXT("GratiaMirrorWall"), false, Character);
        Params.AddIgnoredActor(this);
        const FVector Start = Feet + FVector(0, 0, 140.0);
        float Best = FLT_MAX;
        for (const FVector& Direction : {-ToViewer, Side, -Side, (-ToViewer + Side).GetSafeNormal(), (-ToViewer - Side).GetSafeNormal()})
        {
            FHitResult Hit;
            if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Direction * 400.0, ECC_Visibility, Params)) continue;
            if (FMath::Abs(Hit.ImpactNormal.Z) > 0.3 || Hit.Distance >= Best) continue;
            Best = Hit.Distance;
            Location = Hit.ImpactPoint + Hit.ImpactNormal * 2.0;
            Normal = Flat(Hit.ImpactNormal);
            bPlaced = !Normal.IsNearlyZero();
        }
    }
    else SetSize(FVector2D(90.0f, 180.0f));
    if (!bPlaced)
    {
        // Free-standing beside her; on a wall request without a wall it hangs at head height.
        Location = Feet + Side * 110.0 + ToViewer * 10.0 + FVector(0, 0, bOnWall ? 140.0 : SizeCm.Y * 0.5 + 2.0);
        // Bisector of the directions to the viewer and to her: the player sees her reflected.
        Normal = (Flat(Viewer - Location) + Flat(Feet - Location)).GetSafeNormal();
        if (Normal.IsNearlyZero()) Normal = ToViewer;
    }
    SetActorLocationAndRotation(Location, Normal.Rotation());
}

FString AGratiaMirror::GetDiagnostics() const
{
    return FString::Printf(TEXT("mirror %.0fx%.0f cm, reflection %s at %.0f%%, material %s"), SizeCm.X, SizeCm.Y,
        Reflection && Reflection->IsRegistered() ? TEXT("on") : TEXT("off"), ScreenPercentage, MaterialSource.IsEmpty() ? TEXT("-") : *MaterialSource);
}
