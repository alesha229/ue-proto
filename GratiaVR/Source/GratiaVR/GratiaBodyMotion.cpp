#include "GratiaBodyMotion.h"
#include "GratiaAnimInstance.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaStage1Runtime.h"

#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MotionControllerComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaBodyMotion, Log, All);

using namespace GratiaBodyMotionMath;

namespace
{
    FGratiaArchetypeTuning MakeArchetype(const TCHAR* Name, const TCHAR* Label)
    {
        FGratiaArchetypeTuning Tuning;
        Tuning.Name = Name;
        Tuning.Label = FText::FromString(Label);
        return Tuning;
    }

    FTransform RefComponentTransform(const FReferenceSkeleton& Ref, int32 Index)
    {
        FTransform Result = FTransform::Identity;
        for (int32 Bone = Index; Bone != INDEX_NONE; Bone = Ref.GetParentIndex(Bone)) Result = Result * Ref.GetRefBonePose()[Bone];
        return Result;
    }

    const FGratiaArchetypeTuning& NeutralArchetype()
    {
        static const FGratiaArchetypeTuning Neutral;
        return Neutral;
    }

    /** QA and capture runs measure the authored motion; the layer joins them only with -GratiaBodyMotion. */
    bool IsDisabledByCommandLine()
    {
        const TCHAR* Command = FCommandLine::Get();
        if (FParse::Param(Command, TEXT("GratiaNoBodyMotion"))) return true;
        if (FParse::Param(Command, TEXT("GratiaBodyMotion"))) return false;
        for (const TCHAR* QA : {TEXT("GratiaSelfTest"), TEXT("GratiaSmokeTest"), TEXT("GratiaSoftBodyQA"), TEXT("GratiaFlowQA"),
                 TEXT("GratiaChannelShots"), TEXT("GratiaMenuShots")})
            if (FParse::Param(Command, QA)) return true;
        return false;
    }
}

FGratiaBodyMotionSettings::FGratiaBodyMotionSettings()
{
    // Mood 0 (was "calm"): cold, muted until strong stimulation breaks the composure.
    FGratiaArchetypeTuning Kuudere = MakeArchetype(TEXT("Kuudere"), TEXT("Кудере"));
    Kuudere.ExcitementGain = 0.08f; Kuudere.ExcitementDecay = 0.05f;
    Kuudere.TensionScale = 0.6f; Kuudere.DodgeScale = 0.3f; Kuudere.LeanTowardScale = 0.4f;
    Kuudere.LivelinessScale = 0.6f; Kuudere.BreakThreshold = 0.7f; Kuudere.MutedResponse = 0.35f;
    Kuudere.GazeAversion = 0.1f; Kuudere.Smile = 0.05f;
    // Mood 1 (was "cheerful"): eager, leans in, takes the initiative.
    FGratiaArchetypeTuning Deredere = MakeArchetype(TEXT("Deredere"), TEXT("Дередере"));
    Deredere.ExcitementGain = 0.16f; Deredere.ExcitementDecay = 0.035f;
    Deredere.TensionScale = 1.1f; Deredere.DodgeScale = 0.15f; Deredere.LeanTowardScale = 1.4f;
    Deredere.InitiativeLeanDegrees = 4.0f; Deredere.LivelinessScale = 1.3f;
    Deredere.GazeAversion = 0.05f; Deredere.Smile = 0.8f;
    // Mood 2 (was "reserved"): resists, turns away and pouts, melts with gentle sustained contact.
    FGratiaArchetypeTuning Tsundere = MakeArchetype(TEXT("Tsundere"), TEXT("Цундере"));
    Tsundere.ExcitementGain = 0.12f; Tsundere.ExcitementDecay = 0.04f;
    Tsundere.TensionScale = 1.0f; Tsundere.DodgeScale = 1.6f; Tsundere.LeanTowardScale = 0.7f;
    Tsundere.TurnAwayDegrees = 12.0f; Tsundere.ThawPerSecond = 0.035f; Tsundere.ThawDecayPerSecond = 0.008f;
    Tsundere.GazeAversion = 0.75f; Tsundere.Pout = 0.7f; Tsundere.Smile = 0.15f; Tsundere.PushAway = 0.6f;
    Archetypes = {Kuudere, Deredere, Tsundere};
}

UGratiaBodyMotion::UGratiaBodyMotion()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
    Random.Initialize(0x6A7B3C1D);
}

UGratiaBodyMotion* UGratiaBodyMotion::Find(const AActor* Actor)
{
    return Actor ? Actor->FindComponentByClass<UGratiaBodyMotion>() : nullptr;
}

UGratiaBodyMotion* UGratiaBodyMotion::Ensure(AGratiaPreviewCharacter* InCharacter)
{
    if (!InCharacter) return nullptr;
    if (UGratiaBodyMotion* Existing = Find(InCharacter)) return Existing;
    UGratiaBodyMotion* Component = NewObject<UGratiaBodyMotion>(InCharacter, TEXT("BodyMotion"));
    InCharacter->AddInstanceComponent(Component);
    Component->RegisterComponent();
    return Component;
}

void UGratiaBodyMotion::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    bQADisabled = IsDisabledByCommandLine();
    Random.Initialize(int32(GetTypeHash(GetOwner() ? GetOwner()->GetName() : FString())) ^ 0x51ED27);
    // Publish before the mesh evaluates: no one-frame lag between the inputs and the pose.
    if (Character.IsValid() && Character->CharacterMesh) Character->CharacterMesh->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
    ResetState();
    UE_LOG(LogGratiaBodyMotion, Display, TEXT("Body motion on %s%s"), *GetNameSafe(GetOwner()), bQADisabled ? TEXT(" disabled for this QA run") : TEXT(""));
}

void UGratiaBodyMotion::EndPlay(const EEndPlayReason::Type Reason)
{
    Publish(false);
    ApplyPlayRate(false);
    Super::EndPlay(Reason);
}

void UGratiaBodyMotion::AddExcitement(float Amount)
{
    if (FMath::IsFinite(Amount)) PendingExcitement = FMath::Clamp(PendingExcitement + Amount, -1.0f, 1.0f);
}

