#include "GratiaPenetration.h"

#include "GratiaAnimInstance.h"
#include "GratiaBodySurface.h"
#include "GratiaInteraction.h"
#include "GratiaPenetrator.h"
#include "GratiaPreviewCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaPenetration, Log, All);

namespace
{
FTransform GratiaPenetrationRef(const FReferenceSkeleton& Ref, int32 Index)
{
    FTransform Result = Ref.GetRefBonePose()[Index];
    for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
        Result *= Ref.GetRefBonePose()[Parent];
    return Result;
}

FTransform GratiaNoScale(const FTransform& Transform) { return FTransform(Transform.GetRotation(), Transform.GetLocation()); }
}

UGratiaPenetration::UGratiaPenetration()
{
    PrimaryComponentTick.bCanEverTick = true;
    // After the hands (runtime, post-update work): the held shaft is current when its joints are laid.
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGratiaPenetration::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
}

bool UGratiaPenetration::IsEnabled() const
{
    const UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    return Profile && Profile->Penetration.bEnabled && Character->CharacterMesh && Character->CharacterMesh->GetSkeletalMeshAsset();
}

const FGratiaPenetrationChannel* UGratiaPenetration::Definition(const FChannel& Channel) const
{
    const UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    return Profile && Profile->Penetration.Channels.IsValidIndex(Channel.Definition) ? &Profile->Penetration.Channels[Channel.Definition] : nullptr;
}

void UGratiaPenetration::SetPenetrator(AGratiaPenetrator* Shaft)
{
    if (Penetrator.Get() == Shaft) return;
    for (int32 Index = Engagements.Num() - 1; Index >= 0; --Index)
        if (Engagements[Index].Shaft.Get() == Penetrator.Get()) Release(Index, TEXT("shaft changed"));
    Penetrator = Shaft;
    Shown.Reset(); Residual.Reset();
}

void UGratiaPenetration::SetCandidates(const TArray<AGratiaPenetrator*>& Shafts)
{
    Candidates.Reset();
    for (AGratiaPenetrator* Shaft : Shafts) if (Shaft) Candidates.Add(Shaft);
}

TArray<AGratiaPenetrator*> UGratiaPenetration::GetShafts() const
{
    TArray<AGratiaPenetrator*> Shafts;
    if (Penetrator.IsValid()) Shafts.Add(Penetrator.Get());
    for (const TWeakObjectPtr<AGratiaPenetrator>& Candidate : Candidates) if (Candidate.IsValid()) Shafts.AddUnique(Candidate.Get());
    return Shafts;
}

const UGratiaPenetration::FEngagement* UGratiaPenetration::FindEngagement(const AGratiaPenetrator* Shaft) const
{
    return Shaft ? Engagements.FindByPredicate([Shaft](const FEngagement& Engagement) { return Engagement.Shaft.Get() == Shaft; }) : nullptr;
}

bool UGratiaPenetration::GetEngagedFrame(const AGratiaPenetrator* Shaft, FVector& Entrance, FVector& Inward, double& Inserted, double& Depth) const
{
    const FEngagement* Engagement = FindEngagement(Shaft);
    if (!Engagement || !Channels.IsValidIndex(Engagement->Channel) || !Channels[Engagement->Channel].bFrame) return false;
    const FChannel& Channel = Channels[Engagement->Channel];
    Entrance = Channel.Entrance + Engagement->Lateral;
    Inward = Channel.Inward;
    Inserted = Engagement->Inserted;
    Depth = Channel.Path.Length();
    return true;
}

bool UGratiaPenetration::GetChannelFrame(int32 Index, FName& Name, FVector& Entrance, FVector& Inward, double& Depth) const
{
    if (!Channels.IsValidIndex(Index) || !Channels[Index].bFrame) return false;
    const FChannel& Channel = Channels[Index];
    Name = Channel.Name; Entrance = Channel.Entrance; Inward = Channel.Inward; Depth = Channel.Path.Length();
    return true;
}

double UGratiaPenetration::GetEntranceGap(const FVector& Point) const
{
    double Gap = UE_BIG_NUMBER;
    for (const FChannel& Channel : Channels) if (Channel.bFrame) Gap = FMath::Min(Gap, FVector::Distance(Point, Channel.Entrance));
    return Gap;
}

int32 UGratiaPenetration::FindCapture(const AGratiaPenetrator& Shaft) const
{
    if (!Shaft.IsHeld()) return INDEX_NONE;
    const GratiaPenetration::FShaft Geometry = Shaft.GetShaft();
    const FTransform Base = Shaft.GetBase();
    const FVector Direction = Base.GetRotation().GetForwardVector();
    for (int32 Index = 0; Index < Channels.Num(); ++Index)
    {
        const FChannel& Channel = Channels[Index];
        const FGratiaPenetrationChannel* Source = Definition(Channel);
        // A channel takes up to two shafts side by side.
        if (Engagements.FilterByPredicate([Index](const FEngagement& Engagement) { return Engagement.Channel == Index; }).Num() >= MaxShaftsPerChannel) continue;
        if (Channel.bFrame && Source && GratiaPenetration::CanCapture(Geometry, Base.GetLocation(), Direction, Channel.Entrance, Channel.Inward,
            CaptureRadius(Channel, Geometry), Source->CaptureAngleDegrees)) return Index;
    }
    return INDEX_NONE;
}

void UGratiaPenetration::Release(int32 Index, const TCHAR* Reason)
{
    if (!Engagements.IsValidIndex(Index)) return;
    const FEngagement& Engagement = Engagements[Index];
    if (Channels.IsValidIndex(Engagement.Channel))
        UE_LOG(LogGratiaPenetration, Display, TEXT("PENETRATION_EXIT channel=%s shaft=%s reason=%s depth=%.1fcm"),
            *Channels[Engagement.Channel].Name.ToString(), Engagement.Shaft.IsValid() ? *Engagement.Shaft->GetSizeLabel() : TEXT("-"),
            Reason, FMath::Max(0.0, Engagement.Inserted));
    if (AGratiaPenetrator* Shaft = Engagement.Shaft.Get()) Shaft->HapticAmplitude = Shaft->HapticFrequency = 0.0f;
    Engagements.RemoveAt(Index);
}

