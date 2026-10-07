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
    Release(TEXT("shaft changed"));
    Penetrator = Shaft;
    bShaftWasHeld = false;
    Shown.Reset(); Residual.Reset();
}

void UGratiaPenetration::Release(const TCHAR* Reason)
{
    if (Channels.IsValidIndex(Engaged.Channel))
        UE_LOG(LogGratiaPenetration, Display, TEXT("PENETRATION_EXIT channel=%s reason=%s depth=%.1fcm"),
            *Channels[Engaged.Channel].Name.ToString(), Reason, FMath::Max(0.0, Engaged.Inserted));
    Engaged = FEngagement();
}

void UGratiaPenetration::ResetPenetration()
{
    Release(TEXT("reset"));
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
    const double Out = GratiaPenetration::BoneOffset(Opening, Bone.Settings.StartOpeningCm * Scale, Bone.Settings.Response, Bone.Settings.MaxOffsetCm * Scale);
    const double MaxDrag = Bone.Settings.MaxDragCm * Scale;
    const double Drag = Opening > 0.0 ? FMath::Clamp(Velocity * Bone.Settings.DragSeconds, -MaxDrag, MaxDrag) : 0.0;
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

void UGratiaPenetration::UpdateWalls(const GratiaPenetration::FShaft& Shaft, float Delta)
{
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get();
    const double Alpha = 1.0 - FMath::Exp(-FMath::Max(1.0f, Profile->Penetration.WallFollowSpeed) * Delta);
    Engaged.Opening = 0.0;
    for (int32 Index = 0; Index < Channels.Num(); ++Index)
    {
        FChannel& Channel = Channels[Index];
        const FGratiaPenetrationChannel* Settings = Definition(Channel);
        const bool bActive = Index == Engaged.Channel && Settings && Channel.bFrame;
        double Opening = 0.0;
        if (bActive)
        {
            Opening = GratiaPenetration::WallOpening(Shaft, FMath::Clamp(Engaged.Inserted, 0.0, Channel.Path.Length()), 0.0,
                Settings->RestRadiusCm * Channel.Scale, Settings->WallFalloffCm * Channel.Scale);
            Engaged.Opening = Opening;
        }
        for (FWallBone& Bone : Channel.Bones)
        {
            Bone.Target = bActive ? WallTarget(Channel, Bone, Shaft, Engaged.Inserted, Engaged.Velocity) : FVector::ZeroVector;
            Bone.Offset = FMath::Lerp(Bone.Offset, Bone.Target, Alpha);
            if (Bone.Offset.ContainsNaN()) Bone.Offset = FVector::ZeroVector;
        }
        if (Channel.Morph.IsNone() || !Settings) continue;
        const float Weight = bActive ? FMath::Clamp(float(Opening / (Settings->MorphFullOpeningCm * Channel.Scale)), 0.0f, 1.0f) : 0.0f;
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
    AGratiaPenetrator* Shaft = Penetrator.Get();
    if (!IsEnabled())
    {
        if (bWasEnabled) { ResetPenetration(); Channels.Reset(); ResolvedProfile = nullptr; ResolvedMesh = nullptr; }
        bWasEnabled = false;
        if (Shaft) { Shaft->HapticAmplitude = Shaft->HapticFrequency = 0.0f; Shaft->SetJoints({}); }
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
    GratiaPenetration::FShaft Geometry;
    if (!Shaft)
    {
        if (Engaged.Channel != INDEX_NONE) Release(TEXT("shaft removed"));
    }
    else
    {
        Geometry = Shaft->GetShaft();
        // Let go inside: the base stays where it is on the body and moves with it.
        if (Shaft->IsHeld()) Engaged.bAnchored = false;
        else if (bShaftWasHeld && Channels.IsValidIndex(Engaged.Channel))
        {
            Engaged.bAnchored = true;
            Engaged.AnchoredBase = Shaft->GetBase().GetRelativeTransform(GratiaNoScale(Channels[Engaged.Channel].AnchorWorld));
        }
        bShaftWasHeld = Shaft->IsHeld();
        if (Engaged.bAnchored && Channels.IsValidIndex(Engaged.Channel))
            Shaft->SetBase(Engaged.AnchoredBase * GratiaNoScale(Channels[Engaged.Channel].AnchorWorld));
        const FTransform Base = Shaft->GetBase();
        const FVector Direction = Base.GetRotation().GetForwardVector();
        // Only a held shaft enters; its tip has to find the entrance.
        if (Engaged.Channel == INDEX_NONE && Shaft->IsHeld())
            for (int32 Index = 0; Index < Channels.Num(); ++Index)
            {
                const FChannel& Channel = Channels[Index];
                const FGratiaPenetrationChannel* Source = Definition(Channel);
                if (!Channel.bFrame || !Source) continue;
                if (!GratiaPenetration::CanCapture(Geometry, Base.GetLocation(), Direction, Channel.Entrance, Channel.Inward,
                    CaptureRadius(Channel, Geometry), Source->CaptureAngleDegrees)) continue;
                Engaged = FEngagement();
                Engaged.Channel = Index;
                Engaged.Pulse = 0.08;
                UE_LOG(LogGratiaPenetration, Display, TEXT("PENETRATION_ENTER channel=%s size=%s hand=%d"), *Channel.Name.ToString(), *Shaft->GetSizeLabel(), Shaft->GetHeldHand());
                break;
            }
        TArray<FVector> Target;
        if (Channels.IsValidIndex(Engaged.Channel))
        {
            const FChannel& Channel = Channels[Engaged.Channel];
            const FGratiaPenetrationChannel* Source = Definition(Channel);
            const double Inserted = Channel.bFrame && Source
                ? GratiaPenetration::EngagedJoints(Geometry, Base.GetLocation(), Direction, Channel.Path, Channel.Path.Length(), Target) : -1.0e9;
            const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, Channel.Inward), -1.0, 1.0)));
            // The tip may hover before the entrance it was captured at; it lets go a little further out.
            if (!Source || !Channel.bFrame || Inserted < -(CaptureRadius(Channel, Geometry) + 1.0) || Angle > Source->ReleaseAngleDegrees)
            {
                Release(!Source || !Channel.bFrame ? TEXT("channel unavailable") : Inserted < 0.0 ? TEXT("pulled out") : TEXT("bent away"));
                Target.Reset();
            }
            else
            {
                const double Moved = Engaged.bFresh ? 0.0 : Inserted - Engaged.Inserted;
                const double Speed = Step > 0.0f ? Moved / Step : 0.0;
                Engaged.Velocity = FMath::Lerp(Engaged.Velocity, Speed, 1.0 - FMath::Exp(-15.0 * Step));
                Engaged.Inserted = Inserted;
                Engaged.Travel += FMath::Abs(Moved);
                const int32 Hand = Shaft->GetHeldHand();
                if (Engaged.bFresh || Engaged.Travel >= Settings.ReactionTravelCm * Channel.Scale)
                {
                    Engaged.Travel = 0.0;
                    if (Character->Interaction) Character->Interaction->ExternalReaction(Channel.Name, Hand, float(FMath::Abs(Engaged.Velocity)));
                }
                Engaged.bFresh = false;
            }
        }
        if (Target.IsEmpty())
        {
            GratiaPenetration::StraightJoints(Geometry, Base.GetLocation(), Direction, Target);
            if (Settings.bShaftBodyCollision) CollideWithBody(Geometry, Target);
        }
        // Entering and leaving switch the shape: the jump of the joints eases out (~60 ms).
        const bool bEngagedNow = Engaged.Channel != INDEX_NONE;
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
        Shaft->SetJoints(Shown);
    }
    UpdateWalls(Geometry, Step);
    PushToAnimation();
    // Feedback: a short pulse on entering, then vibration with the insertion speed and the stretch.
    const bool bInside = Channels.IsValidIndex(Engaged.Channel);
    const float Stretch = bInside ? FMath::Clamp(float(Engaged.Opening / FMath::Max(0.5, Geometry.Radius)), 0.0f, 1.0f) : 0.0f;
    if (Shaft)
    {
        float Amplitude = 0.0f, Frequency = 0.0f;
        if (bInside && Shaft->IsHeld())
        {
            const float Speed = FMath::Clamp(float(FMath::Abs(Engaged.Velocity)) / FMath::Max(1.0f, Settings.HapticFullSpeed), 0.0f, 1.0f);
            Amplitude = Settings.HapticBase + (Settings.HapticMax - Settings.HapticBase) * FMath::Max(Speed, 0.35f * Stretch);
            Frequency = FMath::Lerp(0.2f, 0.7f, Speed);
            if (Engaged.Pulse > 0.0) { Amplitude = FMath::Max(Amplitude, Settings.HapticCapturePulse); Frequency = 0.4f; }
        }
        Shaft->HapticAmplitude = FMath::Clamp(Amplitude, 0.0f, 1.0f);
        Shaft->HapticFrequency = FMath::Clamp(Frequency, 0.0f, 1.0f);
    }
    Engaged.Pulse = FMath::Max(0.0, Engaged.Pulse - Step);
    if (bInside && Character->Interaction) Character->Interaction->SetExternalHold(Settings.HoldReactionWeight * (0.6f + 0.4f * Stretch));
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if (Now >= NextDiagnosticTime && (bInside || Shaft))
    {
        NextDiagnosticTime = Now + 2.0;
        UE_LOG(LogGratiaPenetration, Display, TEXT("PENETRATION %s"), *GetDiagnostics());
    }
}