void UGratiaBodyMotion::ResetState()
{
    Excitement = 0.0f; Stamina = 1.0f; PendingExcitement = 0.0f; ContactSpeed = 0.0f;
    Breath = FBreathState();
    TensionSpring.Reset(); LeanForward.Reset(); LeanRight.Reset(); TurnSpring.Reset();
    Active[0] = Active[1] = FActiveFragment();
    FragmentGap = Random.FRandRange(3.0f, 6.0f);
    bComposureBroken = false;
    Thaw = 0.0f;
    LastHand.Reset();
    CurrentFragment.Reset();
}

const FGratiaArchetypeTuning& UGratiaBodyMotion::GetTuning(const FGratiaBodyMotionSettings& S) const
{
    return S.Archetypes.IsValidIndex(Archetype) ? S.Archetypes[Archetype] : NeutralArchetype();
}

FText UGratiaBodyMotion::GetArchetypeLabel(int32 Mood) const
{
    const AGratiaPreviewCharacter* Owner = Character.Get();
    const UGratiaCharacterProfile* Profile = Owner ? Owner->CharacterProfile.Get() : nullptr;
    if (Profile && Profile->BodyMotion.Archetypes.IsValidIndex(Mood)) return Profile->BodyMotion.Archetypes[Mood].Label;
    return FText::AsNumber(Mood);
}

void UGratiaBodyMotion::Rebuild(const UGratiaCharacterProfile& Profile, const USkeletalMesh& Mesh)
{
    Rig.Reset(); Spine.Reset(); Clavicles.Reset(); ClavicleDirections.Reset(); LegCompensation.Reset(); FeatureBones.Reset();
    PelvisBone = NeckBone = HeadBone = INDEX_NONE;
    MissingBones.Reset();
    const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
    Up = Profile.UpAxis.GetSafeNormal();
    Forward = Profile.ForwardAxis.GetSafeNormal();
    if (Up.IsNearlyZero()) Up = FVector::UpVector;
    if (Forward.IsNearlyZero()) Forward = FVector::RightVector;
    Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();

    auto Resolve = [&](const TCHAR* Semantic, bool bRequired) -> int32
    {
        const FName Name = Profile.ResolveBone(Semantic);
        const int32 Index = Name.IsNone() ? INDEX_NONE : Ref.FindBoneIndex(Name);
        if (Index == INDEX_NONE && bRequired) MissingBones += FString::Printf(TEXT("%s "), Semantic);
        return Index;
    };
    TMap<int32, int32> RigOf;
    auto AddRig = [&](int32 RefIndex, bool bPre) -> int32
    {
        if (const int32* Existing = RigOf.Find(RefIndex)) return *Existing;
        FRigBone Bone;
        Bone.Name = Ref.GetBoneName(RefIndex);
        Bone.RestCS = RefComponentTransform(Ref, RefIndex).GetRotation();
        Bone.bPre = bPre;
        return RigOf.Add(RefIndex, Rig.Add(Bone));
    };

    const int32 Pelvis = Resolve(TEXT("Pelvis"), true), Chest = Resolve(TEXT("Chest"), true);
    const int32 Upper = Resolve(TEXT("UpperChest"), false), Neck = Resolve(TEXT("Neck"), false), Head = Resolve(TEXT("Head"), true);
    PelvisName = Pelvis != INDEX_NONE ? Ref.GetBoneName(Pelvis) : NAME_None;
    ChestName = Chest != INDEX_NONE ? Ref.GetBoneName(Chest) : NAME_None;
    HeadName = Head != INDEX_NONE ? Ref.GetBoneName(Head) : NAME_None;
    if (Pelvis != INDEX_NONE) PelvisBone = AddRig(Pelvis, false);

    // Spine: every bone from above the pelvis up to the upper chest (model-independent chain).
    TArray<int32> Chain;
    const int32 Top = Upper != INDEX_NONE ? Upper : Chest;
    for (int32 Bone = Top; Bone != INDEX_NONE && Bone != Pelvis; Bone = Ref.GetParentIndex(Bone)) Chain.Insert(Bone, 0);
    if (Pelvis == INDEX_NONE || Chain.IsEmpty() || Ref.GetParentIndex(Chain[0]) != Pelvis)
    {
        // The chest is not under the pelvis: drive only the mapped torso bones.
        Chain.Reset();
        if (Chest != INDEX_NONE) Chain.Add(Chest);
        if (Upper != INDEX_NONE && Upper != Chest) Chain.Add(Upper);
    }
    for (const int32 Bone : Chain) Spine.Add(AddRig(Bone, false));
    if (Neck != INDEX_NONE && !Chain.Contains(Neck)) NeckBone = AddRig(Neck, false);
    if (Head != INDEX_NONE) HeadBone = AddRig(Head, false);

    // Clavicles: the parent of each upper arm when it is a shoulder bone of its own.
    for (const TCHAR* Arm : {TEXT("LeftUpperArm"), TEXT("RightUpperArm")})
    {
        const int32 ArmIndex = Resolve(Arm, false);
        const int32 Parent = ArmIndex != INDEX_NONE ? Ref.GetParentIndex(ArmIndex) : INDEX_NONE;
        if (Parent == INDEX_NONE || Chain.Contains(Parent) || Parent == Neck || Parent == Pelvis) continue;
        const FVector Direction = (RefComponentTransform(Ref, ArmIndex).GetLocation() - RefComponentTransform(Ref, Parent).GetLocation()).GetSafeNormal();
        if (Direction.IsNearlyZero()) continue;
        Clavicles.Add(AddRig(Parent, false));
        ClavicleDirections.Add(Direction);
    }
    // Legs: the pelvis child on the way to each thigh takes the inverse pelvis rotation (feet stay planted).
    for (const TCHAR* Thigh : {TEXT("LeftThigh"), TEXT("RightThigh")})
    {
        int32 Bone = Resolve(Thigh, true);
        while (Bone != INDEX_NONE && Ref.GetParentIndex(Bone) != Pelvis) Bone = Ref.GetParentIndex(Bone);
        if (Bone != INDEX_NONE && Pelvis != INDEX_NONE) LegCompensation.AddUnique(AddRig(Bone, true));
    }
    for (const TCHAR* Feature : {TEXT("Pelvis"), TEXT("LeftFoot"), TEXT("RightFoot"), TEXT("Head"), TEXT("LeftHand"), TEXT("RightHand")})
    {
        const int32 Index = Resolve(Feature, false);
        if (Index != INDEX_NONE) FeatureBones.Add(Ref.GetBoneName(Index));
    }
    UE_LOG(LogGratiaBodyMotion, Display, TEXT("Body motion rig of %s: spine %d, neck %d, clavicles %d, leg compensation %d%s%s"),
        *Profile.GetName(), Spine.Num(), NeckBone != INDEX_NONE ? 1 : 0, Clavicles.Num(), LegCompensation.Num(),
        MissingBones.IsEmpty() ? TEXT("") : TEXT(", missing: "), *MissingBones);
}

