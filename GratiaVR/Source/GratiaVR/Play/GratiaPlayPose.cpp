#include "GratiaPlayPose.h"
#include "GratiaPlayMath.h"
#include "Animation/AnimNodeBase.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "AnimNode_KawaiiPhysics.h"

namespace
{
    FCompactPoseBoneIndex FindBone(const FBoneContainer& Bones, FName Name)
    {
        if (Name.IsNone()) return FCompactPoseBoneIndex(INDEX_NONE);
        const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
        return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
    }

    bool IsValidBone(const FCompactPoseBoneIndex& Index) { return Index.GetInt() != INDEX_NONE; }

    int32 Translate(FCSPose<FCompactPose>& Pose, const FBoneContainer& Bones, const TArray<FGratiaPlayBoneOffset>& Offsets)
    {
        TArray<FBoneTransform> Moved;
        for (const FGratiaPlayBoneOffset& Offset : Offsets)
        {
            const FCompactPoseBoneIndex Index = FindBone(Bones, Offset.Bone);
            if (!IsValidBone(Index) || Offset.OffsetCS.ContainsNaN() || Offset.OffsetCS.IsNearlyZero(0.01)) continue;
            FTransform Transform = Pose.GetComponentSpaceTransform(Index);
            Transform.AddToTranslation(Offset.OffsetCS);
            Moved.Emplace(Index, Transform);
        }
        if (Moved.IsEmpty()) return 0;
        Moved.Sort(FCompareBoneTransformIndex());
        Pose.SafeSetCSBoneTransforms(Moved);
        return Moved.Num();
    }
}

int32 GratiaPlayPose::ApplyBody(FPoseContext& Output, const FGratiaPlayPoseInput& Input)
{
    if (Input.BodyOffsets.IsEmpty() && Input.Limbs.IsEmpty()) return 0;
    const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
    FCSPose<FCompactPose> Pose;
    Pose.InitPose(Output.Pose);

    // Planted ends are taken before the body moves, so they stay where the animation put them.
    TArray<FVector, TInlineAllocator<8>> Planted;
    Planted.SetNumZeroed(Input.Limbs.Num());
    for (int32 I = 0; I < Input.Limbs.Num(); ++I)
    {
        const FCompactPoseBoneIndex End = FindBone(Bones, Input.Limbs[I].End);
        if (Input.Limbs[I].bPlant && IsValidBone(End)) Planted[I] = Pose.GetComponentSpaceTransform(End).GetLocation();
    }

    int32 Changed = Translate(Pose, Bones, Input.BodyOffsets);
    for (int32 I = 0; I < Input.Limbs.Num(); ++I)
    {
        const FGratiaPlayLimbGoal& Limb = Input.Limbs[I];
        const float Weight = FMath::IsFinite(Limb.Weight) ? FMath::Clamp(Limb.Weight, 0.0f, 1.0f) : 0.0f;
        const FCompactPoseBoneIndex Root = FindBone(Bones, Limb.Root), Mid = FindBone(Bones, Limb.Mid), End = FindBone(Bones, Limb.End);
        if (Weight <= 0.0f || !IsValidBone(Root) || !IsValidBone(Mid) || !IsValidBone(End)) continue;
        const FTransform RootT = Pose.GetComponentSpaceTransform(Root);
        const FTransform MidT = Pose.GetComponentSpaceTransform(Mid);
        const FTransform EndT = Pose.GetComponentSpaceTransform(End);
        const FVector Wanted = (Limb.bPlant ? Planted[I] : Limb.GoalCS) + Limb.ExtraCS;
        if (Wanted.ContainsNaN()) continue;
        const FVector Goal = FMath::Lerp(EndT.GetLocation(), Wanted, double(Weight));
        if (FVector::DistSquared(Goal, EndT.GetLocation()) < 1.0e-4) continue;
        FVector NewMid, NewEnd;
        if (!GratiaPlay::SolveTwoBone(RootT.GetLocation(), MidT.GetLocation(), EndT.GetLocation(), Goal, Limb.PoleCS, NewMid, NewEnd)) continue;

        FTransform NewRoot = RootT;
        NewRoot.SetRotation((FQuat::FindBetweenVectors(MidT.GetLocation() - RootT.GetLocation(), NewMid - RootT.GetLocation()) * RootT.GetRotation()).GetNormalized());
        FTransform NewMidT = MidT;
        NewMidT.SetLocation(NewMid);
        NewMidT.SetRotation((FQuat::FindBetweenVectors(EndT.GetLocation() - MidT.GetLocation(), NewEnd - NewMid) * MidT.GetRotation()).GetNormalized());
        FTransform NewEndT = EndT; // the hand/foot keeps its component-space orientation
        NewEndT.SetLocation(NewEnd);
        if (NewRoot.ContainsNaN() || NewMidT.ContainsNaN() || NewEndT.ContainsNaN()) continue;

        TArray<FBoneTransform> Chain;
        Chain.Emplace(Root, NewRoot);
        Chain.Emplace(Mid, NewMidT);
        Chain.Emplace(End, NewEndT);
        Chain.Sort(FCompareBoneTransformIndex());
        Pose.SafeSetCSBoneTransforms(Chain);
        Changed += Chain.Num();
    }
    if (Changed > 0) FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(Pose, Output.Pose);
    return Changed;
}

