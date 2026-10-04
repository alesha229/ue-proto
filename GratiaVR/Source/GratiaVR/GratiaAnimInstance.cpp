#include "GratiaAnimInstance.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaSecondaryBones.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "Components/SkeletalMeshComponent.h"

struct FGratiaSpringBone
{
    FName Name;
    uint8 Group = 0;
    float Angle = 0.0f;
    float Velocity = 0.0f;
    float Falloff = 1.0f;
};

struct FGratiaAnimProxy : public FAnimSingleNodeInstanceProxy
{
    FGratiaAnimProxy(UAnimInstance* Instance) : FAnimSingleNodeInstanceProxy(Instance) {}
    float Yaw = 0.0f, Pitch = 0.0f, Response = 0.0f;
    bool bEnabled = true;
    UAnimSequence* Cue = nullptr;
    float CueTime = 2.0f;
    FVector HeadYawAxis = FVector::UpVector, HeadNodAxis = FVector::ForwardVector;
    TArray<FGratiaSpringBone> Springs;

    virtual void PreUpdate(UAnimInstance* InInstance, float DeltaSeconds) override
    {
        FAnimSingleNodeInstanceProxy::PreUpdate(InInstance, DeltaSeconds);
        const UGratiaAnimInstance* Instance = CastChecked<UGratiaAnimInstance>(InInstance);
        Yaw = Instance->HeadYaw; Pitch = Instance->HeadPitch; Response = Instance->Reaction; bEnabled = Instance->bProcedural;
        Cue = Instance->ReactionClip; CueTime = Instance->ReactionTime;
        if (Springs.IsEmpty())
        {
            const FReferenceSkeleton& Ref = GetSkelMeshComponent()->GetSkeletalMeshAsset()->GetRefSkeleton();
            int32 HeadIndex = Ref.FindBoneIndex(FName("DEF-spine_006"));
            if (HeadIndex != INDEX_NONE)
            {
                FTransform Rest = Ref.GetRefBonePose()[HeadIndex];
                int32 Parent = Ref.GetParentIndex(HeadIndex);
                while (Parent != INDEX_NONE) { Rest = Rest * Ref.GetRefBonePose()[Parent]; Parent = Ref.GetParentIndex(Parent); }
                HeadYawAxis = Rest.GetRotation().Inverse().RotateVector(FVector::UpVector).GetSafeNormal();
                HeadNodAxis = Rest.GetRotation().Inverse().RotateVector(FVector::ForwardVector).GetSafeNormal();
            }
            for (int32 I = 0; I < Ref.GetNum(); ++I)
            {
                const FName BoneName = Ref.GetBoneName(I);
                const auto* Definition = GratiaSecondaryBones::Find(BoneName);
                if (!Definition || !Definition->bSafeDefaultSimulation) continue;
                int32 Depth = 0, Parent = Ref.GetParentIndex(I);
                while (Parent != INDEX_NONE && GratiaSecondaryBones::Find(Ref.GetBoneName(Parent)))
                { ++Depth; Parent = Ref.GetParentIndex(Parent); }
                FGratiaSpringBone Bone; Bone.Name = BoneName; Bone.Group = Definition->Group;
                Bone.Falloff = 1.0f / FMath::Square(1.0f + Depth);
                Springs.Add(Bone);
            }
        }
        if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
        const float Time = FMath::Min(DeltaSeconds, 0.05f);
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(Time / (1.0f / 240.0f)));
        const float Dt = Time / Steps;
        for (FGratiaSpringBone& Bone : Springs)
        {
            const bool Enabled = Instance->bSpring && (Bone.Group == 4 ? Instance->bEars : Bone.Group == 1 ? Instance->bHair : Bone.Group == 2 ? Instance->bCloth : Instance->bBody);
            const float Limit = (Bone.Group == 1 ? 3.0f : Bone.Group == 2 ? 4.0f : Bone.Group == 4 ? 4.0f : 1.0f) * Bone.Falloff;
            const float Inertia = Bone.Group == 1 || Bone.Group == 4 ? Instance->HeadVelocity * -0.012f : 0.0f;
            const float Target = Enabled ? FMath::Clamp(Instance->Impulse * Limit + Instance->Reaction * 0.4f * Bone.Falloff + Inertia, -Limit, Limit) : 0.0f;
            for (int32 I = 0; I < Steps; ++I)
            {
                // Critically damped angular spring, small bounded local deflection, no root simulation.
                Bone.Velocity += (90.0f * (Target - Bone.Angle) - 19.0f * Bone.Velocity) * Dt;
                Bone.Angle = FMath::Clamp(Bone.Angle + Bone.Velocity * Dt, -Limit, Limit);
                if (!FMath::IsFinite(Bone.Angle) || !FMath::IsFinite(Bone.Velocity)) Bone.Angle = Bone.Velocity = 0.0f;
            }
        }
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        const bool Result = FAnimSingleNodeInstanceProxy::Evaluate(Output);
        if (!Result || !bEnabled) return Result;
        if (Cue && CueTime < Cue->GetPlayLength())
        {
            FPoseContext CuePose(Output);
            FAnimationPoseData CueData(CuePose);
            Cue->GetAnimationPose(CueData, FAnimExtractContext(CueTime, false));
            FAnimationPoseData BaseData(Output);
            const float Weight = FMath::SmoothStep(0.0f, 0.15f, CueTime) *
                FMath::SmoothStep(0.0f, 0.25f, Cue->GetPlayLength() - CueTime);
            // Blend morph curves with the pose; the exported Game_ correctives
            // must follow the cue instead of only the idle SingleNode curves.
            FAnimationRuntime::BlendTwoPosesTogetherInPlace(BaseData, CueData, 1.0f - Weight);
        }
        const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
        auto Rotate = [&](FName Name, const FQuat& Rotation)
        {
            const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
            if (MeshIndex == INDEX_NONE) return;
            const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
            if (Index.GetInt() == INDEX_NONE) return;
            FTransform& Transform = Output.Pose[Index];
            Transform.SetRotation((Transform.GetRotation() * Rotation).GetNormalized());
        };
        // Derive local axes from the imported rest frame: FBX bone Z is not world up.
        Rotate(TEXT("DEF-spine_006"), FQuat(HeadYawAxis, FMath::DegreesToRadians(Yaw)) * FQuat(HeadNodAxis, FMath::DegreesToRadians(Pitch)));
        for (const FGratiaSpringBone& Bone : Springs) Rotate(Bone.Name, FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Bone.Angle)));
        return Result;
    }
};