bool UGratiaBodyMotion::SampleFeatures(const UAnimSequence* Clip, float Time, TArray<FVector>& Out) const
{
    Out.Reset();
    const USkeleton* Skeleton = Clip ? Clip->GetSkeleton() : nullptr;
    if (!Skeleton) return false;
    const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
    const FAnimExtractContext Context(double(Time), false);
    for (const FName Name : FeatureBones)
    {
        const int32 Index = Ref.FindBoneIndex(Name);
        if (Index == INDEX_NONE) return false;
        FTransform Component = FTransform::Identity;
        for (int32 Bone = Index; Bone != INDEX_NONE; Bone = Ref.GetParentIndex(Bone))
        {
            FTransform Local;
            Clip->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Bone), Context, false);
            Component = Component * Local;
        }
        if (Component.ContainsNaN()) return false;
        Out.Add(Component.GetLocation());
    }
    return !Out.IsEmpty();
}

void UGratiaBodyMotion::CurrentFeatures(TArray<FVector>& Out) const
{
    Out.Reset();
    const USkeletalMeshComponent* Mesh = Character.IsValid() ? Character->CharacterMesh.Get() : nullptr;
    if (!Mesh) return;
    for (const FName Name : FeatureBones) Out.Add(Mesh->GetBoneLocation(Name, EBoneSpaces::ComponentSpace));
}

void UGratiaBodyMotion::AddFragmentsFromClip(UAnimSequence* Clip, float Start, float End, EGratiaFragmentKind Kind, float Weight, bool bCut, const FGratiaBodyMotionSettings& S)
{
    if (!Clip || FeatureBones.IsEmpty()) return;
    const float Length = Clip->GetPlayLength();
    Start = FMath::Clamp(Start, 0.0f, Length);
    End = End > 0.0f ? FMath::Clamp(End, Start, Length) : Length;
    if (End - Start < 0.5f) return;
    // Features at 10 Hz: pose per sample and motion energy between samples.
    constexpr float Rate = 10.0f;
    const int32 Count = FMath::Max(2, FMath::FloorToInt((End - Start) * Rate) + 1);
    TArray<TArray<FVector>> Poses;
    Poses.SetNum(Count);
    for (int32 I = 0; I < Count; ++I)
        if (!SampleFeatures(Clip, FMath::Min(End, Start + I / Rate), Poses[I])) return;
    TArray<float> Energy;
    Energy.SetNum(Count);
    for (int32 I = 0; I < Count; ++I) Energy[I] = PoseDistance(Poses[I], Poses[FMath::Min(I + 1, Count - 1)]) + PoseDistance(Poses[I], Poses[FMath::Max(I - 1, 0)]);
    TArray<int32> Cuts;
    if (bCut) Cuts = FindCutSamples(Energy, FMath::RoundToInt(S.AutoFragmentMinSeconds * Rate), FMath::RoundToInt(S.AutoFragmentMaxSeconds * Rate));
    else Cuts = {0, Count - 1};
    const int32 PelvisFeature = FeatureBones.IndexOfByKey(PelvisName), HeadFeature = FeatureBones.IndexOfByKey(HeadName);
    for (int32 C = 1; C < Cuts.Num(); ++C)
    {
        FFragment Fragment;
        Fragment.Clip = Clip;
        Fragment.Start = FMath::Min(End, Start + Cuts[C - 1] / Rate);
        Fragment.End = FMath::Min(End, Start + Cuts[C] / Rate);
        Fragment.Weight = FMath::Max(0.0f, Weight);
        Fragment.Entry = Poses[Cuts[C - 1]];
        Fragment.Exit = Poses[Cuts[C]];
        Fragment.Kind = Kind;
        if (Kind == EGratiaFragmentKind::Any && PelvisFeature != INDEX_NONE && HeadFeature != INDEX_NONE)
        {
            // Classify by what the body does inside the fragment.
            float MaxHandOverHead = -FLT_MAX, MinLean = FLT_MAX, MaxLean = -FLT_MAX, MinSide = FLT_MAX, MaxSide = -FLT_MAX;
            for (int32 I = Cuts[C - 1]; I <= Cuts[C]; ++I)
            {
                const TArray<FVector>& P = Poses[I];
                const float HeadHeight = FVector::DotProduct(P[HeadFeature], Up);
                for (int32 F = 0; F < P.Num(); ++F)
                    if (FeatureBones[F] != PelvisName && FeatureBones[F] != HeadName && FVector::DotProduct(P[F], Up) > FVector::DotProduct(P[PelvisFeature], Up) + 20.0)
                        MaxHandOverHead = FMath::Max(MaxHandOverHead, float(FVector::DotProduct(P[F], Up)) - HeadHeight);
                const float Lean = FVector::DotProduct(P[HeadFeature] - P[PelvisFeature], Forward);
                const float Side = FVector::DotProduct(P[PelvisFeature], Right);
                MinLean = FMath::Min(MinLean, Lean); MaxLean = FMath::Max(MaxLean, Lean);
                MinSide = FMath::Min(MinSide, Side); MaxSide = FMath::Max(MaxSide, Side);
            }
            Fragment.Kind = MaxHandOverHead > 0.0f ? EGratiaFragmentKind::Stretch
                : MaxLean - MinLean > 6.0f ? EGratiaFragmentKind::Lean
                : MaxSide - MinSide > 3.0f ? EGratiaFragmentKind::WeightShift : EGratiaFragmentKind::Any;
        }
        Fragments.Add(MoveTemp(Fragment));
    }
}

