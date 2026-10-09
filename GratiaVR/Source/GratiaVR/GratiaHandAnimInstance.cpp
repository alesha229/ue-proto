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
    UAnimSequence* IndexClosed = nullptr;
    UAnimSequence* ThumbOpen = nullptr;
    UMirrorDataTable* Mirror = nullptr;
    bool bLeft = false;
    float Alpha[UGratiaHandAnimInstance::NumFingers] = {};
    float Together = 0.0f;
    TArray<int32> BoneFinger;
    // Finger chain joints (compact indices) for the active hand side.
    int32 Joints[UGratiaHandAnimInstance::NumFingers][3];
    int32 WristJoint = INDEX_NONE, PalmJoint = INDEX_NONE;
    uint16 Serial = 0;

    virtual void PreUpdate(UAnimInstance* InInstance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(InInstance, DeltaSeconds);
        Owner = CastChecked<UGratiaHandAnimInstance>(InInstance);
        Open = Owner->OpenPose; Closed = Owner->ClosedPose; IndexClosed = Owner->IndexClosedPose; ThumbOpen = Owner->ThumbOpenPose; Mirror = Owner->MirrorTable; bLeft = Owner->bLeftHand;
        for (int32 F = 0; F < UGratiaHandAnimInstance::NumFingers; ++F) Alpha[F] = Owner->FingerAlpha[F];
        Together = FMath::Clamp(Owner->FingersTogether, 0.0f, 1.0f);
    }

    /** Fingers pressed together: index, ring and little finger turn at their base toward the middle finger. */
    void CloseSpread(FCompactPose& Pose) const
    {
        if (Together <= 0.0f || Joints[2][0] == INDEX_NONE || Joints[2][1] == INDEX_NONE) return;
        FCSPose<FCompactPose> Component;
        Component.InitPose(Pose);
        auto Point = [&Component](int32 Joint) { return Component.GetComponentSpaceTransform(FCompactPoseBoneIndex(Joint)).GetLocation(); };
        const FVector Middle = (Point(Joints[2][1]) - Point(Joints[2][0])).GetSafeNormal();
        if (Middle.IsNearlyZero()) return;
        TArray<FBoneTransform> Turned;
        for (const int32 F : {1, 3, 4})
        {
            if (Joints[F][0] == INDEX_NONE || Joints[F][1] == INDEX_NONE) continue;
            const FVector Direction = (Point(Joints[F][1]) - Point(Joints[F][0])).GetSafeNormal();
            if (Direction.IsNearlyZero()) continue;
            // Not quite parallel: the fingers touch side by side rather than overlap.
            const FQuat Turn = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Direction, Middle), 0.85f * Together);
            FTransform Base = Component.GetComponentSpaceTransform(FCompactPoseBoneIndex(Joints[F][0]));
            Base.SetRotation(Turn * Base.GetRotation());
            Turned.Emplace(FCompactPoseBoneIndex(Joints[F][0]), Base);
        }
        if (Turned.IsEmpty()) return;
        Turned.Sort(FCompareBoneTransformIndex());
        Component.SafeSetCSBoneTransforms(Turned);
        FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(Component), Pose);
    }

    void CacheHandBones(const FBoneContainer& Bones)
    {
        if (Serial == Bones.GetSerialNumber() && !BoneFinger.IsEmpty()) return;
        Serial = Bones.GetSerialNumber();
        const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
        BoneFinger.Init(INDEX_NONE, Bones.GetCompactPoseNumBones());
        const TCHAR* Side = bLeft ? TEXT("_l") : TEXT("_r");
        for (int32 F = 0; F < UGratiaHandAnimInstance::NumFingers; ++F) for (int32 J = 0; J < 3; ++J) Joints[F][J] = INDEX_NONE;
        WristJoint = PalmJoint = INDEX_NONE;
        for (FCompactPoseBoneIndex Index(0); Index < Bones.GetCompactPoseNumBones(); ++Index)
        {
            const FString Name = Ref.GetBoneName(Bones.MakeMeshPoseIndex(Index).GetInt()).ToString().ToLower();
            if (Name == FString(TEXT("hand")) + Side) WristJoint = Index.GetInt();
            if (Name == FString(TEXT("palm")) + Side) PalmJoint = Index.GetInt();
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

    void BlendInto(FCompactPose& Out, const FCompactPose& A, const FCompactPose& B, const FCompactPose* IndexB, const FCompactPose* ThumbA,
        const float* FingerAlpha) const
    {
        for (FCompactPoseBoneIndex Index : Out.ForEachBoneIndex())
        {
            const int32 F = BoneFinger.IsValidIndex(Index.GetInt()) ? BoneFinger[Index.GetInt()] : INDEX_NONE;
            if (F == INDEX_NONE) { Out[Index] = A[Index]; continue; }
            // Negative curl extrapolates past the open pose (straighter fingers).
            const float Curl = FMath::Clamp(FingerAlpha[F], -UGratiaHandAnimInstance::Extension(F), 1.0f);
            // Thumb: extended (0) to relaxed (1); index: its own curl; others: grasp.
            if (F == 0 && ThumbA) { Out[Index].Blend((*ThumbA)[Index], A[Index], Curl); continue; }
            const FCompactPose& Target = F == 1 && IndexB ? *IndexB : B;
            Out[Index].Blend(A[Index], Target[Index], Curl);
        }
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        if (!Open || !Closed) { Output.ResetToRefPose(); return true; }
        const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
        CacheHandBones(Bones);
        FCompactPose OpenPose, ClosedPose, IndexPose, ThumbPose;
        OpenPose.SetBoneContainer(&Bones); ClosedPose.SetBoneContainer(&Bones); IndexPose.SetBoneContainer(&Bones); ThumbPose.SetBoneContainer(&Bones);
        FBlendedCurve CurveA, CurveB, CurveC, CurveD;
        UE::Anim::FStackAttributeContainer AttrA, AttrB, AttrC, AttrD;
        SamplePose(Open, 0.0, OpenPose, CurveA, AttrA);
        SamplePose(Closed, Closed->GetPlayLength(), ClosedPose, CurveB, AttrB);
        const bool bIndex = IndexClosed && SamplePose(IndexClosed, IndexClosed->GetPlayLength(), IndexPose, CurveC, AttrC);
        const bool bThumb = ThumbOpen && SamplePose(ThumbOpen, ThumbOpen->GetPlayLength(), ThumbPose, CurveD, AttrD);
        BlendInto(Output.Pose, OpenPose, ClosedPose, bIndex ? &IndexPose : nullptr, bThumb ? &ThumbPose : nullptr, Alpha);
        CloseSpread(Output.Pose);
        if (Owner && !Owner->bSamplesReady.load())
        {
            // Component-space finger joints for every uniform curl step; the game thread
            // tests these against contact spheres to find where each finger touches.
            FCompactPose Step; Step.SetBoneContainer(&Bones);
            for (int32 K = 0; K <= UGratiaHandAnimInstance::NumSamples; ++K)
            {
                float Uniform[UGratiaHandAnimInstance::NumFingers];
                for (int32 F = 0; F < UGratiaHandAnimInstance::NumFingers; ++F) Uniform[F] = UGratiaHandAnimInstance::CurlAt(F, K);
                BlendInto(Step, OpenPose, ClosedPose, bIndex ? &IndexPose : nullptr, bThumb ? &ThumbPose : nullptr, Uniform);
                FCSPose<FCompactPose> Component; Component.InitPose(Step);
                if (K == 0)
                {
                    Owner->WristSample = WristJoint != INDEX_NONE ? Component.GetComponentSpaceTransform(FCompactPoseBoneIndex(WristJoint)).GetLocation() : FVector::ZeroVector;
                    Owner->bPalmBone = PalmJoint != INDEX_NONE;
                    Owner->PalmSample = Owner->bPalmBone ? Component.GetComponentSpaceTransform(FCompactPoseBoneIndex(PalmJoint)).GetLocation() : FVector::ZeroVector;
                }
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
    if (!IndexClosedPose) IndexClosedPose = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/XRMannequins/Animations/A_MannequinsXR_IndexCurl_Right.A_MannequinsXR_IndexCurl_Right"));
    if (!ThumbOpenPose) ThumbOpenPose = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/XRMannequins/Animations/A_MannequinsXR_ThumbUp_Right.A_MannequinsXR_ThumbUp_Right"));
    if (!MirrorTable) MirrorTable = LoadObject<UMirrorDataTable>(nullptr, TEXT("/Game/XRMannequins/Animations/MDT_MannequinsXR.MDT_MannequinsXR"));
    return OpenPose && ClosedPose && (!bLeftHand || MirrorTable);
}

float UGratiaHandAnimInstance::CapFinger(int32 Finger, const FTransform& Component) const
{
    if (!bConform || (ConformSpheres.IsEmpty() && ConformCapsules.IsEmpty()) || !bSamplesReady.load()) return 1.0f;
    const float Clearance = FingerRadiusCm + ConformMarginCm;
    // Deepest overlap of the moving joints and tip with any contact shape at curl sample K.
    // The knuckle (P = 0) does not move with the curl; a palm resting on the body must not
    // block the fingers from wrapping, so it is not tested.
    auto Overlap = [&](int32 K)
    {
        double Deepest = -1.0e9;
        for (int32 P = 1; P < PointsPerFinger; ++P)
        {
            const FVector Point = Component.TransformPosition(Samples[Finger][K][P]);
            for (const FVector4& Sphere : ConformSpheres)
                Deepest = FMath::Max(Deepest, Sphere.W + Clearance - FVector::Distance(Point, FVector(Sphere.X, Sphere.Y, Sphere.Z)));
            for (const FGratiaConformCapsule& Capsule : ConformCapsules)
                Deepest = FMath::Max(Deepest, Capsule.Radius + Clearance - FMath::PointDistToSegment(Point, Capsule.A, Capsule.B));
        }
        return Deepest;
    };
    auto Touches = [&](int32 K) { return Overlap(K) > 0.0; };
    if (Touches(0))
    {
        // Already in contact when open (tight spot): take the least overlapping curl.
        int32 Best = 0;
        double Least = Overlap(0);
        for (int32 K = 1; K <= NumSamples; ++K)
        {
            const double Value = Overlap(K);
            if (Value < Least) { Least = Value; Best = K; }
        }
        return CurlAt(Finger, Best);
    }
    for (int32 K = 1; K <= NumSamples; ++K)
        if (Touches(K)) return CurlAt(Finger, K - 1);
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
        const float Scaled = FMath::Clamp((FingerAlpha[F] + Extension(F)) / (1.0f + Extension(F)), 0.0f, 1.0f) * NumSamples;
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
    FGratiaPalmFrame Frame;
    if (!GetPalmFrame(Frame)) return false;
    OutWorld = Mesh->GetComponentTransform().TransformPosition(Frame.Point);
    return !OutWorld.ContainsNaN();
}

bool UGratiaHandAnimInstance::GetPalmFrame(FGratiaPalmFrame& Out) const
{
    if (!bSamplesReady.load()) return false;
    FVector Knuckles = FVector::ZeroVector, Closed = FVector::ZeroVector;
    for (int32 F = 1; F < NumFingers; ++F)
    {
        Knuckles += Samples[F][0][0];
        // Curled finger tips lie on the palm side of the knuckle line.
        Closed += Samples[F][NumSamples][3];
    }
    Knuckles /= double(NumFingers - 1);
    Closed /= double(NumFingers - 1);
    Out.Point = bPalmBone ? PalmSample : FMath::Lerp(WristSample, Knuckles, 0.6);
    Out.Finger = (Knuckles - WristSample).GetSafeNormal();
    Out.Normal = FVector::VectorPlaneProject(Closed - Knuckles, Out.Finger).GetSafeNormal();
    Out.RestPoints.Reset();
    for (int32 F = 0; F < NumFingers; ++F)
        for (int32 P = 1; P < PointsPerFinger; ++P) Out.RestPoints.Add(Samples[F][0][P]);
    return !Out.Point.ContainsNaN() && Out.Finger.IsNormalized() && Out.Normal.IsNormalized();
}

FString UGratiaHandAnimInstance::GetDiagnostics() const
{
    return FString::Printf(TEXT("curl T%.2f I%.2f M%.2f R%.2f P%.2f cap T%.2f I%.2f M%.2f R%.2f P%.2f%s"),
        FingerAlpha[0], FingerAlpha[1], FingerAlpha[2], FingerAlpha[3], FingerAlpha[4],
        FingerCap[0], FingerCap[1], FingerCap[2], FingerCap[3], FingerCap[4], bSamplesReady.load() ? TEXT("") : TEXT(" (sampling)"));
}
