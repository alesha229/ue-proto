#include "GratiaHandAnimInstance.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/MirrorDataTable.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
const TCHAR* GratiaHandFingerNames[UGratiaHandAnimInstance::NumFingers] = {TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")};

struct FGratiaHandProxy : public FAnimInstanceProxy
{
    FGratiaHandProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

    UGratiaHandAnimInstance* Owner = nullptr;
    UAnimSequence* Open = nullptr;
    UAnimSequence* Closed = nullptr;
    UMirrorDataTable* Mirror = nullptr;
    bool bLeft = false;
    float Alpha[UGratiaHandAnimInstance::NumFingers] = {};
    TArray<int32> BoneFinger;
    // Finger chain joints (compact indices) for the active hand side.
    int32 Joints[UGratiaHandAnimInstance::NumFingers][3];
    uint16 Serial = 0;

    virtual void PreUpdate(UAnimInstance* InInstance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(InInstance, DeltaSeconds);
        Owner = CastChecked<UGratiaHandAnimInstance>(InInstance);
        Open = Owner->OpenPose; Closed = Owner->ClosedPose; Mirror = Owner->MirrorTable; bLeft = Owner->bLeftHand;
        for (int32 F = 0; F < UGratiaHandAnimInstance::NumFingers; ++F) Alpha[F] = Owner->FingerAlpha[F];
    }

    void CacheBones(const FBoneContainer& Bones)
    {
        if (Serial == Bones.GetSerialNumber() && !BoneFinger.IsEmpty()) return;
        Serial = Bones.GetSerialNumber();
        const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
        BoneFinger.Init(INDEX_NONE, Bones.GetCompactPoseNumBones());
        const TCHAR* Side = bLeft ? TEXT("_l") : TEXT("_r");
        for (int32 F = 0; F < UGratiaHandAnimInstance::NumFingers; ++F) for (int32 J = 0; J < 3; ++J) Joints[F][J] = INDEX_NONE;
        for (FCompactPoseBoneIndex Index(0); Index < Bones.GetCompactPoseNumBones(); ++Index)
        {
            const FString Name = Ref.GetBoneName(Bones.MakeMeshPoseIndex(Index).GetInt()).ToString().ToLower();
            for (int32 F = 0; F < UGratiaHandAnimInstance::NumFingers; ++F)
            {
                if (!Name.StartsWith(GratiaHandFingerNames[F])) continue;
                BoneFinger[Index.GetInt()] = F;
                if (!Name.EndsWith(Side)) break;
                for (int32 J = 0; J < 3; ++J)
                    if (Name == FString::Printf(TEXT("%s_0%d%s"), GratiaHandFingerNames[F], J + 1, Side)) Joints[F][J] = Index.GetInt();
            }
        }
    }

    bool SamplePose(UAnimSequence* Sequence, double Time, FCompactPose& OutPose, FBlendedCurve& Curve, UE::Anim::FStackAttributeContainer& Attributes)
    {
        if (!Sequence) return false;
        FAnimationPoseData Data(OutPose, Curve, Attributes);
        Sequence->GetAnimationPose(Data, FAnimExtractContext(Time, false));
        if (bLeft && Mirror) FAnimationRuntime::MirrorPose(OutPose, *Mirror);
        return true;
    }

    void BlendInto(FCompactPose& Out, const FCompactPose& A, const FCompactPose& B, const float* FingerAlpha) const
    {
        for (FCompactPoseBoneIndex Index : Out.ForEachBoneIndex())
        {
            const int32 F = BoneFinger.IsValidIndex(Index.GetInt()) ? BoneFinger[Index.GetInt()] : INDEX_NONE;
            if (F == INDEX_NONE) { Out[Index] = A[Index]; continue; }
            Out[Index].Blend(A[Index], B[Index], FMath::Clamp(FingerAlpha[F], 0.0f, 1.0f));
        }
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        if (!Open || !Closed) { Output.ResetToRefPose(); return true; }
        const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
        CacheBones(Bones);
        FCompactPose OpenPose, ClosedPose;
        OpenPose.SetBoneContainer(&Bones); ClosedPose.SetBoneContainer(&Bones);
        FBlendedCurve CurveA, CurveB;
        UE::Anim::FStackAttributeContainer AttrA, AttrB;
        SamplePose(Open, 0.0, OpenPose, CurveA, AttrA);
        SamplePose(Closed, Closed->GetPlayLength(), ClosedPose, CurveB, AttrB);
        BlendInto(Output.Pose, OpenPose, ClosedPose, Alpha);
        if (Owner && !Owner->bSamplesReady.load())
        {
            // Component-space finger joints for every uniform curl step; the game thread
            // tests these against contact spheres to find where each finger touches.
            FCompactPose Step; Step.SetBoneContainer(&Bones);
            for (int32 K = 0; K <= UGratiaHandAnimInstance::NumSamples; ++K)
            {
                float Uniform[UGratiaHandAnimInstance::NumFingers];
                for (float& Value : Uniform) Value = float(K) / UGratiaHandAnimInstance::NumSamples;
                BlendInto(Step, OpenPose, ClosedPose, Uniform);
                FCSPose<FCompactPose> Component; Component.InitPose(Step);
                for (int32 F = 0; F < UGratiaHandAnimInstance::NumFingers; ++F)
                {
                    FVector Points[3];
                    bool bValid = true;
                    for (int32 J = 0; J < 3; ++J)
                    {
                        bValid &= Joints[F][J] != INDEX_NONE;
                        Points[J] = bValid ? Component.GetComponentSpaceTransform(FCompactPoseBoneIndex(Joints[F][J])).GetLocation() : FVector::ZeroVector;
                    }
                    for (int32 J = 0; J < 3; ++J) Owner->Samples[F][K][J] = Points[J];
                    Owner->Samples[F][K][3] = bValid ? Points[2] + (Points[2] - Points[1]) * 0.8 : FVector::ZeroVector;
                }
            }
            bool bAll = true;
            for (int32 F = 0; F < UGratiaHandAnimInstance::NumFingers; ++F) bAll &= Joints[F][2] != INDEX_NONE;
            if (bAll) Owner->bSamplesReady.store(true);
        }
        return true;
    }
};
}

