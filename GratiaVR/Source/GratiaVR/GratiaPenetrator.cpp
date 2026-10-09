#include "GratiaPenetrator.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
FGratiaShaftSize GratiaShaftSize(const TCHAR* Name, float Length, float Radius)
{
    FGratiaShaftSize Size;
    Size.Name = Name; Size.LengthCm = Length; Size.RadiusCm = Radius;
    return Size;
}

// Engine basic shapes: the cylinder is 100 cm tall along Z with radius 50, the sphere 100 across.
void GratiaPlaceCylinder(UStaticMeshComponent* Mesh, const FVector& A, const FVector& B, double Radius)
{
    const FVector Segment = B - A;
    const double Length = Segment.Size();
    Mesh->SetVisibility(Length > 0.01 && Radius > 0.01);
    if (Length <= 0.01) return;
    Mesh->SetWorldLocationAndRotation((A + B) * 0.5, FRotationMatrix::MakeFromZ(Segment / Length).ToQuat(), false, nullptr, ETeleportType::TeleportPhysics);
    Mesh->SetWorldScale3D(FVector(Radius / 50.0, Radius / 50.0, Length / 100.0));
}

void GratiaPlaceSphere(UStaticMeshComponent* Mesh, const FVector& Center, double Radius)
{
    Mesh->SetVisibility(Radius > 0.01);
    Mesh->SetWorldLocation(Center, false, nullptr, ETeleportType::TeleportPhysics);
    Mesh->SetWorldScale3D(FVector(Radius / 50.0));
}
}

AGratiaPenetrator::AGratiaPenetrator()
{
    PrimaryActorTick.bCanEverTick = false;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Base"));
    RootComponent = Root;
    Sizes = { GratiaShaftSize(TEXT("S"), 13.0f, 1.6f), GratiaShaftSize(TEXT("M"), 17.0f, 2.1f), GratiaShaftSize(TEXT("L"), 21.0f, 2.7f),
        GratiaShaftSize(TEXT("XL"), 26.0f, 3.4f), GratiaShaftSize(TEXT("XXL"), 32.0f, 4.3f),
        GratiaShaftSize(TEXT("3XL"), 38.0f, 5.2f), GratiaShaftSize(TEXT("4XL"), 45.0f, 6.2f) };
    // Constructor references keep the shapes in the cooked build.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    CylinderMesh = Cylinder.Object; SphereMesh = Sphere.Object; BasicMaterial = Material.Object;
}

void AGratiaPenetrator::BeginPlay()
{
    Super::BeginPlay();
    SetJoints({});
}

GratiaPenetration::FShaft AGratiaPenetrator::GetShaft() const
{
    GratiaPenetration::FShaft Shaft;
    const FGratiaShaftSize Size = Sizes.IsValidIndex(SizeIndex) ? Sizes[SizeIndex] : FGratiaShaftSize();
    Shaft.Length = FMath::Max(4.0f, Size.LengthCm);
    Shaft.Radius = FMath::Max(0.5f, Size.RadiusCm);
    Shaft.TipCm = FMath::Clamp(TipTaperCm, 0.2f, float(Shaft.Length) * 0.5f);
    Shaft.BaseScale = FMath::Clamp(BaseRadiusScale, 0.5f, 2.0f);
    Shaft.Joints = FMath::Clamp(JointCount, 3, 16);
    return Shaft;
}

void AGratiaPenetrator::SetShape(FName Label, const GratiaPenetration::FShaft& Shape)
{
    FGratiaShaftSize Size;
    Size.Name = Label;
    Size.LengthCm = float(Shape.Length);
    Size.RadiusCm = float(Shape.Radius);
    if (Sizes.Num() != 1) Sizes.SetNum(1);
    Sizes[0] = Size;
    SizeIndex = 0;
    TipTaperCm = float(Shape.TipCm);
    BaseRadiusScale = float(Shape.BaseScale);
    JointCount = Shape.Joints;
}

void AGratiaPenetrator::SetSize(int32 Index)
{
    if (Sizes.IsEmpty()) return;
    SizeIndex = FMath::Clamp(Index, 0, Sizes.Num() - 1);
    // The solver re-shapes an engaged shaft next frame; a free one is redrawn straight now.
    SetJoints({});
}

void AGratiaPenetrator::CycleSize()
{
    SetSize(Sizes.IsEmpty() ? 0 : (SizeIndex + 1) % Sizes.Num());
}