void UGratiaPenetration::ReleaseAll(const TCHAR* Reason)
{
    for (int32 Index = Engagements.Num() - 1; Index >= 0; --Index) Release(Index, Reason);
}

void UGratiaPenetration::ResetPenetration()
{
    ReleaseAll(TEXT("reset"));
    for (FChannel& Channel : Channels)
        for (FWallBone& Bone : Channel.Bones) Bone.Offset = Bone.Target = FVector::ZeroVector;
    if (Character.IsValid() && Character->CharacterMesh)
        for (FChannel& Channel : Channels)
            if (Channel.bMorphSet) { Character->CharacterMesh->SetMorphTarget(Channel.Morph, 0.0f, false); Channel.bMorphSet = false; }
    if (Character.IsValid() && Character->CharacterMesh) PushToAnimation();
    if (AGratiaPenetrator* Shaft = Penetrator.Get()) Shaft->SetJoints({});
    Shown.Reset(); Residual.Reset();
}

void UGratiaPenetration::ResolveChannels()
{
    Channels.Reset();
    const UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    const USkeletalMesh* Mesh = Profile && Character->CharacterMesh ? Character->CharacterMesh->GetSkeletalMeshAsset() : nullptr;
    ResolvedProfile = Profile; ResolvedMesh = Mesh;
    if (!Profile || !Mesh || !Profile->Penetration.bEnabled) return;
    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    for (int32 Index = 0; Index < Profile->Penetration.Channels.Num(); ++Index)
    {
        const FGratiaPenetrationChannel& Source = Profile->Penetration.Channels[Index];
        if (!Source.bEnabled) continue;
        FChannel Channel;
        Channel.Name = Source.Name; Channel.Definition = Index;
        Channel.Anchor = Ref.FindBoneIndex(Profile->ResolveBone(Source.AnchorBoneSemantic));
        FVector Entrance = FVector::ZeroVector;
        int32 Found = 0;
        bool bMissing = Channel.Anchor == INDEX_NONE || Source.Name.IsNone();
        for (const FName Bone : Source.EntranceBones)
        {
            const int32 BoneIndex = Ref.FindBoneIndex(Bone);
            if (BoneIndex == INDEX_NONE) { bMissing = true; continue; }
            Entrance += GratiaPenetrationRef(Ref, BoneIndex).GetLocation();
            ++Found;
        }
        const int32 Target = Source.InwardTargetBone.IsNone() ? Channel.Anchor : Ref.FindBoneIndex(Source.InwardTargetBone);
        if (bMissing || Found == 0 || Target == INDEX_NONE)
        {
            UE_LOG(LogGratiaPenetration, Warning, TEXT("PENETRATION channel %s disabled: missing anchor, entrance or inward bone"), *Source.Name.ToString());
            continue;
        }
        Entrance = Entrance / Found + Source.EntranceOffsetCm;
        const FVector Inward = (GratiaPenetrationRef(Ref, Target).GetLocation() + Source.InwardOffsetCm - Entrance).GetSafeNormal();
        if (Inward.IsNearlyZero())
        {
            UE_LOG(LogGratiaPenetration, Warning, TEXT("PENETRATION channel %s disabled: entrance and inward target coincide"), *Source.Name.ToString());
            continue;
        }
        const FTransform AnchorRef = GratiaPenetrationRef(Ref, Channel.Anchor);
        Channel.EntranceLocal = AnchorRef.InverseTransformPosition(Entrance);
        Channel.InwardLocal = AnchorRef.InverseTransformVectorNoScale(Inward);
        for (const FGratiaChannelBone& Settings : Source.Bones)
        {
            const int32 BoneIndex = Ref.FindBoneIndex(Settings.Bone);
            if (BoneIndex == INDEX_NONE) continue;
            const FVector Relative = GratiaPenetrationRef(Ref, BoneIndex).GetLocation() - Entrance;
            const double Along = FVector::DotProduct(Relative, Inward);
            FVector Radial = Relative - Inward * Along;
            const double Distance = Radial.Size();
            // A bone on the axis opens sideways (its own side if it has one).
            if (Radial.Size() < 0.2)
            {
                Radial = FVector::CrossProduct(Inward, FVector::UpVector);
                if (Radial.IsNearlyZero()) Radial = FVector::CrossProduct(Inward, FVector::ForwardVector);
            }
            FWallBone Bone;
            Bone.Name = Settings.Bone; Bone.Settings = Settings;
            // Bones outside the entrance (lips, outer ring) answer to the entrance opening.
            Bone.Depth = FMath::Clamp(Along, 0.0, double(Source.DepthCm));
            Bone.Radial = AnchorRef.InverseTransformVectorNoScale(Radial.GetSafeNormal());
            Bone.RestDistance = Distance < 0.2 ? 0.0 : Distance;
            Channel.Bones.Add(Bone);
        }
        if (!Source.OpeningMorph.IsNone() && Mesh->FindMorphTarget(Source.OpeningMorph)) Channel.Morph = Source.OpeningMorph;
        UE_LOG(LogGratiaPenetration, Display, TEXT("PENETRATION channel=%s anchor=%s depth=%.1fcm wall_bones=%d morph=%s"),
            *Channel.Name.ToString(), *Ref.GetBoneName(Channel.Anchor).ToString(), Source.DepthCm, Channel.Bones.Num(),
            Channel.Morph.IsNone() ? TEXT("none") : *Channel.Morph.ToString());
        Channels.Add(MoveTemp(Channel));
    }
}

bool UGratiaPenetration::UpdateFrame(FChannel& Channel) const
{
    Channel.bFrame = false;
    const USkeletalMeshComponent* Mesh = Character.IsValid() ? Character->CharacterMesh.Get() : nullptr;
    const FGratiaPenetrationChannel* Settings = Definition(Channel);
    if (!Mesh || !Settings || Channel.Anchor == INDEX_NONE || Channel.Anchor >= Mesh->GetNumBones()) return false;
    const FTransform Anchor = Mesh->GetBoneTransform(Channel.Anchor);
    if (Anchor.ContainsNaN()) return false;
    Channel.AnchorWorld = Anchor;
    Channel.Scale = FMath::Max(UE_SMALL_NUMBER, Mesh->GetComponentTransform().GetScale3D().GetAbsMax());
    Channel.Entrance = Anchor.TransformPosition(Channel.EntranceLocal);
    Channel.Inward = Anchor.TransformVectorNoScale(Channel.InwardLocal).GetSafeNormal();
    Channel.Path.Reset();
    Channel.Path.Add(Channel.Entrance);
    Channel.Path.Add(Channel.Entrance + Channel.Inward * Settings->DepthCm * Channel.Scale);
    Channel.bFrame = !Channel.Entrance.ContainsNaN() && !Channel.Inward.IsNearlyZero() && Channel.Path.IsValid();
    return Channel.bFrame;
}

