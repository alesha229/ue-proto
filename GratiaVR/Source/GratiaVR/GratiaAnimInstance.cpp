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
#include "AnimNode_KawaiiPhysics.h"

namespace
{
// KawaiiPhysics as a native node: the input pose is the already evaluated pose,
// not a linked graph pin (an unlinked pin would reset to the reference pose).
struct FGratiaKawaiiNode : public FAnimNode_KawaiiPhysics
{
    virtual void UpdateComponentPose_AnyThread(const FAnimationUpdateContext&) override {}
    virtual void EvaluateComponentPose_AnyThread(FComponentSpacePoseContext&) override {}
};

struct FGratiaKawaiiChain
{
    TUniquePtr<FGratiaKawaiiNode> Node;
    TArray<FName> Roots;
    int32 FirstHandLimit = 0;
    bool bInitialized = false;
    uint16 BoneSerial = 0;
};

constexpr int32 GratiaKawaiiMaxHandSpheres = 24;

EBoneForwardAxis ToKawaiiAxis(EGratiaBoneAxis Axis)
{
    switch (Axis)
    {
    case EGratiaBoneAxis::XNegative: return EBoneForwardAxis::X_Negative;
    case EGratiaBoneAxis::YPositive: return EBoneForwardAxis::Y_Positive;
    case EGratiaBoneAxis::YNegative: return EBoneForwardAxis::Y_Negative;
    case EGratiaBoneAxis::ZPositive: return EBoneForwardAxis::Z_Positive;
    case EGratiaBoneAxis::ZNegative: return EBoneForwardAxis::Z_Negative;
    default: return EBoneForwardAxis::X_Positive;
    }
}

FTransform GratiaKawaiiRefTransform(const FReferenceSkeleton& Ref, int32 Index)
{
    FTransform Result = Ref.GetRefBonePose()[Index];
    for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
        Result *= Ref.GetRefBonePose()[Parent];
    return Result;
}
}

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
    TArray<FGratiaKawaiiChain> Kawaii;
    bool bSoftBody = false;
    bool bSoftBodyReset = false;
    TArray<FGratiaSoftBodyGrab> Grabs;
    int32* ActiveChainsOut = nullptr;

    void BuildSoftBody(const UGratiaCharacterProfile* Profile, const USkeletalMesh* Mesh, UAnimInstance* InInstance)
    {
        Kawaii.Reset();
        if (!Profile || !Mesh || !Profile->SoftBody.bEnabled) return;
        const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
        for (const FGratiaSoftBodyChain& Definition : Profile->SoftBody.Chains)
        {
            TArray<FName> Roots;
            for (const FName Bone : Definition.RootBones) if (Ref.FindBoneIndex(Bone) != INDEX_NONE) Roots.Add(Bone);
            if (Roots.IsEmpty()) continue;
            FGratiaKawaiiChain& Chain = Kawaii.AddDefaulted_GetRef();
            Chain.Roots = Roots;
            Chain.Node = MakeUnique<FGratiaKawaiiNode>();
            FGratiaKawaiiNode& Node = *Chain.Node;
            Node.RootBone = FBoneReference(Roots[0]);
            for (int32 I = 1; I < Roots.Num(); ++I)
            {
                FKawaiiPhysicsRootBoneSetting Extra;
                Extra.RootBone = FBoneReference(Roots[I]);
                Node.AdditionalRootBones.Add(Extra);
            }
            Node.DummyBoneLength = Definition.DummyBoneLengthCm;
            Node.BoneForwardAxis = ToKawaiiAxis(Definition.ForwardAxis);
            Node.PhysicsSettings.Damping = Definition.Damping;
            Node.PhysicsSettings.Stiffness = Definition.Stiffness;
            Node.PhysicsSettings.WorldDampingLocation = Definition.WorldDampingLocation;
            Node.PhysicsSettings.WorldDampingRotation = Definition.WorldDampingRotation;
            Node.PhysicsSettings.Radius = Definition.CollisionRadiusCm;
            Node.PhysicsSettings.LimitAngle = Definition.LimitAngleDegrees;
            Node.Gravity = FVector(0, 0, -980.0 * Definition.GravityScale);
            Node.TargetFramerate = 90;
            Node.bUpdatePhysicsSettingsInGame = true;
            // Source body collision follows its skinning bones.
            for (const FGratiaBodyColliderSphere& Sphere : Profile->SoftBody.BodyColliders)
            {
                const int32 Bone = Ref.FindBoneIndex(Sphere.Bone);
                if (Bone == INDEX_NONE) continue;
                FSphericalLimit Limit;
                Limit.DrivingBone = FBoneReference(Sphere.Bone);
                Limit.OffsetLocation = GratiaKawaiiRefTransform(Ref, Bone).InverseTransformPosition(Sphere.RefCenterCm);
                Limit.Radius = Sphere.RadiusCm;
                Limit.LimitType = ESphericalLimitType::Outer;
                Node.SphericalLimits.Add(Limit);
            }
            // Hand/finger slots are driven from the root bone and moved every update.
            Chain.FirstHandLimit = Node.SphericalLimits.Num();
            for (int32 I = 0; I < GratiaKawaiiMaxHandSpheres; ++I)
            {
                FSphericalLimit Limit;
                Limit.DrivingBone = FBoneReference(Ref.GetBoneName(0));
                Limit.OffsetLocation = FVector(0, 0, -1.0e6);
                Limit.Radius = 0.0f;
                Limit.LimitType = ESphericalLimitType::Outer;
                Node.SphericalLimits.Add(Limit);
            }
            Node.OnInitializeAnimInstance(this, InInstance);
        }
    }

    virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override
    {
        FAnimSingleNodeInstanceProxy::UpdateAnimationNode(InContext);
        if (!bSoftBody) return;
        const uint16 Serial = GetRequiredBones().GetSerialNumber();
        for (FGratiaKawaiiChain& Chain : Kawaii)
        {
            if (!Chain.bInitialized || Chain.BoneSerial != Serial)
            {
                FAnimationInitializeContext Init(this);
                Chain.Node->Initialize_AnyThread(Init);
                FAnimationCacheBonesContext Cache(this);
                Chain.Node->CacheBones_AnyThread(Cache);
                Chain.bInitialized = true; Chain.BoneSerial = Serial;
            }
            if (bSoftBodyReset) Chain.Node->ResetDynamics(ETeleportType::ResetPhysics);
            Chain.Node->Update_AnyThread(InContext);
        }
        bSoftBodyReset = false;
    }

    void EvaluateSoftBody(FPoseContext& Output)
    {
        if (!bSoftBody || Kawaii.IsEmpty()) { if (ActiveChainsOut) *ActiveChainsOut = 0; return; }
        FComponentSpacePoseContext ComponentPose(this);
        ComponentPose.Pose.InitPose(Output.Pose);
        ComponentPose.Curve = Output.Curve;
        int32 Active = 0;
        for (FGratiaKawaiiChain& Chain : Kawaii)
        {
            if (!Chain.bInitialized) continue;
            // VRChat-style grab: move the simulated tip toward the hand target. Location and
            // previous location move together, so the grab adds no velocity; the chain's own
            // stiffness pulls back toward the pose and the bone length stays constrained.
            for (const FGratiaSoftBodyGrab& Grab : Grabs)
            {
                if (!Chain.Roots.Contains(Grab.RootBone)) continue;
                for (FKawaiiPhysicsModifyBone& Bone : Chain.Node->ModifyBones)
                {
                    if (!Bone.bDummy || !Chain.Node->ModifyBones.IsValidIndex(Bone.ParentIndex)
                        || Chain.Node->ModifyBones[Bone.ParentIndex].BoneRef.BoneName != Grab.RootBone) continue;
                    const FVector Desired = Bone.PoseLocation + (Grab.TargetCS - Bone.PoseLocation).GetClampedToMaxSize(Grab.MaxStretchCm);
                    const FVector Shift = (Desired - Bone.Location) * FMath::Clamp(Grab.Movement, 0.0f, 1.0f);
                    if (Shift.ContainsNaN()) continue;
                    Bone.Location += Shift; Bone.PrevLocation += Shift;
                }
            }
            Chain.Node->EvaluateComponentSpace_AnyThread(ComponentPose);
            ++Active;
        }
        FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(ComponentPose.Pose, Output.Pose);
        if (ActiveChainsOut) *ActiveChainsOut = Active;
    }

    virtual void PreUpdate(UAnimInstance* InInstance, float DeltaSeconds) override
    {
        FAnimSingleNodeInstanceProxy::PreUpdate(InInstance, DeltaSeconds);
        UGratiaAnimInstance* Instance = CastChecked<UGratiaAnimInstance>(InInstance);
        ActiveChainsOut = &Instance->ActiveSoftBodyChains;
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
            BuildSoftBody(Profile, Mesh, InInstance);
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
        // Soft body input: hand spheres move the reserved root-driven limits.
        bSoftBody = Instance->bSoftBody && !Kawaii.IsEmpty() && Profile && Mesh;
        bSoftBodyReset |= Instance->SoftBodyInput.bReset;
        Instance->SoftBodyInput.bReset = false;
        Grabs = Instance->SoftBodyInput.Grabs;
        if (bSoftBody && GetSkelMeshComponent() && !GetSkelMeshComponent()->GetComponentSpaceTransforms().IsEmpty())
        {
            const FTransform Root = GetSkelMeshComponent()->GetComponentSpaceTransforms()[0];
            for (FGratiaKawaiiChain& Chain : Kawaii)
                for (int32 I = 0; I < GratiaKawaiiMaxHandSpheres; ++I)
                {
                    FSphericalLimit& Limit = Chain.Node->SphericalLimits[Chain.FirstHandLimit + I];
                    const bool bActive = Instance->SoftBodyInput.HandSpheres.IsValidIndex(I);
                    const FVector4 Sphere = bActive ? Instance->SoftBodyInput.HandSpheres[I] : FVector4(0, 0, -1.0e6, 0);
                    Limit.OffsetLocation = Root.InverseTransformPosition(FVector(Sphere.X, Sphere.Y, Sphere.Z));
                    Limit.Radius = bActive ? float(Sphere.W) : 0.0f;
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
        const bool Result = EvaluateProcedural(Output);
        if (Result) EvaluateSoftBody(Output);
        return Result;
    }

    bool EvaluateProcedural(FPoseContext& Output)
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