FString AGratiaPenetrator::GetSizeLabel() const
{
    if (!Sizes.IsValidIndex(SizeIndex)) return TEXT("—");
    const FGratiaShaftSize& Size = Sizes[SizeIndex];
    return FString::Printf(TEXT("%s  %.0f × %.1f см"), *Size.Name.ToString(), Size.LengthCm, Size.RadiusCm * 2.0f);
}

void AGratiaPenetrator::SetBase(const FTransform& Base)
{
    if (Base.ContainsNaN()) return;
    SetActorLocationAndRotation(Base.GetLocation(), Base.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
}

void AGratiaPenetrator::SetJoints(const TArray<FVector>& World)
{
    const GratiaPenetration::FShaft Shaft = GetShaft();
    const bool bValid = World.Num() == Shaft.Joints && !World.ContainsByPredicate([](const FVector& Point) { return Point.ContainsNaN(); });
    if (bValid) Joints = World;
    else GratiaPenetration::StraightJoints(Shaft, GetActorLocation(), GetActorForwardVector(), Joints);
    UpdateVisuals();
}

double AGratiaPenetrator::GrabGap(const FVector& Point) const
{
    const FVector Base = GetActorLocation(), Forward = GetActorForwardVector();
    double Gap = FMath::PointDistToSegment(Point, Base - Forward * HandleLengthCm, Base) - HandleRadiusCm;
    const GratiaPenetration::FShaft Shaft = GetShaft();
    for (int32 Joint = 0; Joint + 1 < Joints.Num() && Joint < Joints.Num() / 2; ++Joint)
        Gap = FMath::Min(Gap, FMath::PointDistToSegment(Point, Joints[Joint], Joints[Joint + 1]) - Shaft.Radius);
    return Gap;
}

void AGratiaPenetrator::BuildVisuals()
{
    const int32 Count = GetShaft().Joints;
    const bool bMesh = ChainMesh && ChainBones.Num() >= 2;
    auto Make = [this](const TCHAR* Prefix, int32 Index, UStaticMesh* Shape, UMaterialInstanceDynamic* Material)
    {
        UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("%s%d"), Prefix, Index));
        Mesh->SetupAttachment(Root);
        Mesh->SetUsingAbsoluteLocation(true); Mesh->SetUsingAbsoluteRotation(true); Mesh->SetUsingAbsoluteScale(true);
        Mesh->SetStaticMesh(Shape);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCastShadow(false);
        if (Material) Mesh->SetMaterial(0, Material);
        Mesh->RegisterComponent();
        return Mesh;
    };
    if (!ShaftMaterial && BasicMaterial)
    {
        ShaftMaterial = UMaterialInstanceDynamic::Create(BasicMaterial, this);
        HandleMaterial = UMaterialInstanceDynamic::Create(BasicMaterial, this);
    }
    if (ShaftMaterial) ShaftMaterial->SetVectorParameterValue(TEXT("Color"), ShaftColor);
    if (HandleMaterial) HandleMaterial->SetVectorParameterValue(TEXT("Color"), HandleColor);
    if (!Handle && CylinderMesh) Handle = Make(TEXT("Handle"), 0, CylinderMesh, HandleMaterial);
    if (bMesh)
    {
        for (UStaticMeshComponent* Mesh : Segments) if (Mesh) Mesh->DestroyComponent();
        for (UStaticMeshComponent* Mesh : Knuckles) if (Mesh) Mesh->DestroyComponent();
        Segments.Reset(); Knuckles.Reset();
        if (!Poseable)
        {
            Poseable = NewObject<UPoseableMeshComponent>(this, TEXT("ChainMesh"));
            Poseable->SetupAttachment(Root);
            Poseable->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Poseable->SetCastShadow(false);
            Poseable->RegisterComponent();
        }
        if (Poseable->GetSkinnedAsset() != ChainMesh) Poseable->SetSkinnedAssetAndUpdate(ChainMesh);
        return;
    }
    if (Poseable) { Poseable->DestroyComponent(); Poseable = nullptr; }
    if (!CylinderMesh || !SphereMesh) return;
    while (Segments.Num() > Count - 1) { if (Segments.Last()) Segments.Last()->DestroyComponent(); Segments.Pop(); }
    while (Knuckles.Num() > Count) { if (Knuckles.Last()) Knuckles.Last()->DestroyComponent(); Knuckles.Pop(); }
    while (Segments.Num() < Count - 1) Segments.Add(Make(TEXT("Segment"), Segments.Num(), CylinderMesh, ShaftMaterial));
    while (Knuckles.Num() < Count) Knuckles.Add(Make(TEXT("Joint"), Knuckles.Num(), SphereMesh, ShaftMaterial));
}

