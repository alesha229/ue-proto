#include "GratiaAnimInstance.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaCharacterProfile.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

struct FGratiaSpringBone
{
    FName Name;
    uint8 Group = 0;
    float Angle = 0.0f;
    float Velocity = 0.0f;
    float Falloff = 1.0f;
    FGratiaSecondaryGroupSettings Settings;
};

struct FGratiaAnimProxy : public FAnimSingleNodeInstanceProxy
{
    FGratiaAnimProxy(UAnimInstance* Instance) : FAnimSingleNodeInstanceProxy(Instance) {}
    float Yaw = 0.0f, Pitch = 0.0f, Response = 0.0f;
    bool bEnabled = true;
    UAnimSequence* Cue = nullptr;
    float CueTime = 2.0f;
    FVector HeadYawAxis = FVector::UpVector, HeadNodAxis = FVector::ForwardVector;
    FName HeadBone;
    TWeakObjectPtr<UGratiaCharacterProfile> CachedProfile;
    TWeakObjectPtr<USkeletalMesh> CachedMesh;
    TArray<FGratiaSpringBone> Springs;

    virtual void PreUpdate(UAnimInstance* InInstance, float DeltaSeconds) override
    {
        FAnimSingleNodeInstanceProxy::PreUpdate(InInstance, DeltaSeconds);
        const UGratiaAnimInstance* Instance = CastChecked<UGratiaAnimInstance>(InInstance);
        const AGratiaPreviewCharacter* Character = Cast<AGratiaPreviewCharacter>(Instance->GetOwningActor());
        UGratiaCharacterProfile* Profile = Character ? Character->CharacterProfile.Get() : nullptr;
        USkeletalMesh* Mesh = GetSkelMeshComponent() ? GetSkelMeshComponent()->GetSkeletalMeshAsset() : nullptr;
        Yaw = Instance->HeadYaw; Pitch = Instance->HeadPitch; Response = Instance->Reaction;
        bEnabled = Instance->bProcedural && Profile && Mesh;
        Cue = Instance->ReactionClip; CueTime = Instance->ReactionTime;
        if (CachedProfile.Get() != Profile || CachedMesh.Get() != Mesh)
        {
            CachedProfile = Profile; CachedMesh = Mesh;
            Springs.Reset(); HeadBone = NAME_None;
            if (!Profile || !Mesh) return;
            HeadBone = Profile->ResolveBone(TEXT("Head"));
            const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
            int32 HeadIndex = Ref.FindBoneIndex(HeadBone);
            if (HeadIndex != INDEX_NONE)
            {
                FTransform Rest = Ref.GetRefBonePose()[HeadIndex];
                int32 Parent = Ref.GetParentIndex(HeadIndex);
                while (Parent != INDEX_NONE) { Rest = Rest * Ref.GetRefBonePose()[Parent]; Parent = Ref.GetParentIndex(Parent); }
                const FVector Up = Profile->UpAxis.GetSafeNormal();
                const FVector Right = FVector::CrossProduct(Up, Profile->ForwardAxis.GetSafeNormal()).GetSafeNormal();
                HeadYawAxis = Rest.GetRotation().Inverse().RotateVector(Up).GetSafeNormal();
                HeadNodAxis = Rest.GetRotation().Inverse().RotateVector(-Right).GetSafeNormal();
            }
            for (int32 I = 0; I < Ref.GetNum(); ++I)
            {
                const FName BoneName = Ref.GetBoneName(I);
                const auto* Definition = Profile->FindSecondaryBone(BoneName);
                if (!Definition || !Definition->bSafeSimulation) continue;
                int32 Depth = 0, Parent = Ref.GetParentIndex(I);
                while (Parent != INDEX_NONE && Profile->FindSecondaryBone(Ref.GetBoneName(Parent)))
                { ++Depth; Parent = Ref.GetParentIndex(Parent); }
                FGratiaSpringBone Bone; Bone.Name = BoneName; Bone.Group = Definition->Group;
                Bone.Settings = Profile->GetSecondaryGroupSettings(Definition->Group);
                Bone.Falloff = 1.0f / FMath::Square(1.0f + Depth);
                Springs.Add(Bone);
            }
        }
        if (!bEnabled) return;
        if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
        const float Time = FMath::Min(DeltaSeconds, 0.05f);
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(Time / (1.0f / 240.0f)));
        const float Dt = Time / Steps;
        for (FGratiaSpringBone& Bone : Springs)
        {
            const bool Enabled = Instance->bSpring && (Bone.Group == 4 ? Instance->bEars : Bone.Group == 1 ? Instance->bHair : Bone.Group == 2 ? Instance->bCloth : Instance->bBody);
            const float Limit = Bone.Settings.SpringLimitDegrees * Bone.Falloff;
            const float Inertia = Instance->HeadVelocity * Bone.Settings.HeadInertiaScale;
            const float Target = Enabled ? FMath::Clamp(Instance->Impulse * Limit + Instance->Reaction * 0.4f * Bone.Falloff + Inertia, -Limit, Limit) : 0.0f;
            for (int32 I = 0; I < Steps; ++I)
            {
                // Critically damped angular spring, small bounded local deflection, no root simulation.
                Bone.Velocity += (Bone.Settings.SpringStiffness * (Target - Bone.Angle) - Bone.Settings.SpringDamping * Bone.Velocity) * Dt;
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
        if (!HeadBone.IsNone()) Rotate(HeadBone, FQuat(HeadYawAxis, FMath::DegreesToRadians(Yaw)) * FQuat(HeadNodAxis, FMath::DegreesToRadians(Pitch)));
        for (const FGratiaSpringBone& Bone : Springs) Rotate(Bone.Name, FQuat(Bone.Settings.SpringLocalAxis.GetSafeNormal(), FMath::DegreesToRadians(Bone.Angle)));
        return Result;
    }
};

UGratiaAnimInstance::UGratiaAnimInstance(const FObjectInitializer& Initializer) : Super(Initializer) {}

FAnimInstanceProxy* UGratiaAnimInstance::CreateAnimInstanceProxy() { return new FGratiaAnimProxy(this); }

FGratiaAnimationSnapshot UGratiaAnimationProfileLibrary::GetCharacterAnimationSnapshot(AGratiaPreviewCharacter* Character)
{
    FGratiaAnimationSnapshot Result;
    if (!Character || !Character->CharacterProfile || !Character->Interaction || !Character->CharacterMesh) return Result;
    UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    const UGratiaInteraction* Interaction = Character->Interaction;
    Result.Profile = Profile; Result.LookTarget = Interaction->LookTarget;
    Result.ReactionWeight = Interaction->Reaction; Result.ReactionImpulse = Interaction->Impulse;
    Result.ReactionSerial = static_cast<int32>(Interaction->ReactionSerial);
    Result.Mood = Interaction->Mood; Result.Quality = Interaction->Quality;
    Result.bIdle = Character->IsIdlePreview(); Result.bGaze = Profile->Capabilities.bGaze;
    Result.bBodyMotion = Interaction->bBodyMotion; Result.bHairMotion = Interaction->bHairMotion;
    Result.bEarMotion = Interaction->bEarMotion; Result.bClothMotion = Interaction->bClothMotion;
    Result.bLocalSpring = Interaction->bLocalSpring && Profile->Capabilities.bLocalSprings;
    const FName Head = Profile->ResolveBone(TEXT("Head"));
    if (!Result.bGaze || Head.IsNone() || Character->CharacterMesh->GetBoneIndex(Head) == INDEX_NONE) return Result;
    const FVector Direction = Character->GetActorTransform().InverseTransformVectorNoScale(
        Result.LookTarget - Character->CharacterMesh->GetSocketLocation(Head));
    const FVector Forward = Profile->ForwardAxis.GetSafeNormal(), Up = Profile->UpAxis.GetSafeNormal();
    const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
    const double Ahead = FVector::DotProduct(Direction, Forward);
    const double Side = FVector::DotProduct(Direction, Right);
    const double Height = FVector::DotProduct(Direction, Up);
    if (!Direction.ContainsNaN() && Ahead > 10.0)
    {
        Result.TargetHeadYaw = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Side, Ahead)),
            -double(Profile->MaxHeadYawDegrees), double(Profile->MaxHeadYawDegrees));
        Result.TargetHeadPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Height, FMath::Sqrt(Ahead * Ahead + Side * Side))),
            -double(Profile->MaxHeadPitchDegrees), double(Profile->MaxHeadPitchDegrees));
    }
    return Result;
}

void UGratiaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    AGratiaPreviewCharacter* Character = Cast<AGratiaPreviewCharacter>(GetOwningActor());
    if (!Character || !Character->CharacterProfile || !Character->Interaction || !FMath::IsFinite(DeltaSeconds))
    {
        bProcedural = false; HeadYaw = HeadPitch = HeadVelocity = Reaction = Impulse = 0.0f;
        ReactionClip = nullptr; LastReactionSerial = 0; return;
    }
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    const FGratiaAnimationSnapshot Snapshot = UGratiaAnimationProfileLibrary::GetCharacterAnimationSnapshot(Character);
    UGratiaInteraction* Interaction = Character->Interaction;
    bProcedural = Snapshot.bIdle;
    if (!Profile->Capabilities.bReactionAnimations) { ReactionClip = nullptr; LastReactionSerial = Interaction->ReactionSerial; }
    if (!bProcedural || Interaction->ActiveZone == INDEX_NONE && Interaction->Reaction < 0.001f)
    {
        // The exported cue finishes in neutral; after a reset it is cancelled.
        if (!bProcedural || Interaction->ReactionSerial == 0) ReactionTime = ReactionClip ? ReactionClip->GetPlayLength() : 2.0f;
    }
    if (Profile->Capabilities.bReactionAnimations && bProcedural && Interaction->ReactionSerial != 0 && Interaction->ReactionSerial != LastReactionSerial)
    {
        LastReactionSerial = Interaction->ReactionSerial;
        ReactionClip = Character->GetReactionAnimationForZone(Interaction->LastReactionZoneName);
        ReactionClipDuration = ReactionClip ? ReactionClip->GetPlayLength() : 0.0f;
        ReactionTime = 0.0f;
    }
    if (Interaction->ReactionSerial == 0) LastReactionSerial = 0;
    ReactionTime = FMath::Min(ReactionClip ? ReactionClip->GetPlayLength() : 2.0f, ReactionTime + FMath::Clamp(DeltaSeconds, 0.0f, 0.05f));
    Reaction = Interaction->Reaction; Impulse = Interaction->Impulse;
    bBody = Snapshot.bBodyMotion; bHair = Snapshot.bHairMotion; bCloth = Snapshot.bClothMotion; bSpring = Snapshot.bLocalSpring;
    bEars = Snapshot.bEarMotion;
    const float PreviousYaw = HeadYaw;
    HeadYaw = FMath::FInterpTo(HeadYaw, bProcedural ? Snapshot.TargetHeadYaw : 0.0f, FMath::Min(DeltaSeconds, 0.05f), Profile->GazeInterpSpeed);
    HeadVelocity = DeltaSeconds > 0.001f ? FMath::Clamp((HeadYaw - PreviousYaw) / DeltaSeconds, -120.0f, 120.0f) : 0.0f;
    HeadPitch = FMath::FInterpTo(HeadPitch, bProcedural ? Snapshot.TargetHeadPitch : 0.0f, FMath::Min(DeltaSeconds, 0.05f), Profile->GazeInterpSpeed);
}