double UGratiaPenetration::CaptureRadius(const FChannel& Channel, const GratiaPenetration::FShaft& Shaft) const
{
    const FGratiaPenetrationChannel* Settings = Definition(Channel);
    return (Settings ? Settings->CaptureRadiusCm * Channel.Scale : 0.0) + 0.5 * Shaft.Radius;
}

FVector UGratiaPenetration::WallTarget(const FChannel& Channel, const FWallBone& Bone, const GratiaPenetration::FShaft& Shaft, double Inserted, double Velocity) const
{
    const FGratiaPenetrationChannel* Settings = Definition(Channel);
    if (!Settings || !Channel.bFrame) return FVector::ZeroVector;
    const double Scale = Channel.Scale;
    const double Depth = FMath::Clamp(Inserted, 0.0, Channel.Path.Length());
    const double Opening = GratiaPenetration::WallOpening(Shaft, Depth, Bone.Depth * Scale, Settings->RestRadiusCm * Scale, Settings->WallFalloffCm * Scale);
    // The wall only has to clear the shaft surface: a bone already that far from the axis does not move, one closer
    // moves by the difference (plus a little clearance), so the slit opens around the shaft instead of ballooning.
    constexpr double Clearance = 0.15;
    const double Surface = Opening > 0.0 ? Opening + (Settings->RestRadiusCm + Clearance) * Scale : 0.0;
    const double Need = FMath::Max(0.0, Surface - Bone.RestDistance * Scale);
    // With an opening morph the morph opens the channel over a wide, smooth area; the bones keep a quarter for the
    // lips' own shape (and their drag below), so the two do not add up.
    const double Share = Channel.Morph.IsNone() ? 1.0 : 0.25;
    const double Out = Share * GratiaPenetration::BoneOffset(Need, Bone.Settings.StartOpeningCm * Scale, Bone.Settings.Response, Bone.Settings.MaxOffsetCm * Scale);
    const double MaxDrag = Bone.Settings.MaxDragCm * Scale;
    // Only a wall the shaft touches is dragged along with it.
    const double Drag = Need > 0.0 ? FMath::Clamp(Velocity * Bone.Settings.DragSeconds, -MaxDrag, MaxDrag) * FMath::Min(1.0, Need / FMath::Max(0.1, 0.5 * Scale)) : 0.0;
    const FVector Result = Channel.AnchorWorld.TransformVectorNoScale(Bone.Radial) * Out + Channel.Inward * Drag;
    return Result.ContainsNaN() ? FVector::ZeroVector : Result;
}

void UGratiaPenetration::CollideWithBody(const GratiaPenetration::FShaft& Shaft, TArray<FVector>& Joints) const
{
    const UGratiaBodySurface* Surface = Character.IsValid() ? Character->BodySurface.Get() : nullptr;
    if (!Surface || !Surface->HasSurface() || Joints.Num() < 2) return;
    const double Spacing = Shaft.Spacing();
    for (int32 Joint = 1; Joint < Joints.Num(); ++Joint)
    {
        // Each joint keeps its spacing from the (possibly pushed) previous joint.
        FVector Point = Joints[Joint - 1] + (Joints[Joint] - Joints[Joint - 1]).GetSafeNormal() * Spacing;
        const double Radius = FMath::Max(0.5, Shaft.RadiusAt(Shaft.Length - Spacing * Joint));
        bool bNearEntrance = false;
        for (const FChannel& Channel : Channels)
        {
            const FGratiaPenetrationChannel* Settings = Definition(Channel);
            bNearEntrance |= Channel.bFrame && Settings
                && FVector::Distance(Point, Channel.Entrance) < Settings->CaptureRadiusCm * Channel.Scale + Shaft.Radius + 6.0;
        }
        FGratiaSurfaceHit Hit;
        if (!bNearEntrance && Surface->FindNearest(Point, float(Radius), Hit, true) && Hit.Gap < Radius && !Hit.Normal.ContainsNaN())
            Point += Hit.Normal * (Radius - Hit.Gap);
        Joints[Joint] = Point;
    }
}