void UGratiaBodyMotion::BuildFragments(const UGratiaCharacterProfile& Profile, const USkeletalMesh& Mesh)
{
    Fragments.Reset();
    Active[0] = Active[1] = FActiveFragment();
    const FGratiaBodyMotionSettings& S = Profile.BodyMotion;
    if (!S.bFragments || FeatureBones.IsEmpty()) { FragmentCount = 0; return; }
    const double StartTime = FPlatformTime::Seconds();
    auto Usable = [&Mesh](const UAnimSequence* Clip) { return Clip && Clip->GetSkeleton() == Mesh.GetSkeleton(); };
    if (!S.Fragments.IsEmpty())
    {
        for (const FGratiaMotionFragment& Fragment : S.Fragments)
            if (Usable(Fragment.Clip)) AddFragmentsFromClip(Fragment.Clip, Fragment.StartSeconds, Fragment.EndSeconds, Fragment.Kind, Fragment.Weight, false, S);
    }
    else
    {
        // Automatic database: the free-play stance clips and the authored idle variations, cut at calm frames.
        TArray<UAnimSequence*> Sources;
        for (UAnimSequence* Clip : {Profile.Idle.Get(), Profile.Arms.Get(), Profile.Head.Get()}) if (Usable(Clip)) Sources.AddUnique(Clip);
        const AGratiaPreviewCharacter* Owner = Character.Get();
        for (int32 I = 0; Owner && I < Profile.PerformanceClips.Num(); ++I)
            if (Owner->IsStance(I) && Usable(Profile.PerformanceClips[I].Clip)) Sources.AddUnique(Profile.PerformanceClips[I].Clip);
        for (UAnimSequence* Clip : Sources) AddFragmentsFromClip(Clip, 0.0f, 0.0f, EGratiaFragmentKind::Any, 1.0f, true, S);
    }
    FragmentCount = Fragments.Num();
    int32 Kinds[4] = {};
    for (const FFragment& Fragment : Fragments) ++Kinds[FMath::Min<int32>(int32(Fragment.Kind), 3)];
    UE_LOG(LogGratiaBodyMotion, Display, TEXT("Motion fragments: %d (any %d, stretch %d, lean %d, weight shift %d) in %.0f ms"),
        Fragments.Num(), Kinds[0], Kinds[1], Kinds[2], Kinds[3], (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

UGratiaBodyMotion::FHandSense UGratiaBodyMotion::SenseHands(float DeltaSeconds)
{
    FHandSense Result;
    AGratiaPreviewCharacter* Owner = Character.Get();
    USkeletalMeshComponent* Mesh = Owner ? Owner->CharacterMesh.Get() : nullptr;
    if (!Mesh || !GetWorld()) return Result;
    if (!Runtime.IsValid())
        for (TActorIterator<AGratiaStage1Runtime> It(GetWorld()); It; ++It) { Runtime = *It; break; }
    if (!Runtime.IsValid()) return Result;
    TArray<FVector, TInlineAllocator<3>> BodyPoints;
    for (const FName Bone : {ChestName, PelvisName, HeadName})
        if (!Bone.IsNone() && Mesh->GetBoneIndex(Bone) != INDEX_NONE) BodyPoints.Add(Mesh->GetSocketLocation(Bone));
    if (BodyPoints.IsEmpty()) return Result;
    const double Scale = FMath::Max(0.01, Mesh->GetComponentScale().GetAbsMax());
    APawn* Pawn = Runtime->GetPlayerPawn();
    if (!Pawn) return Result;
    TArray<UMotionControllerComponent*> Controllers;
    Pawn->GetComponents(Controllers);
    for (UMotionControllerComponent* Controller : Controllers)
    {
        if (!Controller || !Controller->IsTracked()) continue;
        const bool bLeft = Controller->MotionSource.ToString().Contains(TEXT("Left"));
        if (!Runtime->IsHandInteractionAllowed(bLeft)) continue;
        const FVector Position = Controller->GetComponentLocation();
        const FVector* Last = LastHand.Find(Controller);
        const FVector Velocity = Last && DeltaSeconds > 1.0e-4f ? ((Position - *Last) / DeltaSeconds).GetClampedToMaxSize(1000.0 * Scale) : FVector::ZeroVector;
        LastHand.Add(Controller, Position);
        for (const FVector& Point : BodyPoints)
        {
            const FVector Offset = Position - Point;
            const double Distance = Offset.Size();
            if (Distance < 1.0e-3 || Distance / Scale >= Result.DistanceCm) continue;
            const FVector Direction = Offset / Distance;
            Result.bValid = true;
            Result.DistanceCm = float(Distance / Scale);
            Result.ClosingSpeed = float(-FVector::DotProduct(Velocity, Direction) / Scale);
            Result.Speed = float(Velocity.Size() / Scale);
            Result.DirectionCS = Mesh->GetComponentTransform().InverseTransformVectorNoScale(Direction);
        }
    }
    return Result;
}

int32 UGratiaBodyMotion::PickFragment(const TArray<FVector>& Pose, float MaxError, const FVector& PlayerCS, int32 Exclude)
{
    const FVector Flat = PlayerCS - FVector::DotProduct(PlayerCS, Up) * Up;
    const float Distance = Flat.Size();
    const float Angle = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Flat, Right), FVector::DotProduct(Flat, Forward)));
    // Kinds that suit where the player is: close -> lean, beside -> weight shift, far -> stretch.
    const EGratiaFragmentKind Preferred = Distance < 80.0f ? EGratiaFragmentKind::Lean
        : FMath::Abs(Angle) > 40.0f ? EGratiaFragmentKind::WeightShift
        : Distance > 150.0f ? EGratiaFragmentKind::Stretch : EGratiaFragmentKind::Any;
    int32 Best = INDEX_NONE;
    float BestScore = FLT_MAX;
    const double Now = Seconds;
    for (int32 I = 0; I < Fragments.Num(); ++I)
    {
        const FFragment& Fragment = Fragments[I];
        if (I == Exclude || Fragment.Weight <= 0.0f) continue;
        const float Error = PoseDistance(Pose, Fragment.Entry);
        if (Error > MaxError) continue;
        float Score = Error / FMath::Max(0.1f, MaxError);
        if (Now - Fragment.LastUsed < 30.0) Score += 1.0f;
        if (Fragment.Kind == Preferred && Preferred != EGratiaFragmentKind::Any) Score -= 0.35f;
        Score -= Random.FRand() * 0.5f * Fragment.Weight;
        if (Score < BestScore) { BestScore = Score; Best = I; }
    }
    return Best;
}

