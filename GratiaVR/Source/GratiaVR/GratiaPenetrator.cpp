#include "GratiaPenetrator.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
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

constexpr int32 GratiaTubeRings = 80;
constexpr int32 GratiaTubeSides = 24;

/** Catmull-Rom point (and tangent) between Points[Segment] and Points[Segment + 1] at T in 0..1; the ends extend. */
FVector GratiaCurve(const TArray<FVector>& Points, int32 Segment, double T, FVector& Tangent)
{
    const int32 Last = Points.Num() - 1;
    const FVector P1 = Points[Segment], P2 = Points[FMath::Min(Segment + 1, Last)];
    const FVector P0 = Segment > 0 ? Points[Segment - 1] : P1 * 2.0 - P2;
    const FVector P3 = Segment + 2 <= Last ? Points[Segment + 2] : P2 * 2.0 - P1;
    const double T2 = T * T, T3 = T2 * T;
    Tangent = (0.5 * ((P2 - P0) + 2.0 * T * (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) + 3.0 * T2 * (3.0 * P1 - P0 - 3.0 * P2 + P3))).GetSafeNormal();
    if (Tangent.IsNearlyZero()) Tangent = (P2 - P1).GetSafeNormal();
    return 0.5 * ((2.0 * P1) + (P2 - P0) * T + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * T2 + (3.0 * P1 - P0 - 3.0 * P2 + P3) * T3);
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
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    CylinderMesh = Cylinder.Object; BasicMaterial = Material.Object;
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
    Shaft.Form = static_cast<GratiaPenetration::EShaftForm>(FMath::Clamp(int32(Form), 0, int32(GratiaPenetration::EShaftForm::Count) - 1));
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
    Form = static_cast<EGratiaShaftForm>(FMath::Min(int32(Shape.Form), int32(EGratiaShaftForm::Tentacle)));
}

void AGratiaPenetrator::SetForm(EGratiaShaftForm NewForm)
{
    Form = NewForm;
    // The solver re-shapes an engaged shaft next frame; a free one is redrawn straight now.
    SetJoints({});
}

void AGratiaPenetrator::CycleForm()
{
    SetForm(static_cast<EGratiaShaftForm>((int32(Form) + 1) % (int32(EGratiaShaftForm::Tentacle) + 1)));
}