void UGratiaPenetration::UpdateWalls(float Delta)
{
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get();
    const double Alpha = 1.0 - FMath::Exp(-FMath::Max(1.0f, Profile->Penetration.WallFollowSpeed) * Delta);
    for (int32 Index = 0; Index < Channels.Num(); ++Index)
    {
        FChannel& Channel = Channels[Index];
        const FGratiaPenetrationChannel* Settings = Definition(Channel);
        TArray<FEngagement*> Inside;
        if (Settings && Channel.bFrame)
            for (FEngagement& Engagement : Engagements) if (Engagement.Channel == Index && Engagement.Shaft.IsValid()) Inside.Add(&Engagement);
        // Two shafts side by side open the channel like one of their combined cross-section.
        double OpeningSquared = 0.0;
        for (FEngagement* Engagement : Inside)
        {
            Engagement->Opening = GratiaPenetration::WallOpening(Engagement->Shaft->GetShaft(), FMath::Clamp(Engagement->Inserted, 0.0, Channel.Path.Length()),
                0.0, Settings->RestRadiusCm * Channel.Scale, Settings->WallFalloffCm * Channel.Scale);
            OpeningSquared += FMath::Square(Engagement->Opening);
        }
        for (FWallBone& Bone : Channel.Bones)
        {
            FVector Target = FVector::ZeroVector;
            if (Inside.Num() == 1)
                Target = WallTarget(Channel, Bone, Inside[0]->Shaft->GetShaft(), Inside[0]->Inserted, Inside[0]->Velocity);
            else if (Inside.Num() > 1)
            {
                const FVector Radial = Channel.AnchorWorld.TransformVectorNoScale(Bone.Radial);
                double Out = 0.0, Drag = 0.0;
                for (const FEngagement* Engagement : Inside)
                {
                    const FVector Part = WallTarget(Channel, Bone, Engagement->Shaft->GetShaft(), Engagement->Inserted, Engagement->Velocity);
                    Out += FMath::Square(FVector::DotProduct(Part, Radial));
                    Drag += FVector::DotProduct(Part, Channel.Inward);
                }
                const double MaxOut = Bone.Settings.MaxOffsetCm * Channel.Scale, MaxDrag = Bone.Settings.MaxDragCm * Channel.Scale;
                Target = Radial * FMath::Min(FMath::Sqrt(Out), MaxOut) + Channel.Inward * FMath::Clamp(Drag, -MaxDrag, MaxDrag);
            }
            Bone.Target = Target.ContainsNaN() ? FVector::ZeroVector : Target;
            Bone.Offset = FMath::Lerp(Bone.Offset, Bone.Target, Alpha);
            if (Bone.Offset.ContainsNaN()) Bone.Offset = FVector::ZeroVector;
        }
        if (Channel.Morph.IsNone() || !Settings) continue;
        const float Weight = Inside.IsEmpty() ? 0.0f
            : FMath::Clamp(float(FMath::Sqrt(OpeningSquared) / (Settings->MorphFullOpeningCm * Channel.Scale)), 0.0f, 1.0f);
        if (Weight > 0.0f || Channel.bMorphSet)
        {
            Character->CharacterMesh->SetMorphTarget(Channel.Morph, Weight, false);
            Channel.bMorphSet = Weight > 0.0f;
        }
    }
}

void UGratiaPenetration::PushToAnimation()
{
    auto* Anim = Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance());
    if (!Anim) return;
    // A bone shared by two channels moves by the sum of both.
    TMap<FName, FVector> Offsets;
    for (const FChannel& Channel : Channels)
        for (const FWallBone& Bone : Channel.Bones)
            if (Bone.Offset.SizeSquared() > 1.0e-6) Offsets.FindOrAdd(Bone.Name) += Bone.Offset;
    const FTransform Component = Character->CharacterMesh->GetComponentTransform();
    Anim->PenetrationOffsets.Reset();
    for (const TPair<FName, FVector>& Offset : Offsets) Anim->PenetrationOffsets.Emplace(Offset.Key, Component.InverseTransformVector(Offset.Value));
}

float UGratiaPenetration::GetMaxWallOffsetCm() const
{
    double Max = 0.0;
    for (const FChannel& Channel : Channels) for (const FWallBone& Bone : Channel.Bones) Max = FMath::Max(Max, Bone.Offset.Size());
    return float(Max);
}

void UGratiaPenetration::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    if (SolvedFrame != GFrameCounter) Solve(Delta);
}