void UGratiaBodyMotion::UpdateFragments(const FGratiaBodyMotionSettings& S, float DeltaSeconds, bool bAllowStart, const FVector& PlayerCS)
{
    const float Blend = FMath::Max(0.05f, S.FragmentBlendSeconds);
    const float Rate = FMath::Max(0.1f, PlayRate);
    FActiveFragment& A = Active[0];
    FActiveFragment& B = Active[1];
    for (FActiveFragment& Layer : Active)
        if (Layer.Index != INDEX_NONE && !Fragments.IsValidIndex(Layer.Index)) Layer = FActiveFragment();
    if (A.Index != INDEX_NONE)
    {
        A.Time = FMath::Min(A.Time + DeltaSeconds * Rate, Fragments[A.Index].End);
        if (B.Index != INDEX_NONE) B.Time = FMath::Min(B.Time + DeltaSeconds * Rate, Fragments[B.Index].End);
        const float Remaining = (Fragments[A.Index].End - A.Time) / Rate;
        if (!bAllowStart) A.bFadingOut = true;
        if (!A.bFadingOut && B.Index == INDEX_NONE && Remaining <= Blend)
        {
            // Seamless assembly: continue with a fragment that starts where this one ends.
            const int32 Next = Random.FRand() < S.ChainChance ? PickFragment(Fragments[A.Index].Exit, S.MaxEntryPoseErrorCm * 0.7f, PlayerCS, A.Index) : INDEX_NONE;
            if (Next != INDEX_NONE)
            {
                B.Index = Next; B.Time = Fragments[Next].Start; B.Weight = 0.0f; B.bFadingOut = false;
                Fragments[Next].LastUsed = Seconds;
            }
            else A.bFadingOut = true;
        }
        A.Weight = FMath::Clamp(A.Weight + (A.bFadingOut ? -DeltaSeconds : DeltaSeconds) / Blend, 0.0f, 1.0f);
        if (B.Index != INDEX_NONE)
        {
            if (A.bFadingOut) B = FActiveFragment();
            else
            {
                B.Weight = FMath::Min(1.0f, B.Weight + DeltaSeconds / Blend);
                if (B.Weight >= 1.0f) { A = B; B = FActiveFragment(); }
            }
        }
        if (A.bFadingOut && A.Weight <= 0.0f)
        {
            A = FActiveFragment();
            FragmentGap = Random.FRandRange(FMath::Min(S.FragmentGapSeconds.X, S.FragmentGapSeconds.Y), FMath::Max(S.FragmentGapSeconds.X, S.FragmentGapSeconds.Y));
        }
    }
    else if (bAllowStart && !Fragments.IsEmpty())
    {
        FragmentGap -= DeltaSeconds;
        if (FragmentGap <= 0.0f)
        {
            TArray<FVector> Pose;
            CurrentFeatures(Pose);
            const int32 Next = PickFragment(Pose, S.MaxEntryPoseErrorCm, PlayerCS, INDEX_NONE);
            if (Next != INDEX_NONE)
            {
                A.Index = Next; A.Time = Fragments[Next].Start; A.Weight = 0.0f; A.bFadingOut = false;
                Fragments[Next].LastUsed = Seconds;
            }
            else FragmentGap = 2.0f;
        }
    }
    for (int32 L = 0; L < 2; ++L)
    {
        const FActiveFragment& Layer = Active[L];
        FGratiaBodyMotionFrame::FLayer& Out = Frame.Layers[L];
        Out = FGratiaBodyMotionFrame::FLayer();
        if (Layer.Index == INDEX_NONE) continue;
        Out.Clip = Fragments[Layer.Index].Clip;
        Out.Time = Layer.Time;
        Out.Weight = FMath::SmoothStep(0.0f, 1.0f, Layer.Weight);
    }
    CurrentFragment = A.Index != INDEX_NONE ? FString::Printf(TEXT("%s@%.1f"), *GetNameSafe(Fragments[A.Index].Clip), Fragments[A.Index].Start) : FString();
}

void UGratiaBodyMotion::ApplyPlayRate(bool bModulate)
{
    AGratiaPreviewCharacter* Owner = Character.Get();
    UAnimSingleNodeInstance* Instance = Owner && Owner->CharacterMesh ? Owner->CharacterMesh->GetSingleNodeInstance() : nullptr;
    if (!Instance) return;
    const bool bIdle = Owner->PreviewPose == EGratiaPreviewPose::Idle;
    const bool bStance = Owner->PreviewPose == EGratiaPreviewPose::Performance && Owner->IsStance(Owner->PerformanceIndex);
    const float Base = bStance ? Owner->GetPerformanceRate() : 1.0f;
    if (bModulate && (bIdle || bStance))
    {
        if (FMath::Abs(Instance->GetPlayRate() - Base * PlayRate) > 0.002f) Instance->SetPlayRate(Base * PlayRate);
        bRateApplied = true;
    }
    else if (bRateApplied)
    {
        // Back to the authored speed; a scripted performance sets its own rate when it starts.
        if (bIdle || bStance) Instance->SetPlayRate(Base);
        bRateApplied = false;
    }
}

void UGratiaBodyMotion::Tilt(int32 Bone, const FVector& DirectionCS, float Degrees)
{
    if (!Rig.IsValidIndex(Bone) || !FMath::IsFinite(Degrees) || FMath::Abs(Degrees) < 1.0e-4f) return;
    // Rotating Up toward Direction: the bone's top tilts that way (sign-safe for any rig axes).
    const FVector Axis = FVector::CrossProduct(Up, DirectionCS);
    if (Axis.SizeSquared() < 1.0e-6) return;
    Rig[Bone].Delta = FQuat(Axis.GetSafeNormal(), FMath::DegreesToRadians(Degrees)) * Rig[Bone].Delta;
}

void UGratiaBodyMotion::Twist(int32 Bone, float Degrees)
{
    if (!Rig.IsValidIndex(Bone) || !FMath::IsFinite(Degrees) || FMath::Abs(Degrees) < 1.0e-4f) return;
    // Positive turns the front toward the character's right.
    Rig[Bone].Delta = FQuat(Up, FMath::DegreesToRadians(Degrees)) * Rig[Bone].Delta;
}