FAnimInstanceProxy* UGratiaHandAnimInstance::CreateAnimInstanceProxy() { return new FGratiaHandProxy(this); }

bool UGratiaHandAnimInstance::LoadDefaultPoses()
{
    if (!OpenPose) OpenPose = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/XRMannequins/Animations/A_MannequinsXR_Idle_Right.A_MannequinsXR_Idle_Right"));
    if (!ClosedPose) ClosedPose = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/XRMannequins/Animations/A_MannequinsXR_Grasp_Right.A_MannequinsXR_Grasp_Right"));
    if (!MirrorTable) MirrorTable = LoadObject<UMirrorDataTable>(nullptr, TEXT("/Game/XRMannequins/Animations/MDT_MannequinsXR.MDT_MannequinsXR"));
    return OpenPose && ClosedPose && (!bLeftHand || MirrorTable);
}

float UGratiaHandAnimInstance::CapFinger(int32 Finger, const FTransform& Component) const
{
    if (!bConform || ConformSpheres.IsEmpty() || !bSamplesReady.load()) return 1.0f;
    auto Touches = [&](int32 K)
    {
        for (int32 P = 0; P < PointsPerFinger; ++P)
        {
            const FVector Point = Component.TransformPosition(Samples[Finger][K][P]);
            for (const FVector4& Sphere : ConformSpheres)
                if (FVector::Distance(Point, FVector(Sphere.X, Sphere.Y, Sphere.Z)) < Sphere.W + FingerRadiusCm + ConformMarginCm) return true;
        }
        return false;
    };
    if (Touches(0)) return 0.0f;
    for (int32 K = 1; K <= NumSamples; ++K)
        if (Touches(K)) return float(K - 1) / NumSamples;
    return 1.0f;
}

void UGratiaHandAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    const float Dt = FMath::IsFinite(DeltaSeconds) ? FMath::Clamp(DeltaSeconds, 0.0f, 0.1f) : 0.0f;
    for (int32 F = 0; F < NumFingers; ++F)
    {
        FingerCap[F] = Mesh ? CapFinger(F, Mesh->GetComponentTransform()) : 1.0f;
        const float Input = FMath::IsFinite(FingerInput[F]) ? FMath::Clamp(FingerInput[F], 0.0f, 1.0f) : 0.0f;
        const float Target = FMath::Min(Input, FingerCap[F]);
        // Close quickly but never through a surface; open a little slower for a soft release.
        const float Speed = Target > FingerAlpha[F] ? 14.0f : 9.0f;
        FingerAlpha[F] = FMath::FInterpTo(FingerAlpha[F], Target, Dt, Speed);
        if (FingerAlpha[F] > FingerCap[F]) FingerAlpha[F] = FingerCap[F];
    }
}

void UGratiaHandAnimInstance::GetFingerPoints(TArray<FVector>& OutWorld) const
{
    OutWorld.Reset();
    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    if (!Mesh || !bSamplesReady.load()) return;
    const FTransform Component = Mesh->GetComponentTransform();
    for (int32 F = 0; F < NumFingers; ++F)
    {
        const float Scaled = FMath::Clamp(FingerAlpha[F], 0.0f, 1.0f) * NumSamples;
        const int32 K = FMath::Min(FMath::FloorToInt(Scaled), NumSamples - 1);
        const float T = Scaled - K;
        for (int32 P = 2; P < PointsPerFinger; ++P) // distal joint and tip: 10 spheres per hand
            OutWorld.Add(Component.TransformPosition(FMath::Lerp(Samples[F][K][P], Samples[F][K + 1][P], T)));
    }
}

bool UGratiaHandAnimInstance::GetPalmPoint(FVector& OutWorld) const
{
    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    if (!Mesh || !bSamplesReady.load()) return false;
    // Knuckles of index..pinky; the hand root is the wrist.
    FVector Knuckles = FVector::ZeroVector;
    for (int32 F = 1; F < NumFingers; ++F) Knuckles += Samples[F][0][0];
    Knuckles /= double(NumFingers - 1);
    OutWorld = Mesh->GetComponentTransform().TransformPosition(Knuckles * 0.6);
    return !OutWorld.ContainsNaN();
}

FString UGratiaHandAnimInstance::GetDiagnostics() const
{
    return FString::Printf(TEXT("curl T%.2f I%.2f M%.2f R%.2f P%.2f cap T%.2f I%.2f M%.2f R%.2f P%.2f%s"),
        FingerAlpha[0], FingerAlpha[1], FingerAlpha[2], FingerAlpha[3], FingerAlpha[4],
        FingerCap[0], FingerCap[1], FingerCap[2], FingerCap[3], FingerCap[4], bSamplesReady.load() ? TEXT("") : TEXT(" (sampling)"));
}