void UGratiaPenetration::Solve(float Delta)
{
    SolvedFrame = GFrameCounter;
    if (!Character.IsValid()) Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    if (!Character.IsValid() || !Character->CharacterMesh) return;
    const TArray<AGratiaPenetrator*> Shafts = GetShafts();
    if (!IsEnabled())
    {
        if (bWasEnabled) { ResetPenetration(); Channels.Reset(); ResolvedProfile = nullptr; ResolvedMesh = nullptr; }
        bWasEnabled = false;
        for (AGratiaPenetrator* Shaft : Shafts) Shaft->HapticAmplitude = Shaft->HapticFrequency = 0.0f;
        if (AGratiaPenetrator* Primitive = Penetrator.Get()) Primitive->SetJoints({});
        return;
    }
    bWasEnabled = true;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get();
    if (ResolvedProfile.Get() != Profile || ResolvedMesh.Get() != Character->CharacterMesh->GetSkeletalMeshAsset())
    {
        ResetPenetration();
        ResolveChannels();
    }
    const FGratiaPenetrationSettings& Settings = Profile->Penetration;
    const float Step = FMath::IsFinite(Delta) ? FMath::Clamp(Delta, 0.0f, 0.1f) : 0.0f;
    for (FChannel& Channel : Channels) UpdateFrame(Channel);

    // Shafts that went away leave their channel.
    for (int32 Index = Engagements.Num() - 1; Index >= 0; --Index)
        if (!Engagements[Index].Shaft.IsValid() || !Shafts.Contains(Engagements[Index].Shaft.Get()) || !Channels.IsValidIndex(Engagements[Index].Channel))
            Release(Index, TEXT("shaft removed"));
    // A held shaft whose tip finds an entrance with room goes in (the primitive first, then the hands).
    for (AGratiaPenetrator* Shaft : Shafts)
    {
        if (FindEngagement(Shaft)) continue;
        const int32 Channel = FindCapture(*Shaft);
        if (Channel == INDEX_NONE) continue;
        FEngagement& Engagement = Engagements.AddDefaulted_GetRef();
        Engagement.Channel = Channel;
        Engagement.Shaft = Shaft;
        Engagement.Pulse = 0.08;
        const int32 Sharing = Engagements.FilterByPredicate([Channel](const FEngagement& Other) { return Other.Channel == Channel; }).Num();
        UE_LOG(LogGratiaPenetration, Display, TEXT("PENETRATION_ENTER channel=%s size=%s hand=%d in_channel=%d"),
            *Channels[Channel].Name.ToString(), *Shaft->GetSizeLabel(), Shaft->GetHeldHand(), Sharing);
    }
    // Two shafts in one channel lie side by side: each moves off the axis, away from the other.
    const double Ease = 1.0 - FMath::Exp(-12.0 * Step);
    for (int32 Index = 0; Index < Channels.Num(); ++Index)
    {
        TArray<FEngagement*> Inside;
        for (FEngagement& Engagement : Engagements) if (Engagement.Channel == Index) Inside.Add(&Engagement);
        if (Inside.Num() < 2)
        {
            for (FEngagement* Engagement : Inside) Engagement->Lateral = FMath::Lerp(Engagement->Lateral, FVector::ZeroVector, Ease);
            continue;
        }
        const FVector Inward = Channels[Index].Inward;
        FVector Apart = Inside[1]->Shaft->GetBase().GetLocation() - Inside[0]->Shaft->GetBase().GetLocation();
        Apart -= Inward * FVector::DotProduct(Apart, Inward);
        if (!Apart.Normalize()) Apart = FVector::CrossProduct(Inward, FVector::UpVector).GetSafeNormal();
        const double First = Inside[0]->Shaft->GetShaft().Radius, Second = Inside[1]->Shaft->GetShaft().Radius;
        Inside[0]->Lateral = FMath::Lerp(Inside[0]->Lateral, -Apart * Second * 0.8, Ease);
        Inside[1]->Lateral = FMath::Lerp(Inside[1]->Lateral, Apart * First * 0.8, Ease);
    }
    // Every shaft inside: a released primitive stays on the body, a hand that opens leaves; depth, release, reactions.
    TArray<FVector> PrimitiveJoints;
    for (int32 Index = Engagements.Num() - 1; Index >= 0; --Index)
    {
        FEngagement& Engagement = Engagements[Index];
        AGratiaPenetrator* Shaft = Engagement.Shaft.Get();
        const FChannel& Channel = Channels[Engagement.Channel];
        const FGratiaPenetrationChannel* Source = Definition(Channel);
        if (Shaft->IsHeld()) Engagement.bAnchored = false;
        else if (Engagement.bShaftWasHeld)
        {
            if (!Shaft->bAnchorWhenReleased) { Release(Index, TEXT("hand let go")); continue; }
            Engagement.bAnchored = true;
            Engagement.AnchoredBase = Shaft->GetBase().GetRelativeTransform(GratiaNoScale(Channel.AnchorWorld));
        }
        Engagement.bShaftWasHeld = Shaft->IsHeld();
        if (Engagement.bAnchored) Shaft->SetBase(Engagement.AnchoredBase * GratiaNoScale(Channel.AnchorWorld));
        const FTransform Base = Shaft->GetBase();
        const FVector Direction = Base.GetRotation().GetForwardVector();
        const GratiaPenetration::FShaft Geometry = Shaft->GetShaft();
        TArray<FVector> Joints;
        // Solved in the channel frame shifted by the shaft's place beside the other one.
        const double Inserted = Channel.bFrame && Source
            ? GratiaPenetration::EngagedJoints(Geometry, Base.GetLocation() - Engagement.Lateral, Direction, Channel.Path, Channel.Path.Length(), Joints) : -1.0e9;
        const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, Channel.Inward), -1.0, 1.0)));
        // The tip may hover before the entrance it was captured at; it lets go a little further out.
        if (!Source || !Channel.bFrame || Inserted < -(CaptureRadius(Channel, Geometry) + 1.0) || Angle > Source->ReleaseAngleDegrees)
        {
            Release(Index, !Source || !Channel.bFrame ? TEXT("channel unavailable") : Inserted < 0.0 ? TEXT("pulled out") : TEXT("bent away"));
            continue;
        }
        const double Moved = Engagement.bFresh ? 0.0 : Inserted - Engagement.Inserted;
        const double Speed = Step > 0.0f ? Moved / Step : 0.0;
        Engagement.Velocity = FMath::Lerp(Engagement.Velocity, Speed, 1.0 - FMath::Exp(-15.0 * Step));
        Engagement.Inserted = Inserted;
        Engagement.Travel += FMath::Abs(Moved);
        if (Engagement.bFresh || Engagement.Travel >= Settings.ReactionTravelCm * Channel.Scale)
        {
            Engagement.Travel = 0.0;
            if (Character->Interaction) Character->Interaction->ExternalReaction(Channel.Name, Shaft->GetHeldHand(), float(FMath::Abs(Engagement.Velocity)));
        }
        Engagement.bFresh = false;
        if (Shaft == Penetrator.Get())
        {
            for (FVector& Joint : Joints) Joint += Engagement.Lateral;
            PrimitiveJoints = MoveTemp(Joints);
        }
    }
    // The drawn primitive: along the channel when inside, otherwise straight and sliding over the body.
    if (AGratiaPenetrator* Primitive = Penetrator.Get())
    {
        const GratiaPenetration::FShaft Geometry = Primitive->GetShaft();
        TArray<FVector> Target = MoveTemp(PrimitiveJoints);
        const bool bEngagedNow = FindEngagement(Primitive) != nullptr;
        if (!bEngagedNow || Target.IsEmpty())
        {
            const FTransform Base = Primitive->GetBase();
            GratiaPenetration::StraightJoints(Geometry, Base.GetLocation(), Base.GetRotation().GetForwardVector(), Target);
            if (Settings.bShaftBodyCollision) CollideWithBody(Geometry, Target);
        }
        // Entering and leaving switch the shape: the jump of the joints eases out (~60 ms).
        if (Shown.Num() != Target.Num()) { Shown = Target; Residual.Init(FVector::ZeroVector, Target.Num()); }
        else if (bEngagedNow != bShownEngaged)
            for (int32 Joint = 1; Joint < Target.Num(); ++Joint) Residual[Joint] = (Shown[Joint] - Target[Joint]).GetClampedToMaxSize(20.0);
        bShownEngaged = bEngagedNow;
        const double Decay = FMath::Exp(-Step / 0.06);
        for (int32 Joint = 0; Joint < Target.Num(); ++Joint)
        {
            Residual[Joint] = Joint == 0 ? FVector::ZeroVector : Residual[Joint] * Decay;
            Shown[Joint] = Target[Joint] + Residual[Joint];
        }
        Primitive->SetJoints(Shown);
    }
    UpdateWalls(Step);
    PushToAnimation();
    // Feedback per held shaft: a short pulse on entering, then vibration with the insertion speed and the stretch.
    float MaxStretch = 0.0f;
    for (AGratiaPenetrator* Shaft : Shafts)
    {
        const FEngagement* Engagement = FindEngagement(Shaft);
        float Amplitude = 0.0f, Frequency = 0.0f;
        if (Engagement)
        {
            const float Stretch = FMath::Clamp(float(Engagement->Opening / FMath::Max(0.5, Shaft->GetShaft().Radius)), 0.0f, 1.0f);
            MaxStretch = FMath::Max(MaxStretch, Stretch);
            if (Shaft->IsHeld())
            {
                const float Speed = FMath::Clamp(float(FMath::Abs(Engagement->Velocity)) / FMath::Max(1.0f, Settings.HapticFullSpeed), 0.0f, 1.0f);
                Amplitude = Settings.HapticBase + (Settings.HapticMax - Settings.HapticBase) * FMath::Max(Speed, 0.35f * Stretch);
                Frequency = FMath::Lerp(0.2f, 0.7f, Speed);
                if (Engagement->Pulse > 0.0) { Amplitude = FMath::Max(Amplitude, Settings.HapticCapturePulse); Frequency = 0.4f; }
            }
        }
        Shaft->HapticAmplitude = FMath::Clamp(Amplitude, 0.0f, 1.0f);
        Shaft->HapticFrequency = FMath::Clamp(Frequency, 0.0f, 1.0f);
    }
    for (FEngagement& Engagement : Engagements) Engagement.Pulse = FMath::Max(0.0, Engagement.Pulse - Step);
    if (!Engagements.IsEmpty() && Character->Interaction) Character->Interaction->SetExternalHold(Settings.HoldReactionWeight * (0.6f + 0.4f * MaxStretch));
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if (Now >= NextDiagnosticTime && (!Engagements.IsEmpty() || Penetrator.IsValid()))
    {
        NextDiagnosticTime = Now + 2.0;
        UE_LOG(LogGratiaPenetration, Display, TEXT("PENETRATION %s"), *GetDiagnostics());
    }
}