UGratiaAnimInstance::UGratiaAnimInstance(const FObjectInitializer& Initializer) : Super(Initializer) {}

FAnimInstanceProxy* UGratiaAnimInstance::CreateAnimInstanceProxy() { return new FGratiaAnimProxy(this); }

void UGratiaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    AGratiaPreviewCharacter* Character = Cast<AGratiaPreviewCharacter>(GetOwningActor());
    if (!Character || !Character->Interaction || !FMath::IsFinite(DeltaSeconds)) return;
    UGratiaInteraction* Interaction = Character->Interaction;
    bProcedural = Character->IsIdlePreview();
    if (!bProcedural || Interaction->ActiveZone == INDEX_NONE && Interaction->Reaction < 0.001f)
    {
        // The exported cue finishes in neutral; after a reset it is cancelled.
        if (!bProcedural || Interaction->ReactionSerial == 0) ReactionTime = 2.0f;
    }
    if (bProcedural && Interaction->ReactionSerial != 0 && Interaction->ReactionSerial != LastReactionSerial)
    {
        LastReactionSerial = Interaction->ReactionSerial;
        ReactionClip = Character->GetReactionAnimation(Interaction->Mood == 1 || Interaction->Impulse > 0.7f);
        ReactionTime = 0.0f;
    }
    if (Interaction->ReactionSerial == 0) LastReactionSerial = 0;
    ReactionTime = FMath::Min(2.0f, ReactionTime + FMath::Clamp(DeltaSeconds, 0.0f, 0.05f));
    Reaction = Interaction->Reaction; Impulse = Interaction->Impulse;
    bBody = Interaction->bBodyMotion; bHair = Interaction->bHairMotion; bCloth = Interaction->bClothMotion; bSpring = Interaction->bLocalSpring;
    bEars = Interaction->bEarMotion;
    FVector Direction = Character->GetActorTransform().InverseTransformVectorNoScale(Interaction->LookTarget - GetSkelMeshComponent()->GetSocketLocation(TEXT("DEF-spine_006")));
    const bool Ahead = Direction.Y > 10.0;
    const float DesiredYaw = Ahead ? FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(-Direction.X, Direction.Y)), -28.0, 28.0) : 0.0f;
    const float DesiredPitch = Ahead ? FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Direction.Z, Direction.Size2D())), -12.0, 12.0) : 0.0f;
    const float PreviousYaw = HeadYaw;
    HeadYaw = FMath::FInterpTo(HeadYaw, bProcedural ? DesiredYaw : 0.0f, FMath::Min(DeltaSeconds, 0.05f), 4.0f);
    HeadVelocity = DeltaSeconds > 0.001f ? FMath::Clamp((HeadYaw - PreviousYaw) / DeltaSeconds, -120.0f, 120.0f) : 0.0f;
    HeadPitch = FMath::FInterpTo(HeadPitch, bProcedural ? DesiredPitch : 0.0f, FMath::Min(DeltaSeconds, 0.05f), 4.0f);
}
