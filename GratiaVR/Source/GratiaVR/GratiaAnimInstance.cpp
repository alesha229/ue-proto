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
#include "Misc/ScopeExit.h"

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
    /** Gravity relative to the authored pose: parent bone of the first root and its reference rotation. */
    int32 GravityParent = INDEX_NONE;
    FQuat GravityParentRef = FQuat::Identity;
    float GravityCmPerSecond2 = 0.0f;
    bool bInitialized = false;
    uint16 BoneSerial = 0;
    /** Spring chain (hair, decor, ears): runs with its menu group instead of the soft body. */
    bool bSpring = false;
    uint8 Group = 0;
    bool bRunning = false;
};

constexpr int32 GratiaKawaiiMaxHandSpheres = 24;
/** Spring chains: one sphere per hand. */
constexpr int32 GratiaKawaiiSpringHandSpheres = 2;

/** Translational soft part: a mass on a spring following its bone (world space, so it lags
 *  behind body accelerations and wobbles back). */
struct FGratiaJiggle
{
    FName Bone;
    FVector TipAxis = FVector::XAxisVector;
    float TipLengthCm = 0.0f;
    float Omega = 0.0f, Zeta = 0.2f, MaxCm = 2.0f;
    FVector Mass = FVector::ZeroVector, Velocity = FVector::ZeroVector;
    bool bInitialized = false;
};

FVector GratiaAnimAxisVector(EGratiaBoneAxis Axis)
{
    switch (Axis)
    {
    case EGratiaBoneAxis::XNegative: return -FVector::XAxisVector;
    case EGratiaBoneAxis::YPositive: return FVector::YAxisVector;
    case EGratiaBoneAxis::YNegative: return -FVector::YAxisVector;
    case EGratiaBoneAxis::ZPositive: return FVector::ZAxisVector;
    case EGratiaBoneAxis::ZNegative: return -FVector::ZAxisVector;
    default: return FVector::XAxisVector;
    }
}

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

/** Roots of other spring chains below this chain's roots (strands branching off a ponytail): their
 *  subtrees are simulated by their own chains, after the chain they hang from. */