void UGratiaBodyMotion::Publish(bool bActive)
{
    Frame.Bones.Reset();
    if (bActive)
    {
        FQuat PelvisLocal = FQuat::Identity;
        for (int32 I = 0; I < Rig.Num(); ++I)
        {
            const FRigBone& Bone = Rig[I];
            if (Bone.bPre || Bone.Delta.Equals(FQuat::Identity, 1.0e-7f)) continue;
            // Component-space delta expressed in the bone's local frame (rest orientation), as the gaze does.
            const FQuat Local = (Bone.RestCS.Inverse() * Bone.Delta * Bone.RestCS).GetNormalized();
            if (Local.ContainsNaN()) continue;
            Frame.Bones.Add({Bone.Name, Local, false});
            if (I == PelvisBone) PelvisLocal = Local;
        }
        if (!PelvisLocal.Equals(FQuat::Identity, 1.0e-7f))
            for (const int32 Leg : LegCompensation) Frame.Bones.Add({Rig[Leg].Name, PelvisLocal.Inverse(), true});
    }
    else
    {
        Frame.Layers[0] = Frame.Layers[1] = FGratiaBodyMotionFrame::FLayer();
    }
    for (FRigBone& Bone : Rig) Bone.Delta = FQuat::Identity;
    AGratiaPreviewCharacter* Owner = Character.Get();
    if (UGratiaAnimInstance* Anim = Owner && Owner->CharacterMesh ? Cast<UGratiaAnimInstance>(Owner->CharacterMesh->GetAnimInstance()) : nullptr)
        Anim->BodyMotionFrame = Frame;
}