FString UGratiaPenetration::GetDiagnostics() const
{
    const AGratiaPenetrator* Shaft = Penetrator.Get();
    const auto* Anim = Character.IsValid() && Character->CharacterMesh ? Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance()) : nullptr;
    FString Inside;
    for (const FEngagement& Engagement : Engagements)
        Inside += FString::Printf(TEXT("%s%s:%s:%.1fcm/%.2fcm%s"), Inside.IsEmpty() ? TEXT("") : TEXT(","),
            Channels.IsValidIndex(Engagement.Channel) ? *Channels[Engagement.Channel].Name.ToString() : TEXT("?"),
            Engagement.Shaft.IsValid() ? *Engagement.Shaft->GetSizeLabel() : TEXT("-"), FMath::Max(0.0, Engagement.Inserted), Engagement.Opening,
            Engagement.bAnchored ? TEXT(":anchored") : TEXT(""));
    return FString::Printf(TEXT("channels=%d primitive=%s held=%d inside=[%s] wall_max=%.2fcm applied=%d state=%s"),
        Channels.Num(), Shaft ? *Shaft->GetSizeLabel() : TEXT("none"), Shaft ? Shaft->GetHeldHand() : INDEX_NONE, *Inside,
        GetMaxWallOffsetCm(), Anim ? Anim->GetAppliedPenetrationBones() : -1, IsEnabled() ? TEXT("running") : TEXT("disabled"));
}