TArray<int32> GratiaSpringBranches(const FReferenceSkeleton& Ref, const UGratiaCharacterProfile& Profile, const FGratiaSpringChain& Definition)
{
    TArray<int32> Roots, Branches;
    for (const FName Bone : Definition.RootBones) if (Ref.FindBoneIndex(Bone) != INDEX_NONE) Roots.Add(Ref.FindBoneIndex(Bone));
    for (const FGratiaSpringChain& Other : Profile.SpringChains)
    {
        if (&Other == &Definition) continue;
        for (const FName Bone : Other.RootBones)
        {
            const int32 Index = Ref.FindBoneIndex(Bone);
            for (int32 Parent = Index == INDEX_NONE ? INDEX_NONE : Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
                if (Roots.Contains(Parent)) { Branches.AddUnique(Index); break; }
        }
    }
    return Branches;
}

/** Reference points a spring chain simulates (bones below the roots and the tip dummies), and how
 *  far its free end reaches from the first root along the bones. */
void GratiaSpringRestPoints(const FReferenceSkeleton& Ref, const FGratiaSpringChain& Definition, const TArray<int32>& Branches, TArray<FVector>& Points, float& Reach)
{
    Reach = 0.0f;
    // Path length from the chain root; the skeleton lists parents before children.
    TMap<int32, float> Along;
    for (const FName Bone : Definition.RootBones)
        if (Ref.FindBoneIndex(Bone) != INDEX_NONE) Along.Add(Ref.FindBoneIndex(Bone), 0.0f);
    for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
    {
        const int32 Parent = Ref.GetParentIndex(Index);
        const float* ParentAlong = Parent != INDEX_NONE && !Along.Contains(Index) && !Branches.Contains(Index) ? Along.Find(Parent) : nullptr;
        if (!ParentAlong) continue;
        // Component-space distance: imported bones carry a scale, local offsets are not in cm.
        const FVector Location = GratiaKawaiiRefTransform(Ref, Index).GetLocation();
        const float Length = *ParentAlong + float(FVector::Distance(Location, GratiaKawaiiRefTransform(Ref, Parent).GetLocation()));
        Along.Add(Index, Length);
        Points.Add(Location);
        Reach = FMath::Max(Reach, Length);
    }
    for (const TPair<int32, float>& Bone : Along)
    {
        bool bEnd = true;
        for (int32 Index = Bone.Key + 1; Index < Ref.GetNum() && bEnd; ++Index) bEnd = Ref.GetParentIndex(Index) != Bone.Key || Branches.Contains(Index);
        if (!bEnd) continue;
        const FTransform Transform = GratiaKawaiiRefTransform(Ref, Bone.Key);
        Points.Add(Transform.GetLocation() + Transform.GetRotation().RotateVector(GratiaAnimAxisVector(Definition.ForwardAxis)) * Definition.TipLengthCm);
        Reach = FMath::Max(Reach, Bone.Value + Definition.TipLengthCm);
    }
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
    TArray<FGratiaJiggle> Jiggles;
    float JiggleDelta = 0.0f;
    bool bSoftBody = false;
    bool bSoftBodyReset = false;
    TArray<FGratiaSoftBodyGrab> Grabs;
    TArray<TPair<FName, FVector>> Scales;
    float SoftPushFraction = 1.0f;
    uint8 SpringGroups = 0;
    int32* ActiveChainsOut = nullptr;
    int32* ActiveSpringsOut = nullptr;

    FGratiaKawaiiChain& AddKawaiiChain(const FReferenceSkeleton& Ref, const TArray<FName>& Roots, EGratiaBoneAxis Axis, float DummyCm,
        float Damping, float Stiffness, float WorldLocation, float WorldRotation, float Radius, float LimitAngle, float GravityScale)
    {
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
        Node.DummyBoneLength = DummyCm;
        Node.BoneForwardAxis = ToKawaiiAxis(Axis);
        Node.PhysicsSettings.Damping = Damping;
        Node.PhysicsSettings.Stiffness = Stiffness;
        Node.PhysicsSettings.WorldDampingLocation = WorldLocation;
        Node.PhysicsSettings.WorldDampingRotation = WorldRotation;
        Node.PhysicsSettings.Radius = Radius;
        Node.PhysicsSettings.LimitAngle = LimitAngle;
        // The authored rest shape already includes gravity. The node gets only the change
        // of gravity in the chain parent's frame (set every update), so standing in the
        // reference orientation adds no sag that would push skin through clothing.
        Node.Gravity = FVector::ZeroVector;
        Node.bUseWorldSpaceGravity = false;
        Chain.GravityCmPerSecond2 = 980.0f * GravityScale;
        Chain.GravityParent = Ref.GetParentIndex(Ref.FindBoneIndex(Roots[0]));
        if (Chain.GravityParent != INDEX_NONE) Chain.GravityParentRef = GratiaKawaiiRefTransform(Ref, Chain.GravityParent).GetRotation();
        Node.TargetFramerate = 90;
        Node.bUpdatePhysicsSettingsInGame = true;
        return Chain;
    }

    void AddHandLimits(FGratiaKawaiiChain& Chain, const FReferenceSkeleton& Ref, UAnimInstance* InInstance)
    {
        // Hand/finger slots are driven from the root bone and moved every update.
        FGratiaKawaiiNode& Node = *Chain.Node;
        Chain.FirstHandLimit = Node.SphericalLimits.Num();
        for (int32 I = 0; I < (Chain.bSpring ? GratiaKawaiiSpringHandSpheres : GratiaKawaiiMaxHandSpheres); ++I)
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

    /** Hair/decor collide with the body surface capsules within their reach. A capsule the rest
     *  pose already lies in (hair on the scalp, a tie on the chest) shrinks to the rest pose, or is
     *  left out if it would lose more than half its radius: the pose itself is never pushed. */
    void AddSpringChain(const UGratiaCharacterProfile* Profile, const FReferenceSkeleton& Ref, const FGratiaSpringChain& Definition, UAnimInstance* InInstance)
    {
        TArray<FName> Roots;
        for (const FName Bone : Definition.RootBones) if (Ref.FindBoneIndex(Bone) != INDEX_NONE) Roots.Add(Bone);
        if (Roots.IsEmpty()) return;
        FGratiaKawaiiChain& Chain = AddKawaiiChain(Ref, Roots, Definition.ForwardAxis, Definition.TipLengthCm, Definition.Damping, Definition.Stiffness,
            Definition.WorldDampingLocation, Definition.WorldDampingRotation, Definition.CollisionRadiusCm, Definition.LimitAngleDegrees, Definition.GravityScale);
        Chain.bSpring = true;
        Chain.Group = Definition.Group;
        const TArray<int32> Branches = GratiaSpringBranches(Ref, *Profile, Definition);
        for (const int32 Branch : Branches) Chain.Node->ExcludeBones.Add(FBoneReference(Ref.GetBoneName(Branch)));
        TArray<FVector> Points;
        float Reach = 0.0f;
        GratiaSpringRestPoints(Ref, Definition, Branches, Points, Reach);
        const FVector RootPoint = GratiaKawaiiRefTransform(Ref, Ref.FindBoneIndex(Roots[0])).GetLocation();
        for (const FGratiaSurfaceCapsule& Capsule : Profile->BodySurface)
        {
            const int32 Bone = Ref.FindBoneIndex(Capsule.Bone);
            if (Bone == INDEX_NONE || Points.IsEmpty()) continue;
            const FTransform BoneRef = GratiaKawaiiRefTransform(Ref, Bone);
            const FVector A = BoneRef.TransformPosition(Capsule.StartCm), B = BoneRef.TransformPosition(Capsule.EndCm);
            float Radius = Capsule.RadiusCm;
            // Simplified body: thick parts only (no hands/forearms), within the chain's reach.
            if (Radius < Profile->SpringMinColliderRadiusCm) continue;
            if (FMath::PointDistToSegment(RootPoint, A, B) - Radius > Reach + Definition.CollisionRadiusCm) continue;
            float Gap = FLT_MAX;
            for (const FVector& Point : Points) Gap = FMath::Min(Gap, float(FMath::PointDistToSegment(Point, A, B)) - Definition.CollisionRadiusCm);
            if (Gap < Radius)
            {
                if (Gap < 0.5f * Radius) continue;
                Radius = Gap;
            }
            const FVector Start = Capsule.StartCm, End = Capsule.EndCm;
            const double Length = FVector::Distance(A, B);
            if (Length < 0.1)
            {
                FSphericalLimit Limit;
                Limit.DrivingBone = FBoneReference(Capsule.Bone);
                Limit.OffsetLocation = Start;
                Limit.Radius = Radius;
                Limit.LimitType = ESphericalLimitType::Outer;
                Chain.Node->SphericalLimits.Add(Limit);
                continue;
            }
            FCapsuleLimit Limit;
            Limit.DrivingBone = FBoneReference(Capsule.Bone);
            Limit.OffsetLocation = (Start + End) * 0.5;
            Limit.OffsetRotation = FQuat::FindBetweenNormals(FVector::ZAxisVector, (End - Start).GetSafeNormal()).Rotator();
            Limit.Radius = Radius;
            Limit.Length = float(Length);
            Chain.Node->CapsuleLimits.Add(Limit);
        }
        AddHandLimits(Chain, Ref, InInstance);
    }

    void BuildSoftBody(const UGratiaCharacterProfile* Profile, const USkeletalMesh* Mesh, UAnimInstance* InInstance)
    {
        Kawaii.Reset();
        Jiggles.Reset();
        if (!Profile || !Mesh) return;
        const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
        // Parents before branches: a strand hanging off a ponytail starts from the simulated ponytail.
        TArray<const FGratiaSpringChain*> Ordered;
        for (const FGratiaSpringChain& Definition : Profile->SpringChains) Ordered.Add(&Definition);
        auto FirstRoot = [&Ref](const FGratiaSpringChain& Definition) { return Definition.RootBones.IsEmpty() ? INDEX_NONE : Ref.FindBoneIndex(Definition.RootBones[0]); };
        Ordered.StableSort([&FirstRoot](const FGratiaSpringChain& A, const FGratiaSpringChain& B) { return FirstRoot(A) < FirstRoot(B); });
        for (const FGratiaSpringChain* Definition : Ordered) AddSpringChain(Profile, Ref, *Definition, InInstance);
        if (!Profile->SoftBody.bEnabled) return;
        for (const FGratiaSoftBodyChain& Definition : Profile->SoftBody.Chains)
        {
            TArray<FName> Roots;
            for (const FName Bone : Definition.RootBones) if (Ref.FindBoneIndex(Bone) != INDEX_NONE) Roots.Add(Bone);
            if (Roots.IsEmpty()) continue;
            if (Definition.bTranslational)
            {
                for (const FName Bone : Roots)
                {
                    FGratiaJiggle& Jiggle = Jiggles.AddDefaulted_GetRef();
                    Jiggle.Bone = Bone;
                    Jiggle.TipAxis = GratiaAnimAxisVector(Definition.ForwardAxis);
                    Jiggle.TipLengthCm = Definition.DummyBoneLengthCm;
                    Jiggle.Omega = 2.0f * PI * FMath::Clamp(Definition.JiggleFrequencyHz, 0.5f, 10.0f);
                    Jiggle.Zeta = FMath::Clamp(Definition.JiggleDampingRatio, 0.02f, 1.0f);
                    Jiggle.MaxCm = FMath::Max(0.0f, Definition.MaxJiggleCm);
                }
                continue;
            }
            FGratiaKawaiiChain& Chain = AddKawaiiChain(Ref, Roots, Definition.ForwardAxis, Definition.DummyBoneLengthCm, Definition.Damping,
                Definition.Stiffness, Definition.WorldDampingLocation, Definition.WorldDampingRotation, Definition.CollisionRadiusCm,
                Definition.LimitAngleDegrees, Definition.GravityScale);
            FGratiaKawaiiNode& Node = *Chain.Node;
            // Source body collision follows its skinning bones. A chain inside its own flesh (a
            // limb: its rest tip is inside a collider) ignores the spheres along its bone, or they
            // would hold it in place; a breast or butt keeps every sphere.
            const FTransform RootRef = GratiaKawaiiRefTransform(Ref, Ref.FindBoneIndex(Roots[0]));
            const FVector RootPoint = RootRef.GetLocation();
            const FVector TipPoint = RootPoint + RootRef.GetRotation().RotateVector(GratiaAnimAxisVector(Definition.ForwardAxis)) * Definition.DummyBoneLengthCm;
            const bool bInsideLimb = Profile->SoftBody.BodyColliders.ContainsByPredicate([&](const FGratiaBodyColliderSphere& Sphere)
                { return FVector::Distance(Sphere.RefCenterCm, TipPoint) < Sphere.RadiusCm; });
            for (const FGratiaBodyColliderSphere& Sphere : Profile->SoftBody.BodyColliders)
            {
                const int32 Bone = Ref.FindBoneIndex(Sphere.Bone);
                if (Bone == INDEX_NONE) continue;
                if (bInsideLimb && FMath::PointDistToSegment(Sphere.RefCenterCm, RootPoint, TipPoint) < Sphere.RadiusCm + 4.0f) continue;
                FSphericalLimit Limit;
                Limit.DrivingBone = FBoneReference(Sphere.Bone);
                Limit.OffsetLocation = GratiaKawaiiRefTransform(Ref, Bone).InverseTransformPosition(Sphere.RefCenterCm);
                Limit.Radius = Sphere.RadiusCm;
                Limit.LimitType = ESphericalLimitType::Outer;
                Node.SphericalLimits.Add(Limit);
            }
            AddHandLimits(Chain, Ref, InInstance);
        }
    }

    virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override
    {
        FAnimSingleNodeInstanceProxy::UpdateAnimationNode(InContext);
        const uint16 Serial = GetRequiredBones().GetSerialNumber();
        for (FGratiaKawaiiChain& Chain : Kawaii)
        {
            const bool bRun = Chain.bSpring ? (SpringGroups >> Chain.Group & 1) != 0 : bSoftBody;
            const bool bStart = bRun && !Chain.bRunning;
            Chain.bRunning = bRun;
            if (!bRun) continue;
            if (!Chain.bInitialized || Chain.BoneSerial != Serial)
            {
                FAnimationInitializeContext Init(this);
                Chain.Node->Initialize_AnyThread(Init);
                FAnimationCacheBonesContext Cache(this);
                Chain.Node->CacheBones_AnyThread(Cache);
                Chain.bInitialized = true; Chain.BoneSerial = Serial;
            }
            // A group switched on starts from the pose (no swing from where it was left).
            if (bStart || (bSoftBodyReset && !Chain.bSpring)) Chain.Node->ResetDynamics(ETeleportType::ResetPhysics);
            Chain.Node->Update_AnyThread(InContext);
        }
        if (bSoftBodyReset || !bSoftBody) for (FGratiaJiggle& Jiggle : Jiggles) Jiggle.bInitialized = false;
        bSoftBodyReset = false;
    }

    void EvaluateJiggles(FComponentSpacePoseContext& ComponentPose, int32& Active)
    {
        const FBoneContainer& Bones = ComponentPose.Pose.GetPose().GetBoneContainer();
        const FTransform ToWorld = GetComponentTransform();
        const double Scale = ToWorld.GetScale3D().GetAbsMax();
        const float Time = FMath::Clamp(JiggleDelta, 0.0f, 0.05f);
        for (FGratiaJiggle& Jiggle : Jiggles)
        {
            const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Jiggle.Bone);
            if (MeshIndex == INDEX_NONE) continue;
            const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
            if (Index.GetInt() == INDEX_NONE) continue;
            FTransform Bone = ComponentPose.Pose.GetComponentSpaceTransform(Index);
            const FVector Anchor = ToWorld.TransformPosition(Bone.GetLocation());
            // A grab pulls the part's tip toward the hand (the mass follows, within the stretch).
            FVector Pull = FVector::ZeroVector;
            for (const FGratiaSoftBodyGrab& Grab : Grabs)
            {
                if (Grab.RootBone != Jiggle.Bone) continue;
                const FVector Tip = Bone.GetLocation() + Bone.GetRotation().RotateVector(Jiggle.TipAxis) * Jiggle.TipLengthCm;
                Pull = ToWorld.TransformVector((Grab.TargetCS - Tip).GetClampedToMaxSize(Grab.MaxStretchCm) * FMath::Clamp(Grab.Movement, 0.0f, 1.0f));
            }
            if (!Jiggle.bInitialized || Anchor.ContainsNaN() || FVector::Distance(Anchor, Jiggle.Mass) > 50.0 * Scale)
            {
                Jiggle.Mass = Anchor; Jiggle.Velocity = FVector::ZeroVector; Jiggle.bInitialized = true;
            }
            // Damped spring toward the anchor, a few substeps per frame.
            const FVector Rest = Anchor + Pull;
            for (int32 Step = 0; Step < 4 && Time > 0.0f; ++Step)
            {
                const float Dt = Time / 4.0f;
                Jiggle.Velocity += (FMath::Square(Jiggle.Omega) * (Rest - Jiggle.Mass) - 2.0f * Jiggle.Zeta * Jiggle.Omega * Jiggle.Velocity) * Dt;
                Jiggle.Mass += Jiggle.Velocity * Dt;
            }
            FVector Offset = (Jiggle.Mass - Anchor).GetClampedToMaxSize((Jiggle.MaxCm + Pull.Size() / Scale) * Scale);
            if (Offset.ContainsNaN()) { Jiggle.bInitialized = false; continue; }
            Jiggle.Mass = Anchor + Offset;
            Bone.SetLocation(Bone.GetLocation() + ToWorld.InverseTransformVector(Offset));
            ComponentPose.Pose.SetComponentSpaceTransform(Index, Bone);
            ++Active;
        }
    }

    void EvaluateSoftBody(FPoseContext& Output)
    {
        int32 Active = 0, SpringCounts[5] = {};
        ON_SCOPE_EXIT
        {
            if (ActiveChainsOut) *ActiveChainsOut = Active;
            if (ActiveSpringsOut) for (int32 Group = 0; Group < 5; ++Group) ActiveSpringsOut[Group] = SpringCounts[Group];
        };
        const bool bAnyChain = Kawaii.ContainsByPredicate([](const FGratiaKawaiiChain& Chain) { return Chain.bRunning && Chain.bInitialized; });
        if (!bAnyChain && (!bSoftBody || Jiggles.IsEmpty())) return;
        FComponentSpacePoseContext ComponentPose(this);
        ComponentPose.Pose.InitPose(Output.Pose);
        ComponentPose.Curve = Output.Curve;
        for (FGratiaKawaiiChain& Chain : Kawaii)
        {
            if (!Chain.bRunning || !Chain.bInitialized) continue;
            if (Chain.bSpring)
            {
                // A grabbed spring bone moves toward the hand; its chain keeps the bone lengths.
                for (const FGratiaSoftBodyGrab& Grab : Grabs)
                    for (FKawaiiPhysicsModifyBone& Bone : Chain.Node->ModifyBones)
                    {
                        if (Bone.bDummy || Bone.ParentIndex < 0 || Bone.BoneRef.BoneName != Grab.RootBone) continue;
                        const FVector Desired = Bone.PoseLocation + (Grab.TargetCS - Bone.PoseLocation).GetClampedToMaxSize(Grab.MaxStretchCm);
                        const FVector Shift = (Desired - Bone.Location) * FMath::Clamp(Grab.Movement, 0.0f, 1.0f);
                        if (Shift.ContainsNaN()) continue;
                        Bone.Location += Shift; Bone.PrevLocation += Shift;
                    }
                Chain.Node->EvaluateComponentSpace_AnyThread(ComponentPose);
                ++SpringCounts[FMath::Min<int32>(Chain.Group, 4)];
                continue;
            }
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
        if (bSoftBody) EvaluateJiggles(ComponentPose, Active);
        FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(ComponentPose.Pose, Output.Pose);
        if (!bSoftBody) return;
        // Squeeze: scale the simulated root bone in its own space (skin compresses toward the body).
        const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
        for (const TPair<FName, FVector>& Scale : Scales)
        {
            const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Scale.Key);
            if (MeshIndex == INDEX_NONE || Scale.Value.ContainsNaN()) continue;
            const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
            if (Index.GetInt() == INDEX_NONE) continue;
            Output.Pose[Index].SetScale3D(Output.Pose[Index].GetScale3D() * Scale.Value);
        }
    }

    virtual void PreUpdate(UAnimInstance* InInstance, float DeltaSeconds) override
    {
        FAnimSingleNodeInstanceProxy::PreUpdate(InInstance, DeltaSeconds);
        UGratiaAnimInstance* Instance = CastChecked<UGratiaAnimInstance>(InInstance);
        ActiveChainsOut = &Instance->ActiveSoftBodyChains;
        ActiveSpringsOut = Instance->ActiveSpringChains;
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
        bSoftBody = Instance->bSoftBody && Profile && Mesh && Profile->SoftBody.bEnabled;
        SpringGroups = Profile && Mesh ? Instance->SpringGroups : 0;
        bSoftBodyReset |= Instance->SoftBodyInput.bReset;
        JiggleDelta = DeltaSeconds;
        Instance->SoftBodyInput.bReset = false;
        Grabs = Instance->SoftBodyInput.Grabs;
        Scales = Instance->SoftBodyInput.Scales;
        SoftPushFraction = FMath::Clamp(Instance->SoftBodyInput.SoftPushFraction, 0.0f, 1.0f);
        if ((bSoftBody || SpringGroups) && GetSkelMeshComponent() && !GetSkelMeshComponent()->GetComponentSpaceTransforms().IsEmpty())
        {
            const TArray<FTransform>& Pose = GetSkelMeshComponent()->GetComponentSpaceTransforms();
            const FTransform Root = Pose[0];
            const FVector Down = GetSkelMeshComponent()->GetComponentTransform().InverseTransformVectorNoScale(FVector(0, 0, -1));
            for (FGratiaKawaiiChain& Chain : Kawaii)
            {
                // Component-space gravity minus the gravity already present in the authored pose
                // (reference: upright component, parent bone at its reference rotation).
                FVector Gravity = FVector::ZeroVector;
                if (Chain.GravityCmPerSecond2 > 0.0f && Pose.IsValidIndex(Chain.GravityParent))
                {
                    const FQuat Delta = Pose[Chain.GravityParent].GetRotation() * Chain.GravityParentRef.Inverse();
                    Gravity = (Down - Delta.RotateVector(FVector(0, 0, -1))) * Chain.GravityCmPerSecond2;
                }
                Chain.Node->Gravity = Gravity.ContainsNaN() ? FVector::ZeroVector : Gravity;
            }
            for (FGratiaKawaiiChain& Chain : Kawaii)
            {
                const TArray<FVector4>& Spheres = Chain.bSpring ? Instance->SoftBodyInput.SpringHandSpheres : Instance->SoftBodyInput.HandSpheres;
                const int32 Slots = Chain.bSpring ? GratiaKawaiiSpringHandSpheres : GratiaKawaiiMaxHandSpheres;
                for (int32 I = 0; I < Slots; ++I)
                {
                    FSphericalLimit& Limit = Chain.Node->SphericalLimits[Chain.FirstHandLimit + I];
                    const bool bActive = Spheres.IsValidIndex(I);
                    const FVector4 Sphere = bActive ? Spheres[I] : FVector4(0, 0, -1.0e6, 0);
                    Limit.OffsetLocation = Root.InverseTransformPosition(FVector(Sphere.X, Sphere.Y, Sphere.Z));
                    Limit.Radius = bActive ? float(Sphere.W) * (Chain.bSpring ? 1.0f : SoftPushFraction) : 0.0f;
                }
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
        ReactionClip = nullptr; LastReactionSerial = 0; SpringGroups = 0; return;
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
        ReactionClip = Character->GetReactionAnimation(Interaction->LastReactionZoneName, Interaction->LastReactionSpeed, Interaction->Mood);
        ReactionClipDuration = ReactionClip ? ReactionClip->GetPlayLength() : 0.0f;
        ReactionTime = 0.0f;
    }
    if (Interaction->ReactionSerial == 0) LastReactionSerial = 0;
    ReactionTime = FMath::Min(ReactionClip ? ReactionClip->GetPlayLength() : 2.0f, ReactionTime + FMath::Clamp(DeltaSeconds, 0.0f, 0.05f));
    Reaction = Interaction->Reaction; Impulse = Interaction->Impulse;
    bBody = Snapshot.bBodyMotion; bHair = Snapshot.bHairMotion; bCloth = Snapshot.bClothMotion; bSpring = Snapshot.bLocalSpring;
    bEars = Snapshot.bEarMotion;
    // Spring chains follow the menu groups and the physics switch, also during performances.
    const bool bSpringPhysics = Profile->Capabilities.bSecondaryPhysics && Interaction->bPhysicalMotion && !Profile->SpringChains.IsEmpty();
    SpringGroups = bSpringPhysics ? uint8((bHair ? 1 << 1 : 0) | (bCloth ? 1 << 2 : 0) | (bEars ? 1 << 4 : 0)) : 0;
    const float PreviousYaw = HeadYaw;
    HeadYaw = FMath::FInterpTo(HeadYaw, bProcedural ? Snapshot.TargetHeadYaw : 0.0f, FMath::Min(DeltaSeconds, 0.05f), Profile->GazeInterpSpeed);
    HeadVelocity = DeltaSeconds > 0.001f ? FMath::Clamp((HeadYaw - PreviousYaw) / DeltaSeconds, -120.0f, 120.0f) : 0.0f;
    HeadPitch = FMath::FInterpTo(HeadPitch, bProcedural ? Snapshot.TargetHeadPitch : 0.0f, FMath::Min(DeltaSeconds, 0.05f), Profile->GazeInterpSpeed);
}