FString AGratiaPenetrator::GetFormLabel() const
{
    return GratiaPenetration::ShaftFormName(GetShaft().Form);
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
    const bool bMesh = ChainMesh && ChainBones.Num() >= 2;
    if (!ShaftMaterial && BasicMaterial)
    {
        ShaftMaterial = UMaterialInstanceDynamic::Create(BasicMaterial, this);
        HandleMaterial = UMaterialInstanceDynamic::Create(BasicMaterial, this);
    }
    // Each form has its own colour; the smooth one keeps ShaftColor.
    static const FLinearColor FormColors[] = {FLinearColor(0.78f, 0.42f, 0.55f), FLinearColor(0.86f, 0.56f, 0.5f), FLinearColor(0.62f, 0.16f, 0.22f),
        FLinearColor(0.26f, 0.12f, 0.38f), FLinearColor(0.16f, 0.52f, 0.58f), FLinearColor(0.24f, 0.34f, 0.8f), FLinearColor(0.36f, 0.23f, 0.21f),
        FLinearColor(0.46f, 0.22f, 0.6f)};
    const int32 FormIndex = int32(Form);
    if (ShaftMaterial) ShaftMaterial->SetVectorParameterValue(TEXT("Color"), Form == EGratiaShaftForm::Smooth || FormIndex >= UE_ARRAY_COUNT(FormColors)
        ? ShaftColor : FormColors[FormIndex]);
    if (HandleMaterial) HandleMaterial->SetVectorParameterValue(TEXT("Color"), HandleColor);
    if (!Handle && CylinderMesh)
    {
        Handle = NewObject<UStaticMeshComponent>(this, TEXT("Handle0"));
        Handle->SetupAttachment(Root);
        Handle->SetUsingAbsoluteLocation(true); Handle->SetUsingAbsoluteRotation(true); Handle->SetUsingAbsoluteScale(true);
        Handle->SetStaticMesh(CylinderMesh);
        Handle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Handle->SetGenerateOverlapEvents(false);
        Handle->SetCastShadow(false);
        if (HandleMaterial) Handle->SetMaterial(0, HandleMaterial);
        Handle->RegisterComponent();
    }
    if (bMesh)
    {
        if (Tube) { Tube->DestroyComponent(); Tube = nullptr; bTubeBuilt = false; }
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
    if (!Tube)
    {
        Tube = NewObject<UProceduralMeshComponent>(this, TEXT("Tube"));
        Tube->SetupAttachment(Root);
        Tube->SetUsingAbsoluteLocation(true); Tube->SetUsingAbsoluteRotation(true); Tube->SetUsingAbsoluteScale(true);
        Tube->SetWorldTransform(FTransform::Identity);
        Tube->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Tube->bUseComplexAsSimpleCollision = false;
        Tube->SetGenerateOverlapEvents(false);
        Tube->SetCastShadow(false);
        Tube->RegisterComponent();
        bTubeBuilt = false;
    }
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
    // An invisible shaft (a hand) draws no tube.
    if (!Tube || IsHidden()) return;
    // Rings from the base to the tip along a smooth curve through the joints, each with the form's radius at its
    // distance from the tip; the ring frame is carried along the curve so the tube does not twist.
    const double Total = FromTip[0];
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs;
    const int32 Stride = GratiaTubeSides + 1;
    Vertices.Reserve(GratiaTubeRings * Stride + Stride + 1);
    Normals.Reserve(Vertices.Max());
    UVs.Reserve(Vertices.Max());
    FVector Side = FVector::CrossProduct(Forward, FVector::UpVector).GetSafeNormal();
    if (Side.IsNearlyZero()) Side = FVector::CrossProduct(Forward, FVector::RightVector).GetSafeNormal();
    FVector BaseTangent = Forward;
    int32 Segment = 0;
    for (int32 Ring = 0; Ring < GratiaTubeRings; ++Ring)
    {
        // Denser rings toward the tip, where the forms change fastest.
        const double F = double(Ring) / (GratiaTubeRings - 1);
        const double U = FMath::Max(0.0, Total * FMath::Square(1.0 - F));
        while (Segment < Joints.Num() - 2 && FromTip[Segment + 1] > U) ++Segment;
        const double Span = FMath::Max(1.0e-4, FromTip[Segment] - FromTip[Segment + 1]);
        FVector Tangent;
        const FVector Centre = GratiaCurve(Joints, Segment, FMath::Clamp((FromTip[Segment] - U) / Span, 0.0, 1.0), Tangent);
        if (Ring == 0) BaseTangent = Tangent;
        Side = FVector::VectorPlaneProject(Side, Tangent).GetSafeNormal();
        if (Side.IsNearlyZero()) Side = FVector::CrossProduct(Tangent, FVector::UpVector).GetSafeNormal();
        const FVector Up = FVector::CrossProduct(Tangent, Side);
        // The profile follows the drawn length (a shaft compressed at the channel end keeps its shape).
        const double ProfileU = Total > 1.0e-3 ? U * Shaft.Length / Total : U;
        const double Radius = Shaft.RadiusAt(FMath::Max(ProfileU, 1.0e-3));
        const double Slope = Shaft.SlopeAt(FMath::Max(ProfileU, 0.06));
        for (int32 Step = 0; Step <= GratiaTubeSides; ++Step)
        {
            const double Angle = 2.0 * PI * Step / GratiaTubeSides;
            const FVector Radial = Side * FMath::Cos(Angle) + Up * FMath::Sin(Angle);
            Vertices.Add(Centre + Radial * Radius);
            Normals.Add(Ring == GratiaTubeRings - 1 ? Tangent : (Radial + Tangent * Slope).GetSafeNormal());
            UVs.Add(FVector2D(double(Step) / GratiaTubeSides, U / FMath::Max(Shaft.Length, 1.0)));
        }
    }
    // Base cap: a flat disc facing back.
    const int32 CapCentre = Vertices.Num();
    Vertices.Add(Joints[0]); Normals.Add(-BaseTangent); UVs.Add(FVector2D(0.5, 1.0));
    for (int32 Step = 0; Step <= GratiaTubeSides; ++Step)
    {
        const FVector Rim = Vertices[Step];
        Vertices.Add(Rim); Normals.Add(-BaseTangent); UVs.Add(FVector2D(double(Step) / GratiaTubeSides, 1.0));
    }
    if (!bTubeBuilt || Tube->GetNumSections() == 0)
    {
        TArray<int32> Triangles;
        for (int32 Ring = 0; Ring + 1 < GratiaTubeRings; ++Ring)
            for (int32 Step = 0; Step < GratiaTubeSides; ++Step)
            {
                const int32 A = Ring * Stride + Step, B = A + 1, C = A + Stride, D = C + 1;
                // Engine front faces wind clockwise seen from outside.
                Triangles.Append({A, C, B, B, C, D});
            }
        for (int32 Step = 0; Step < GratiaTubeSides; ++Step) Triangles.Append({CapCentre, CapCentre + 1 + Step, CapCentre + 2 + Step});
        Tube->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, {}, {}, false);
        if (ShaftMaterial) Tube->SetMaterial(0, ShaftMaterial);
        bTubeBuilt = true;
    }
    else Tube->UpdateMeshSection_LinearColor(0, Vertices, Normals, UVs, {}, {});
}