void UGratiaBodyMotion::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AGratiaPreviewCharacter* Owner = Character.Get();
    const UGratiaCharacterProfile* Profile = Owner ? Owner->CharacterProfile.Get() : nullptr;
    USkeletalMeshComponent* Mesh = Owner ? Owner->CharacterMesh.Get() : nullptr;
    const USkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    const UGratiaInteraction* Interaction = Owner ? Owner->Interaction.Get() : nullptr;
    if (!Profile || !MeshAsset || !Interaction || bQADisabled || !Profile->BodyMotion.bEnabled || !FMath::IsFinite(DeltaTime))
    {
        Publish(false);
        ApplyPlayRate(false);
        return;
    }
    const FGratiaBodyMotionSettings& S = Profile->BodyMotion;
    if (BuiltProfile.Get() != Profile || BuiltMesh.Get() != MeshAsset)
    {
        BuiltProfile = Profile; BuiltMesh = MeshAsset;
        Rebuild(*Profile, *MeshAsset);
        BuildFragments(*Profile, *MeshAsset);
        ResetState();
    }
    const float Dt = FMath::Clamp(DeltaTime, 0.0f, 0.1f);
    Seconds += Dt;

    // Archetype from the interaction mood; a change starts it fresh.
    const int32 Mood = FMath::Clamp(Interaction->Mood, 0, 2);
    if (Mood != Archetype)
    {
        Archetype = Mood;
        bComposureBroken = false;
        Thaw = 0.0f;
    }
    const FGratiaArchetypeTuning& A = GetTuning(S);
    const bool bThaws = A.ThawPerSecond > 0.0f;

    // Inputs: hands, contact, player head.
    const FHandSense Hand = SenseHands(Dt);
    bContact = Interaction->ActiveZone != INDEX_NONE || Interaction->Reaction > 0.05f;
    ContactSpeed = Smooth(ContactSpeed, bContact ? (Hand.bValid ? Hand.Speed : Interaction->LastReactionSpeed) : 0.0f, 0.25f, Dt);
    FVector PlayerCS = Forward * 150.0f;
    if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
        PlayerCS = Mesh->GetComponentTransform().InverseTransformPosition(Camera->GetCameraLocation());
    const FVector PlayerFlat = PlayerCS - FVector::DotProduct(PlayerCS, Up) * Up;
    const float PlayerDistance = PlayerFlat.Size() / FMath::Max(0.01f, float(Mesh->GetComponentScale().GetAbsMax()));
    const float PlayerSide = FVector::DotProduct(PlayerFlat.GetSafeNormal(), Right);

    // Excitement, stamina, thaw, composure.
    const float Pace = PaceQuality(ContactSpeed, S.GoodPaceLow, S.GoodPaceHigh);
    const float Stimulus = bContact ? Saturate(0.5f + 0.5f * Interaction->Reaction) * Pace : 0.0f;
    Excitement = Saturate(UpdateExcitement(Excitement, Stimulus, A.ExcitementGain, A.ExcitementDecay, Dt) + PendingExcitement);
    PendingExcitement = 0.0f;
    Stamina = UpdateStamina(Stamina, Excitement * (bContact ? 1.0f : 0.35f), S.StaminaDrainPerSecond, S.StaminaRecoverPerSecond, Dt);
    if (bThaws)
    {
        // Gentle, well-paced contact melts her; rough handling sets her back faster than neglect.
        const bool bRough = bContact && ContactSpeed > 3.0f * S.GoodPaceHigh;
        if (bContact && !bRough) Thaw = Saturate(Thaw + A.ThawPerSecond * Pace * Dt);
        else Thaw = Saturate(Thaw - A.ThawDecayPerSecond * (bRough ? 4.0f : 1.0f) * Dt);
    }
    else Thaw = 1.0f;
    if (A.BreakThreshold > 0.0f)
        bComposureBroken = bComposureBroken ? Excitement > A.BreakThreshold * 0.5f : Excitement >= A.BreakThreshold;
    else bComposureBroken = false;
    const float Response = A.BreakThreshold > 0.0f && !bComposureBroken ? A.MutedResponse : 1.0f;
    const float Resistance = bThaws ? 1.0f - Thaw : 0.0f;
    Coldness = 1.0f - Response;
    GazeAversion = Saturate(A.GazeAversion * (bThaws ? Resistance : 1.0f));
    Pout = Saturate(A.Pout * Resistance);
    PushAway = Saturate(A.PushAway * Resistance);
    Smile = Saturate(FMath::Lerp(A.Smile, FMath::Max(A.Smile, 0.6f), bThaws ? Thaw : 0.0f) * FMath::Lerp(0.4f, 1.0f, Response));

    // Which layers run: idle and stances get everything, a scripted performance only breathes.
    const bool bIdle = Owner->PreviewPose == EGratiaPreviewPose::Idle;
    const bool bStance = Owner->PreviewPose == EGratiaPreviewPose::Performance && Owner->IsStance(Owner->PerformanceIndex);
    bPerformanceMode = Owner->PreviewPose == EGratiaPreviewPose::Performance && !bStance;
    const bool bFree = (bIdle || bStance) && Interaction->bBodyMotion;
    const bool bAny = bFree || (bPerformanceMode && Interaction->bBodyMotion);

    // Breathing.
    FBreathSettings Breathing;
    Breathing.RestPerMinute = S.RestBreathsPerMinute;
    Breathing.ExcitedPerMinute = FMath::Max(S.RestBreathsPerMinute, S.ExcitedBreathsPerMinute);
    Breathing.FatigueBoost = S.FatigueBreathBoost;
    Breathing.RestDepth = S.RestBreathDepth;
    AdvanceBreath(Breath, Excitement, Stamina, Dt, Breathing);
    BreathPhase = Breath.Phase;
    BreathsPerMinute = Breath.PerMinute;
    BreathValue = BreathCurve(Breath.Phase) * Breath.Depth;

    // Tension and lean springs.
    TensionSpring.Step(bFree ? (bContact ? Saturate(0.6f * Interaction->Reaction + 0.4f * Excitement + 0.5f * Interaction->Impulse) * A.TensionScale * Response
        : 0.15f * Excitement) : 0.0f, S.TensionFrequencyHz, S.TensionDampingRatio, Dt);
    Tension = TensionSpring.X;
    FVector LeanTarget = FVector::ZeroVector;
    if (bFree && Hand.bValid)
    {
        const FVector Flat = (Hand.DirectionCS - FVector::DotProduct(Hand.DirectionCS, Up) * Up).GetSafeNormal();
        const float DodgeScale = A.DodgeScale * (bThaws ? FMath::Lerp(1.0f, 0.5f, Thaw) : 1.0f) * Response;
        if (!bContact) LeanTarget -= Flat * S.DodgeDegrees * DodgeWeight(Hand.DistanceCm, Hand.ClosingSpeed, S.DodgeNearCm, S.DodgeFarCm, S.DodgeFullSpeed) * DodgeScale;
        else LeanTarget += Flat * S.LeanTowardDegrees * A.LeanTowardScale * Response * FMath::Min(1.0f, 0.3f + Interaction->Reaction);
    }
    if (bFree && !bContact && A.InitiativeLeanDegrees > 0.0f && PlayerDistance < 120.0f)
        LeanTarget += PlayerFlat.GetSafeNormal() * A.InitiativeLeanDegrees * (1.0f - PlayerDistance / 120.0f);
    LeanForward.Step(FVector::DotProduct(LeanTarget, Forward), S.LeanFrequencyHz, S.LeanDampingRatio, Dt);
    LeanRight.Step(FVector::DotProduct(LeanTarget, Right), S.LeanFrequencyHz, S.LeanDampingRatio, Dt);
    const float TurnAway = bFree && PlayerDistance < 200.0f ? -FMath::Sign(PlayerSide) * A.TurnAwayDegrees * Resistance : 0.0f;
    TurnSpring.Step(TurnAway, S.LeanFrequencyHz * 0.6f, 0.9f, Dt);

    // Playhead modulation: the mocap follows the pace of the caress.
    const float DesiredRate = bContact && ContactSpeed > 0.5f
        ? PlayRateForSpeed(ContactSpeed, S.MinPlayRate, S.MaxPlayRate, S.SlowHandSpeed, S.FastHandSpeed)
        : 1.0f + 0.12f * Excitement;
    PlayRate = FMath::Clamp(Smooth(PlayRate, FMath::Lerp(1.0f, DesiredRate, Response), S.PlayRateSmoothingSeconds, Dt), 0.1f, 3.0f);
    ApplyPlayRate(bFree && S.bModulatePlayhead);

    // Fragments: only on the idle, without contact or a reaction cue, and not while dodging.
    const UGratiaAnimInstance* Anim = Cast<UGratiaAnimInstance>(Mesh->GetAnimInstance());
    const bool bCue = Anim && Anim->IsReactionCuePlaying();
    UpdateFragments(S, Dt, bIdle && Interaction->bBodyMotion && !bContact && !bCue && LeanTarget.IsNearlyZero(0.5f), PlayerCS);

    if (!bAny || Rig.IsEmpty())
    {
        Publish(false);
        return;
    }

    // Bone deltas (component space).
    const float BreathScale = bPerformanceMode ? S.PerformanceBreathScale : FMath::Lerp(0.8f, 1.2f, Saturate(A.LivelinessScale - 0.5f));
    const float B = BreathValue * BreathScale;
    const int32 SpineCount = Spine.Num();
    float NeckPitch = 0.0f;
    for (int32 I = 0; I < SpineCount; ++I)
    {
        // Breath extends the upper spine more than the lower one.
        const float Share = SpineCount > 1 ? (I + 1.0f) / (SpineCount * (SpineCount + 1) * 0.5f) : 1.0f;
        Tilt(Spine[I], -Forward, S.BreathChestDegrees * B * Share);
        NeckPitch += S.BreathChestDegrees * B * Share;
    }
    for (int32 C = 0; C < Clavicles.Num(); ++C)
    {
        const FVector Axis = FVector::CrossProduct(ClavicleDirections[C], Up);
        if (Axis.SizeSquared() > 1.0e-6) Rig[Clavicles[C]].Delta = FQuat(Axis.GetSafeNormal(), FMath::DegreesToRadians(S.BreathClavicleDegrees * B)) * Rig[Clavicles[C]].Delta;
    }
    Tilt(PelvisBone, Forward, S.BreathPelvisDegrees * B);
    float NeckCompensation = NeckPitch * S.BreathNeckCompensation;

    if (bFree)
    {
        // Perlin micro-sway; the legs are compensated so the feet stay planted.
        const float NoiseScale = A.LivelinessScale * (1.0f + S.ExcitedNoiseBoost * Excitement);
        const float F = S.NoiseFrequencyHz;
        Tilt(PelvisBone, Forward, GratiaBodyMotionMath::Noise(Seconds, F, 1.0f) * S.PelvisNoiseDegrees * NoiseScale);
        Tilt(PelvisBone, Right, GratiaBodyMotionMath::Noise(Seconds, F, 2.0f) * S.PelvisNoiseDegrees * NoiseScale);
        for (int32 I = 0; I < SpineCount; ++I)
        {
            const float Share = 1.0f / SpineCount;
            Tilt(Spine[I], Forward, GratiaBodyMotionMath::Noise(Seconds, F * 1.3f, 3.0f + I) * S.SpineNoiseDegrees * NoiseScale * Share);
            Tilt(Spine[I], Right, GratiaBodyMotionMath::Noise(Seconds, F * 1.1f, 7.0f + I) * S.SpineNoiseDegrees * NoiseScale * Share);
            Twist(Spine[I], GratiaBodyMotionMath::Noise(Seconds, F * 0.9f, 11.0f + I) * S.SpineNoiseDegrees * 0.5f * NoiseScale * Share);
        }
        Tilt(HeadBone, Forward, GratiaBodyMotionMath::Noise(Seconds, F * 1.6f, 17.0f) * S.HeadNoiseDegrees * NoiseScale);
        Tilt(HeadBone, Right, GratiaBodyMotionMath::Noise(Seconds, F * 1.4f, 19.0f) * S.HeadNoiseDegrees * 0.6f * NoiseScale);
        Twist(HeadBone, GratiaBodyMotionMath::Noise(Seconds, F * 1.2f, 23.0f) * S.HeadNoiseDegrees * 0.8f * NoiseScale);

        // Tension: lumbar arch (lower spine extends, pelvis tips forward), shoulder blades together.
        const float T = Tension;
        const int32 Lower = FMath::Max(1, (SpineCount + 1) / 2);
        for (int32 I = 0; I < SpineCount; ++I)
        {
            if (I < Lower) { Tilt(Spine[I], -Forward, S.ArchDegrees * T / Lower); NeckCompensation += S.ArchDegrees * T / Lower; }
            else { Tilt(Spine[I], Forward, S.ArchDegrees * 0.25f * T / FMath::Max(1, SpineCount - Lower)); NeckCompensation -= S.ArchDegrees * 0.25f * T / FMath::Max(1, SpineCount - Lower); }
        }
        Tilt(PelvisBone, Forward, S.ArchDegrees * 0.35f * T);
        NeckCompensation -= S.ArchDegrees * 0.35f * T;
        for (int32 C = 0; C < Clavicles.Num(); ++C)
        {
            const FVector Axis = FVector::CrossProduct(ClavicleDirections[C], -Forward);
            if (Axis.SizeSquared() > 1.0e-6) Rig[Clavicles[C]].Delta = FQuat(Axis.GetSafeNormal(), FMath::DegreesToRadians(S.ShoulderRetractDegrees * T)) * Rig[Clavicles[C]].Delta;
        }

        // Lean (dodge/into the touch/initiative) and turning away, spread over the spine.
        const FVector Lean = Forward * LeanForward.X + Right * LeanRight.X;
        const float LeanDegrees = Lean.Size();
        if (LeanDegrees > 1.0e-3f && SpineCount > 0)
        {
            const FVector Direction = Lean / LeanDegrees;
            for (const int32 Bone : Spine) Tilt(Bone, Direction, LeanDegrees / SpineCount);
            if (NeckBone != INDEX_NONE) Tilt(NeckBone, -Direction, LeanDegrees * S.LeanNeckCompensation);
        }
        for (const int32 Bone : Spine) Twist(Bone, TurnSpring.X / FMath::Max(1, SpineCount));
        if (NeckBone != INDEX_NONE) Twist(NeckBone, -TurnSpring.X * 0.3f);
    }
    // The neck cancels the chest pitch so the head and gaze stay steady.
    if (NeckBone != INDEX_NONE) Tilt(NeckBone, Forward, NeckCompensation);
    else Tilt(HeadBone, Forward, NeckCompensation);
    Publish(true);
}