bool UGratiaPenetration::RunChecks(FString& Failure)
{
    if (!Character.IsValid()) Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    const UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    if (!Profile || !Character->CharacterMesh) { Failure = TEXT("Penetration requires a character, profile and mesh"); return false; }
    if (!IsEnabled())
    {
        UE_LOG(LogGratiaPenetration, Display, TEXT("PENETRATION_CHECK skip=profile has no penetration channels"));
        return true;
    }
    ResetPenetration();
    ResolveChannels();
    const int32 Defined = Profile->Penetration.Channels.FilterByPredicate([](const FGratiaPenetrationChannel& Channel) { return Channel.bEnabled; }).Num();
    if (Channels.Num() != Defined) { Failure = FString::Printf(TEXT("Penetration channels %d of %d resolve on the mesh"), Channels.Num(), Defined); return false; }
    if (!Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance()))
        UE_LOG(LogGratiaPenetration, Warning, TEXT("PENETRATION_CHECK custom animation class: wall bones are not deformed"));
    // Sizes: the smallest and largest of a default primitive.
    const AGratiaPenetrator* Defaults = GetDefault<AGratiaPenetrator>();
    TArray<GratiaPenetration::FShaft> Shafts;
    for (const int32 Size : {0, Defaults->Sizes.Num() - 1})
    {
        GratiaPenetration::FShaft Shaft;
        if (Defaults->Sizes.IsValidIndex(Size)) { Shaft.Length = Defaults->Sizes[Size].LengthCm; Shaft.Radius = Defaults->Sizes[Size].RadiusCm; }
        Shaft.TipCm = Defaults->TipTaperCm; Shaft.BaseScale = Defaults->BaseRadiusScale; Shaft.Joints = Defaults->JointCount;
        Shafts.Add(Shaft);
    }
    bool bPass = true;
    for (FChannel& Channel : Channels)
    {
        const FGratiaPenetrationChannel* Settings = Definition(Channel);
        if (!UpdateFrame(Channel) || !Settings) { Failure = FString::Printf(TEXT("Penetration channel %s has no valid frame"), *Channel.Name.ToString()); return false; }
        double MaxOffset[2] = {0.0, 0.0}, WorstDepthError = 0.0, WorstTipError = 0.0;
        bool bCapture = true, bRelease = true, bFinite = true, bBounded = true, bGrows = true;
        for (int32 Size = 0; Size < Shafts.Num(); ++Size)
        {
            const GratiaPenetration::FShaft& Shaft = Shafts[Size];
            const double Capture = CaptureRadius(Channel, Shaft);
            // Tip 1 cm before the entrance, straight along the channel: enters.
            bCapture &= GratiaPenetration::CanCapture(Shaft, Channel.Entrance - Channel.Inward * (Shaft.Length + 1.0), Channel.Inward,
                Channel.Entrance, Channel.Inward, Capture, Settings->CaptureAngleDegrees);
            // From the side (90 degrees) it does not.
            const FVector Side = FVector::CrossProduct(Channel.Inward, FVector::UpVector).GetSafeNormal();
            if (!Side.IsNearlyZero())
                bCapture &= !GratiaPenetration::CanCapture(Shaft, Channel.Entrance - Side * (Shaft.Length + 1.0), Side,
                    Channel.Entrance, Channel.Inward, Capture, Settings->CaptureAngleDegrees);
            // Straight insertion: the inserted length is the base's advance, the tip is on the channel axis.
            const double Reach = FMath::Min(Shaft.Length, Channel.Path.Length());
            TArray<FVector> Joints;
            for (int32 Step = 0; Step <= 10; ++Step)
            {
                const double Depth = Reach * Step / 10.0;
                const double Inserted = GratiaPenetration::EngagedJoints(Shaft, Channel.Entrance - Channel.Inward * (Shaft.Length - Depth),
                    Channel.Inward, Channel.Path, Channel.Path.Length(), Joints);
                WorstDepthError = FMath::Max(WorstDepthError, FMath::Abs(Inserted - Depth));
                WorstTipError = FMath::Max(WorstTipError, FVector::Distance(Joints.Last(), Channel.Entrance + Channel.Inward * Depth));
                bFinite &= Joints.Num() == Shaft.Joints && !Joints.ContainsByPredicate([](const FVector& Point) { return Point.ContainsNaN(); });
                for (const FWallBone& Bone : Channel.Bones)
                {
                    const FVector Offset = WallTarget(Channel, Bone, Shaft, Inserted, 50.0);
                    bFinite &= !Offset.ContainsNaN();
                    bBounded &= Offset.Size() <= (Bone.Settings.MaxOffsetCm + Bone.Settings.MaxDragCm) * Channel.Scale + 0.01;
                    MaxOffset[Size] = FMath::Max(MaxOffset[Size], Offset.Size());
                }
            }
            // Pulled back past the release distance: lets go; within the capture distance: stays.
            bRelease &= GratiaPenetration::EngagedJoints(Shaft, Channel.Entrance - Channel.Inward * (Shaft.Length + Capture + 2.0),
                Channel.Inward, Channel.Path, Channel.Path.Length(), Joints) < -(Capture + 1.0);
            bRelease &= GratiaPenetration::EngagedJoints(Shaft, Channel.Entrance - Channel.Inward * (Shaft.Length + 0.5 * Capture),
                Channel.Inward, Channel.Path, Channel.Path.Length(), Joints) >= -(Capture + 1.0);
        }
        // A larger shaft stretches every wall bone at least as far, at the same depth.
        const double Depth = FMath::Min(Shafts[0].Length, Channel.Path.Length()) * 0.6;
        for (const FWallBone& Bone : Channel.Bones)
            bGrows &= WallTarget(Channel, Bone, Shafts.Last(), Depth, 0.0).Size() + 1.0e-4 >= WallTarget(Channel, Bone, Shafts[0], Depth, 0.0).Size();
        // The player's hand (two fingers, flat hand, fist) enters straight on; inside it stays finite and bounded,
        // and a fist opens the walls further than two fingers.
        bool bHands = true;
        double FingersOpen = 0.0, FistOpen = 0.0;
        for (const GratiaPenetration::EHandShape Shape : {GratiaPenetration::EHandShape::Fingers, GratiaPenetration::EHandShape::Hand, GratiaPenetration::EHandShape::Fist})
        {
            const GratiaPenetration::FShaft Hand = GratiaPenetration::HandShaft(Shape);
            bHands &= GratiaPenetration::CanCapture(Hand, Channel.Entrance - Channel.Inward * (Hand.Length + 1.0), Channel.Inward,
                Channel.Entrance, Channel.Inward, CaptureRadius(Channel, Hand), Settings->CaptureAngleDegrees);
            const double Reach = FMath::Min(Hand.Length, Channel.Path.Length()) * 0.7;
            TArray<FVector> Joints;
            const double Inserted = GratiaPenetration::EngagedJoints(Hand, Channel.Entrance - Channel.Inward * (Hand.Length - Reach),
                Channel.Inward, Channel.Path, Channel.Path.Length(), Joints);
            bHands &= FMath::Abs(Inserted - Reach) < 0.25 && !Joints.ContainsByPredicate([](const FVector& Point) { return Point.ContainsNaN(); });
            double Open = 0.0;
            for (const FWallBone& Bone : Channel.Bones)
            {
                const FVector Offset = WallTarget(Channel, Bone, Hand, FMath::Min(4.0, Inserted), 0.0);
                bHands &= !Offset.ContainsNaN() && Offset.Size() <= (Bone.Settings.MaxOffsetCm + Bone.Settings.MaxDragCm) * Channel.Scale + 0.01;
                Open = FMath::Max(Open, Offset.Size());
            }
            if (Shape == GratiaPenetration::EHandShape::Fingers) FingersOpen = Open;
            if (Shape == GratiaPenetration::EHandShape::Fist) FistOpen = Open;
        }
        bHands &= Channel.Bones.IsEmpty() || FistOpen > FingersOpen;
        const bool bChannel = bCapture && bRelease && bFinite && bBounded && bGrows && bHands && WorstDepthError < 0.25 && WorstTipError < 0.5
            && (Channel.Bones.IsEmpty() || MaxOffset[1] > 0.0);
        UE_LOG(LogGratiaPenetration, Display,
            TEXT("PENETRATION_CHECK channel=%s depth=%.1fcm wall_bones=%d capture=%d release=%d depth_err=%.3fcm tip_err=%.3fcm small_max=%.2fcm large_max=%.2fcm bounded=%d grows=%d hands=%d fingers_open=%.2fcm fist_open=%.2fcm %s"),
            *Channel.Name.ToString(), Channel.Path.Length(), Channel.Bones.Num(), bCapture ? 1 : 0, bRelease ? 1 : 0, WorstDepthError, WorstTipError,
            MaxOffset[0], MaxOffset[1], bBounded ? 1 : 0, bGrows ? 1 : 0, bHands ? 1 : 0, FingersOpen, FistOpen, bChannel ? TEXT("PASS") : TEXT("FAIL"));
        if (!bChannel && Failure.IsEmpty()) Failure = FString::Printf(TEXT("Penetration channel %s failed capture/insertion/wall checks"), *Channel.Name.ToString());
        bPass &= bChannel;
    }
    // End to end with the hands: a held fist goes into the first channel through the candidates and opening the
    // hand lets it go (a hand does not stay on the body like a released primitive); then two hands of three fingers
    // share one channel side by side (both inside, apart, the walls open further than for one), and with a second
    // channel each hand takes its own.
    if (UWorld* World = GetWorld(); World && !Channels.IsEmpty() && Channels[0].bFrame)
    {
        FActorSpawnParameters Parameters;
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AGratiaPenetrator* Hands[2] = {World->SpawnActor<AGratiaPenetrator>(AGratiaPenetrator::StaticClass(), FTransform::Identity, Parameters),
            World->SpawnActor<AGratiaPenetrator>(AGratiaPenetrator::StaticClass(), FTransform::Identity, Parameters)};
        bool bFistEnters = false, bFistLeaves = false, bShared = false, bApart = false, bWider = false, bTwoChannels = Channels.Num() < 2;
        double FistDepth = 0.0, OneOpen = 0.0, TwoOpen = 0.0;
        if (Hands[0] && Hands[1])
        {
            const TArray<TWeakObjectPtr<AGratiaPenetrator>> Saved = Candidates;
            SetCandidates({Hands[0], Hands[1]});
            auto Place = [](AGratiaPenetrator* Hand, const FChannel& Channel, const GratiaPenetration::FShaft& Shape, double TipDepth, const FVector& Side)
            {
                Hand->SetBase(FTransform(FRotationMatrix::MakeFromX(Channel.Inward).ToQuat(), Channel.Entrance + Side - Channel.Inward * (Shape.Length - TipDepth)));
            };
            const auto Step = [this]() { Solve(1.0f / 90.0f); };
            for (AGratiaPenetrator* Hand : Hands) { Hand->SetActorHiddenInGame(true); Hand->bAnchorWhenReleased = false; Hand->SetHeld(INDEX_NONE); }
            const FChannel& First = Channels[0];
            const GratiaPenetration::FShaft Fist = GratiaPenetration::HandShaft(GratiaPenetration::EHandShape::Fist);
            Hands[0]->SetShape(TEXT("fist"), Fist);
            Hands[0]->SetHeld(1);
            // Tip 1 cm before the entrance (captured), then pushed 3 cm in.
            Place(Hands[0], First, Fist, -1.0, FVector::ZeroVector); Step();
            Place(Hands[0], First, Fist, 3.0, FVector::ZeroVector); Step();
            FVector Entrance, Inward;
            double Depth = 0.0;
            bFistEnters = IsEngaged(Hands[0]) && GetEngagedFrame(Hands[0], Entrance, Inward, FistDepth, Depth) && FMath::Abs(FistDepth - 3.0) < 0.5;
            Hands[0]->SetHeld(INDEX_NONE); Step();
            bFistLeaves = GetEngagementCount() == 0;
            // Two hands of three fingers in the first channel, entering one after the other from either side.
            const GratiaPenetration::FShaft Fingers = GratiaPenetration::HandShaft(GratiaPenetration::EHandShape::Fingers);
            const FVector Side = FVector::CrossProduct(First.Inward, FVector::UpVector).GetSafeNormal() * 1.5;
            for (AGratiaPenetrator* Hand : Hands) Hand->SetShape(TEXT("fingers"), Fingers);
            Hands[0]->SetHeld(0);
            Place(Hands[0], First, Fingers, -1.0, -Side); Step();
            Place(Hands[0], First, Fingers, 5.0, -Side);
            for (int32 Frame = 0; Frame < 60; ++Frame) Step();
            OneOpen = GetMaxWallOffsetCm();
            Hands[1]->SetHeld(1);
            Place(Hands[1], First, Fingers, -1.0, Side); Step();
            Place(Hands[1], First, Fingers, 5.0, Side);
            for (int32 Frame = 0; Frame < 60; ++Frame) Step();
            TwoOpen = GetMaxWallOffsetCm();
            FVector EntranceA, EntranceB;
            double DepthA = 0.0, DepthB = 0.0;
            bShared = GetEngagementCount() == 2 && GetEngagedFrame(Hands[0], EntranceA, Inward, DepthA, Depth) && GetEngagedFrame(Hands[1], EntranceB, Inward, DepthB, Depth);
            bApart = bShared && FVector::Distance(EntranceA, EntranceB) > Fingers.Radius;
            bWider = First.Bones.IsEmpty() || TwoOpen > OneOpen;
            for (AGratiaPenetrator* Hand : Hands) Hand->SetHeld(INDEX_NONE);
            Step();
            // A second channel: each hand in its own.
            if (Channels.Num() >= 2 && Channels[1].bFrame)
            {
                Hands[0]->SetHeld(0); Hands[1]->SetHeld(1);
                Place(Hands[0], Channels[0], Fingers, -1.0, FVector::ZeroVector);
                Place(Hands[1], Channels[1], Fingers, -1.0, FVector::ZeroVector);
                Step();
                bTwoChannels = GetEngagementCount() == 2 && GetEngagedChannel() != NAME_None
                    && Engagements[0].Channel != Engagements[1].Channel;
                for (AGratiaPenetrator* Hand : Hands) Hand->SetHeld(INDEX_NONE);
                Step();
            }
            Candidates = Saved;
            if (Character->Interaction) Character->Interaction->ResetState();
        }
        for (AGratiaPenetrator* Hand : Hands) if (Hand) Hand->Destroy();
        const bool bHands = bFistEnters && bFistLeaves && bShared && bApart && bWider && bTwoChannels;
        UE_LOG(LogGratiaPenetration, Display,
            TEXT("PENETRATION_CHECK hands channel=%s fist_enters=%d depth=%.2fcm leaves_on_open=%d two_in_one=%d apart=%d open_one=%.2fcm open_two=%.2fcm two_channels=%d %s"),
            *Channels[0].Name.ToString(), bFistEnters ? 1 : 0, FistDepth, bFistLeaves ? 1 : 0, bShared ? 1 : 0, bApart ? 1 : 0, OneOpen, TwoOpen,
            bTwoChannels ? 1 : 0, bHands ? TEXT("PASS") : TEXT("FAIL"));
        if (!bHands && Failure.IsEmpty()) Failure = TEXT("Hands do not enter, leave, share a channel or take two channels as expected");
        bPass &= bHands;
    }
    ResetPenetration();
    return bPass;
}