void AGratiaPenetrator::UpdateVisuals()
{
    BuildVisuals();
    const GratiaPenetration::FShaft Shaft = GetShaft();
    if (Joints.Num() != Shaft.Joints) return;
    const FVector Base = GetActorLocation(), Forward = GetActorForwardVector();
    if (Handle) GratiaPlaceCylinder(Handle, Base - Forward * HandleLengthCm, Base, HandleRadiusCm);
    // Distance from the tip of each joint along the shaft (spacing may compress at the channel end).
    TArray<double> FromTip;
    FromTip.SetNum(Joints.Num());
    FromTip.Last() = 0.0;
    for (int32 Joint = Joints.Num() - 2; Joint >= 0; --Joint) FromTip[Joint] = FromTip[Joint + 1] + FVector::Distance(Joints[Joint], Joints[Joint + 1]);
    if (Poseable && ChainMesh)
    {
        const FReferenceSkeleton& Ref = ChainMesh->GetRefSkeleton();
        const FTransform Component = Poseable->GetComponentTransform();
        for (int32 Bone = 0; Bone < ChainBones.Num(); ++Bone)
        {
            const int32 Index = Ref.FindBoneIndex(ChainBones[Bone]);
            if (Index == INDEX_NONE) continue;
            const int32 Joint = FMath::RoundToInt(float(Bone) * (Joints.Num() - 1) / (ChainBones.Num() - 1));
            const FVector Along = (Joints[FMath::Min(Joint + 1, Joints.Num() - 1)] - Joints[FMath::Max(Joint - 1, 0)]).GetSafeNormal();
            // Ref rotation of the bone in world space, turned so its direction toward the next chain bone follows the joints.
            FTransform RefCS = Ref.GetRefBonePose()[Index];
            for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent)) RefCS *= Ref.GetRefBonePose()[Parent];
            const int32 Next = Bone + 1 < ChainBones.Num() ? Ref.FindBoneIndex(ChainBones[Bone + 1]) : INDEX_NONE;
            FVector RefDirection = Component.GetRotation().RotateVector(RefCS.GetRotation().GetForwardVector());
            if (Next != INDEX_NONE)
            {
                FTransform NextCS = Ref.GetRefBonePose()[Next];
                for (int32 Parent = Ref.GetParentIndex(Next); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent)) NextCS *= Ref.GetRefBonePose()[Parent];
                RefDirection = Component.GetRotation().RotateVector(NextCS.GetLocation() - RefCS.GetLocation()).GetSafeNormal();
            }
            const FQuat Rotation = FQuat::FindBetweenNormals(RefDirection, Along) * Component.GetRotation() * RefCS.GetRotation();
            Poseable->SetBoneTransformByName(ChainBones[Bone], FTransform(Rotation, Joints[Joint]), EBoneSpaces::WorldSpace);
        }
        return;
    }
    for (int32 Segment = 0; Segment < Segments.Num(); ++Segment)
        if (Segments[Segment])
            GratiaPlaceCylinder(Segments[Segment], Joints[Segment], Joints[Segment + 1],
                Shaft.RadiusAt(0.5 * (FromTip[Segment] + FromTip[Segment + 1])));
    for (int32 Joint = 0; Joint < Knuckles.Num(); ++Joint)
    {
        if (!Knuckles[Joint]) continue;
        if (Joint + 1 < Knuckles.Num()) { GratiaPlaceSphere(Knuckles[Joint], Joints[Joint], Shaft.RadiusAt(FromTip[Joint])); continue; }
        // Rounded tip: a sphere that ends at the tip joint.
        const FVector Back = (Joints[Joint - 1] - Joints[Joint]).GetSafeNormal();
        const double Radius = 0.9 * Shaft.RadiusAt(FMath::Min(Shaft.TipCm, FromTip[Joint - 1]));
        GratiaPlaceSphere(Knuckles[Joint], Joints[Joint] + Back * Radius, Radius);
    }
}