FString UGratiaBodyMotion::GetDiagnostics() const
{
    return FString::Printf(TEXT("body motion: %s archetype=%d excitement=%.2f stamina=%.2f breath=%.0f/min depth=%.2f rate=%.2fx tension=%.2f lean=(%.1f,%.1f) turn=%.1f thaw=%.2f broken=%d fragment=%s (%d) bones=%d%s%s"),
        bQADisabled ? TEXT("off(QA)") : bPerformanceMode ? TEXT("performance") : TEXT("free"), Archetype, Excitement, Stamina,
        BreathsPerMinute, Breath.Depth, PlayRate, Tension, LeanForward.X, LeanRight.X, TurnSpring.X, Thaw, bComposureBroken ? 1 : 0,
        CurrentFragment.IsEmpty() ? TEXT("-") : *CurrentFragment, FragmentCount, Frame.Bones.Num(),
        MissingBones.IsEmpty() ? TEXT("") : TEXT(" missing: "), *MissingBones);
}

void GratiaBodyMotionPose::BlendFragments(FPoseContext& Output, const FGratiaBodyMotionFrame& Frame)
{
    for (const FGratiaBodyMotionFrame::FLayer& Layer : Frame.Layers)
    {
        if (!Layer.Clip || !FMath::IsFinite(Layer.Weight) || Layer.Weight <= 0.001f || !FMath::IsFinite(Layer.Time)) continue;
        const float Weight = FMath::Clamp(Layer.Weight, 0.0f, 1.0f);
        FPoseContext FragmentPose(Output);
        FAnimationPoseData FragmentData(FragmentPose);
        Layer.Clip->GetAnimationPose(FragmentData, FAnimExtractContext(double(Layer.Time), false));
        const FCompactPoseBoneIndex RootIndex(0);
        const FTransform Root = Output.Pose[RootIndex];
        // Body only: the face keeps its procedural/authored curves.
        FBlendedCurve Curves;
        Curves.CopyFrom(Output.Curve);
        FAnimationPoseData BaseData(Output);
        FAnimationRuntime::BlendTwoPosesTogetherInPlace(BaseData, FragmentData, 1.0f - Weight);
        Output.Curve.CopyFrom(Curves);
        if (Frame.bKeepRoot) Output.Pose[RootIndex] = Root;
    }
}

void GratiaBodyMotionPose::ApplyBones(FPoseContext& Output, const FGratiaBodyMotionFrame& Frame)
{
    if (Frame.Bones.IsEmpty()) return;
    const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
    for (const FGratiaBodyBoneDelta& Delta : Frame.Bones)
    {
        if (Delta.Rotation.ContainsNaN()) continue;
        const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Delta.Bone);
        if (MeshIndex == INDEX_NONE) continue;
        const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
        if (Index.GetInt() == INDEX_NONE) continue;
        FTransform& Transform = Output.Pose[Index];
        if (Delta.bPre)
        {
            // Child of a rotated parent: undo the parent's rotation (orientation and joint position).
            Transform.SetRotation((Delta.Rotation * Transform.GetRotation()).GetNormalized());
            Transform.SetTranslation(Delta.Rotation.RotateVector(Transform.GetTranslation()));
        }
        else Transform.SetRotation((Transform.GetRotation() * Delta.Rotation).GetNormalized());
    }
}