int32 GratiaPlayPose::ApplyLate(FPoseContext& Output, const FGratiaPlayPoseInput& Input)
{
    if (Input.LateOffsets.IsEmpty()) return 0;
    const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
    FCSPose<FCompactPose> Pose;
    Pose.InitPose(Output.Pose);
    const int32 Changed = Translate(Pose, Bones, Input.LateOffsets);
    if (Changed > 0) FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(Pose, Output.Pose);
    return Changed;
}

void GratiaPlayPose::FSoftTuner::Apply(FAnimNode_KawaiiPhysics& Node, uint8 Group, const FGratiaPlaySoftTuning& Tuning)
{
    FEntry& Entry = Entries.FindOrAdd(&Node);
    FKawaiiPhysicsSettings& Settings = Node.PhysicsSettings;
    // Values that differ from what was written last came from the profile (first use or a rebuilt chain): new base.
    const bool bNewBase = !Entry.bWritten || Settings.Damping != Entry.Written[0] || Settings.Stiffness != Entry.Written[1]
        || Settings.WorldDampingLocation != Entry.Written[2] || Settings.WorldDampingRotation != Entry.Written[3];
    if (bNewBase)
    {
        Entry.Damping = Settings.Damping;
        Entry.Stiffness = Settings.Stiffness;
        Entry.WorldLocation = Settings.WorldDampingLocation;
        Entry.WorldRotation = Settings.WorldDampingRotation;
    }
    const int32 G = Group < 5 ? Group : 0;
    const bool bOn = Tuning.bEnabled;
    Settings.Damping = FMath::Clamp(Entry.Damping * (bOn ? Tuning.Damping[G] : 1.0f), 0.0f, 1.0f);
    Settings.Stiffness = FMath::Clamp(Entry.Stiffness * (bOn ? Tuning.Stiffness[G] : 1.0f), 0.0f, 1.0f);
    Settings.WorldDampingLocation = FMath::Clamp(Entry.WorldLocation * (bOn ? Tuning.WorldDamping[G] : 1.0f), 0.0f, 1.0f);
    Settings.WorldDampingRotation = FMath::Clamp(Entry.WorldRotation * (bOn ? Tuning.WorldDamping[G] : 1.0f), 0.0f, 1.0f);
    const FVector Kick = bOn ? Tuning.KickWS[G] : FVector::ZeroVector;
    Node.SimpleExternalForce = Kick.ContainsNaN() ? FVector::ZeroVector : Kick;
    Entry.Written[0] = Settings.Damping;
    Entry.Written[1] = Settings.Stiffness;
    Entry.Written[2] = Settings.WorldDampingLocation;
    Entry.Written[3] = Settings.WorldDampingRotation;
    Entry.bWritten = true;
}