FString UGratiaPenetration::GetDiagnostics() const
{
    const AGratiaPenetrator* Shaft = Penetrator.Get();
    const auto* Anim = Character.IsValid() && Character->CharacterMesh ? Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance()) : nullptr;
    return FString::Printf(TEXT("channels=%d shaft=%s held=%d engaged=%s depth=%.1fcm opening=%.2fcm speed=%.1fcm/s wall_max=%.2fcm applied=%d anchored=%d state=%s"),
        Channels.Num(), Shaft ? *Shaft->GetSizeLabel() : TEXT("none"), Shaft ? Shaft->GetHeldHand() : INDEX_NONE,
        *GetEngagedChannel().ToString(), GetDepthCm(), Engaged.Opening, Engaged.Velocity, GetMaxWallOffsetCm(),
        Anim ? Anim->GetAppliedPenetrationBones() : -1, Engaged.bAnchored ? 1 : 0, IsEnabled() ? TEXT("running") : TEXT("disabled"));
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
        const bool bChannel = bCapture && bRelease && bFinite && bBounded && bGrows && WorstDepthError < 0.25 && WorstTipError < 0.5
            && (Channel.Bones.IsEmpty() || MaxOffset[1] > 0.0);
        UE_LOG(LogGratiaPenetration, Display,
            TEXT("PENETRATION_CHECK channel=%s depth=%.1fcm wall_bones=%d capture=%d release=%d depth_err=%.3fcm tip_err=%.3fcm small_max=%.2fcm large_max=%.2fcm bounded=%d grows=%d %s"),
            *Channel.Name.ToString(), Channel.Path.Length(), Channel.Bones.Num(), bCapture ? 1 : 0, bRelease ? 1 : 0, WorstDepthError, WorstTipError,
            MaxOffset[0], MaxOffset[1], bBounded ? 1 : 0, bGrows ? 1 : 0, bChannel ? TEXT("PASS") : TEXT("FAIL"));
        if (!bChannel && Failure.IsEmpty()) Failure = FString::Printf(TEXT("Penetration channel %s failed capture/insertion/wall checks"), *Channel.Name.ToString());
        bPass &= bChannel;
    }
    ResetPenetration();
    return bPass;
}
