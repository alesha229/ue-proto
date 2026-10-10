#include "GratiaProceduralFace.h"
#include "GratiaAnimInstance.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "MotionControllerComponent.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaFace, Log, All);

// Named (not anonymous) so unity builds do not collide with other files.
namespace GratiaFaceLocal
{

// Semantic morph keys (profile SemanticMorphs). Look*/Blink* are driven separately from the channels.
const FName LookLeft(TEXT("LookLeft")), LookRight(TEXT("LookRight")), LookUp(TEXT("LookUp")), LookDown(TEXT("LookDown"));
const FName BlinkLeft(TEXT("BlinkLeft")), BlinkRight(TEXT("BlinkRight"));
const FName BrowsUp(TEXT("BrowsUp")), BrowsDown(TEXT("BrowsDown")), BrowsWorry(TEXT("BrowsWorry")), BrowsAngry(TEXT("BrowsAngry"));
const FName Surprise(TEXT("Surprise")), EyesShrink(TEXT("EyesShrink")), EyesSmug(TEXT("EyesSmug"));
const FName WinkLeft(TEXT("WinkLeft")), WinkRight(TEXT("WinkRight"));
const FName Smile(TEXT("Smile")), MouthOpen(TEXT("MouthOpen")), CornerLeft(TEXT("MouthCornerLeftUp")), CornerRight(TEXT("MouthCornerRightUp"));
const FName MouthPuff(TEXT("MouthPuff")), LipBite(TEXT("LipBite")), MouthPout(TEXT("MouthPout")), MouthNervous(TEXT("MouthNervous"));

enum class EChannelKind : uint8 { Brow, Eye, Mouth };
struct FChannelDefinition { FName Semantic; EChannelKind Kind; };
const FChannelDefinition ChannelDefinitions[] = {
    { BrowsUp, EChannelKind::Brow }, { BrowsDown, EChannelKind::Brow }, { BrowsWorry, EChannelKind::Brow }, { BrowsAngry, EChannelKind::Brow },
    { Surprise, EChannelKind::Eye }, { EyesShrink, EChannelKind::Eye }, { EyesSmug, EChannelKind::Eye },
    { WinkLeft, EChannelKind::Eye }, { WinkRight, EChannelKind::Eye },
    { Smile, EChannelKind::Mouth }, { MouthOpen, EChannelKind::Mouth }, { CornerLeft, EChannelKind::Mouth }, { CornerRight, EChannelKind::Mouth },
    { MouthPuff, EChannelKind::Mouth }, { LipBite, EChannelKind::Mouth }, { MouthPout, EChannelKind::Mouth }, { MouthNervous, EChannelKind::Mouth },
};
// Morphs UGratiaInteraction writes as its facial reaction; the face reads them back as targets.
bool IsInteractionMorph(FName Semantic) { return Semantic == Smile || Semantic == BrowsUp || Semantic == Surprise || Semantic == MouthOpen; }

// Mood index of UGratiaInteraction: 0 Calm (kuudere), 1 Cheerful (deredere), 2 Reserved (tsundere).
constexpr int32 MoodKuudere = 0, MoodDeredere = 1, MoodTsundere = 2;

FQuat RestComponentRotation(const FReferenceSkeleton& Ref, int32 Index)
{
    FTransform Rest = Ref.GetRefBonePose()[Index];
    for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
        Rest = Rest * Ref.GetRefBonePose()[Parent];
    return Rest.GetRotation();
}

float Saturate(float Value) { return FMath::Clamp(FMath::IsFinite(Value) ? Value : 0.0f, 0.0f, 1.0f); }
}
using namespace GratiaFaceLocal;
using namespace GratiaFaceMath;

UGratiaProceduralFace::UGratiaProceduralFace()
{
    PrimaryComponentTick.bCanEverTick = true;
    // After UGratiaInteraction (PostUpdateWork), which writes the reaction morphs this layer refines.
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGratiaProceduralFace::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    Random.Initialize(int32(GetUniqueID() * 2654435761u));
    if (Character.IsValid())
    {
        // The actor tick runs the legacy blink; the face overrides it afterwards.
        AddTickPrerequisiteActor(Character.Get());
        if (Character->Interaction) AddTickPrerequisiteComponent(Character->Interaction);
    }
}

void UGratiaProceduralFace::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Character.IsValid() && Character->CharacterMesh)
        if (UGratiaAnimInstance* Instance = Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance()))
        {
            Instance->FaceBoneDeltas.Reset();
            Instance->bFaceDrivesHead = false;
        }
    Super::EndPlay(Reason);
}

FName UGratiaProceduralFace::Morph(FName Semantic) const
{
    const FName* Found = Resolved.Find(Semantic);
    return Found ? *Found : NAME_None;
}

void UGratiaProceduralFace::Bind()
{
    Missing.Reset(); Resolved.Reset(); Channels.Reset(); OwnedMorphs.Reset(); LastScalars.Reset();
    for (FSegment& Segment : Segments) Segment = FSegment();
    const UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    USkeletalMesh* Mesh = Character.IsValid() && Character->CharacterMesh ? Character->CharacterMesh->GetSkeletalMeshAsset() : nullptr;
    BoundProfile = Profile; BoundMesh = Mesh;
    if (!Profile || !Mesh) return;

    auto Resolve = [&](FName Semantic)
    {
        const FName Name = Profile->ResolveMorph(Semantic);
        if (Name.IsNone() || !Mesh->FindMorphTarget(Name)) { Missing.Add(TEXT("morph ") + Semantic.ToString()); return; }
        Resolved.Add(Semantic, Name);
    };
    for (const FName Semantic : { LookLeft, LookRight, LookUp, LookDown, BlinkLeft, BlinkRight }) Resolve(Semantic);
    for (const GratiaFaceLocal::FChannelDefinition& Definition : GratiaFaceLocal::ChannelDefinitions)
    {
        Resolve(Definition.Semantic);
        if (Morph(Definition.Semantic).IsNone()) continue;
        FMorphChannel& Channel = Channels.AddDefaulted_GetRef();
        Channel.Semantic = Definition.Semantic; Channel.Morph = Morph(Definition.Semantic);
        Channel.Kind = uint8(Definition.Kind);
    }

    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    const FVector Forward = Profile->ForwardAxis.GetSafeNormal(), Up = Profile->UpAxis.GetSafeNormal();
    const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
    const FName SegmentSemantics[3] = { TEXT("Head"), TEXT("Neck"), TEXT("UpperChest") };
    for (int32 I = 0; I < 3; ++I)
    {
        FSegment& Segment = Segments[I];
        Segment.Bone = Profile->ResolveBone(SegmentSemantics[I]);
        const int32 Index = Segment.Bone.IsNone() ? INDEX_NONE : Ref.FindBoneIndex(Segment.Bone);
        if (Index == INDEX_NONE) { Missing.Add(TEXT("bone ") + SegmentSemantics[I].ToString()); continue; }
        // Local axes from the imported rest frame, as UGratiaAnimInstance does for the head.
        const FQuat Rest = RestComponentRotation(Ref, Index);
        Segment.YawAxis = Rest.Inverse().RotateVector(Up).GetSafeNormal();
        Segment.NodAxis = Rest.Inverse().RotateVector(-Right).GetSafeNormal();
        Segment.bValid = true;
        if (I == 0)
        {
            HeadForwardLocal = Rest.Inverse().RotateVector(Forward).GetSafeNormal();
            HeadUpLocal = Rest.Inverse().RotateVector(Up).GetSafeNormal();
            const FVector A = HeadUpLocal.GetAbs();
            HeadVerticalAxis = A.X >= A.Y && A.X >= A.Z ? 0 : A.Y >= A.Z ? 1 : 2;
        }
    }
    const FVector Shares3 = Profile->Face.GazeShares;
    GratiaFaceMath::GazeShares(float(Shares3.X), float(Shares3.Y), float(Shares3.Z), Segments[1].bValid, Segments[2].bValid, Shares);
    ResetFace();
    if (!Missing.IsEmpty())
        UE_LOG(LogGratiaFace, Display, TEXT("Procedural face %s: missing %s; dependent features stay off."),
            *Profile->GetName(), *FString::Join(Missing, TEXT(", ")));
    UE_LOG(LogGratiaFace, Display, TEXT("Procedural face bound to %s: %d expression channels, head=%s neck=%s chest=%s"),
        *Profile->GetName(), Channels.Num(), *Segments[0].Bone.ToString(), *Segments[1].Bone.ToString(), *Segments[2].Bone.ToString());
}

void UGratiaProceduralFace::SetMorph(FName Semantic, float Weight)
{
    const FName Name = Morph(Semantic);
    if (Name.IsNone() || !Character.IsValid() || !Character->CharacterMesh) return;
    Character->CharacterMesh->SetMorphTarget(Name, FMath::Clamp(FMath::IsFinite(Weight) ? Weight : 0.0f, 0.0f, 1.0f), false);
    OwnedMorphs.Add(Semantic);
}

void UGratiaProceduralFace::ReleaseMorph(FName Semantic)
{
    if (!OwnedMorphs.Contains(Semantic)) return;
    OwnedMorphs.Remove(Semantic);
    const FName Name = Morph(Semantic);
    // Removing the override lets clip curves (performances, authored cues) show again.
    if (!Name.IsNone() && Character.IsValid() && Character->CharacterMesh) Character->CharacterMesh->SetMorphTarget(Name, 0.0f, true);
}

void UGratiaProceduralFace::ReleaseAll()
{
    for (const FName Semantic : OwnedMorphs.Array()) ReleaseMorph(Semantic);
}

void UGratiaProceduralFace::SetScalar(FName Name, float Value)
{
    if (!Character.IsValid() || !Character->CharacterMesh || !FMath::IsFinite(Value)) return;
    float* Last = LastScalars.Find(Name);
    if (Last && FMath::Abs(*Last - Value) < 0.003f) return;
    LastScalars.Add(Name, Value);
    Character->CharacterMesh->SetScalarParameterValueOnMaterials(Name, Value);
}

float UGratiaProceduralFace::ReadBodyMotion(FName Property, float Fallback) const
{
    // UGratiaBodyMotion (body thread) owns excitement, stamina and archetype outputs. Read by name so the
    // face builds and works without it; a missing component or property falls back.
    const AActor* Owner = GetOwner();
    if (!Owner) return Fallback;
    for (const UActorComponent* Component : Owner->GetComponents())
    {
        if (!Component || Component->GetClass()->GetName() != TEXT("GratiaBodyMotion")) continue;
        if (const FFloatProperty* Found = FindFProperty<FFloatProperty>(Component->GetClass(), Property))
        {
            const float Value = Found->GetPropertyValue_InContainer(Component);
            return FMath::IsFinite(Value) ? Value : Fallback;
        }
        return Fallback;
    }
    return Fallback;
}

void UGratiaProceduralFace::TriggerEmotion(EGratiaFaceEmotion NewEmotion, float Strength, float Seconds)
{
    PendingEmotion = NewEmotion;
    EmotionStrength = Saturate(Strength);
    EmotionLeft = Seconds > 0.0f ? Seconds : TNumericLimits<float>::Max();
    if (NewEmotion == EGratiaFaceEmotion::Embarrassed) Embarrassment = FMath::Max(Embarrassment, 0.6f * EmotionStrength);
    if (NewEmotion == EGratiaFaceEmotion::Pout && !bAverting) TriggerGazeAversion();
}

void UGratiaProceduralFace::TriggerReflexSquint(float Strength)
{
    const UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    const FVector2D Range = Profile ? Profile->Face.ReflexSeconds : FVector2D(0.3, 0.5);
    ReflexLeft = Random.FRandRange(float(Range.X), float(FMath::Max(Range.X, Range.Y)));
    ReflexStrength = Saturate(Strength);
    ReflexCooldown = ReflexLeft + (Profile ? Profile->Face.ReflexCooldownSeconds : 0.8f);
}

void UGratiaProceduralFace::TriggerGazeAversion()
{
    const UGratiaCharacterProfile* Profile = Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
    const FVector2D Range = Profile ? Profile->Face.AversionSeconds : FVector2D(1.2, 2.2);
    AversionLeft = Random.FRandRange(float(Range.X), float(FMath::Max(Range.X, Range.Y)));
    AversionSide = Random.FRand() < 0.5f ? -1.0f : 1.0f;
    ShyLeft = 0.0f;
    bAverting = true;
    Embarrassment = FMath::Min(1.0f, Embarrassment + 0.35f);
}

void UGratiaProceduralFace::ResetFace()
{
    ReleaseAll();
    for (FMorphChannel& Channel : Channels) { Channel.Spring.Reset(); Channel.LastWritten = -1.0f; Channel.InteractionTarget = 0.0f; }
    for (FSegment& Segment : Segments) { Segment.Yaw.Reset(); Segment.Pitch.Reset(); }
    EyeYaw.Reset(); EyePitch.Reset(); Squeeze.Reset(); Squash.Reset();
    JawJiggle.Reset(); CheekJiggle.Reset(); SideJiggle.Reset();
    BlinkElapsed = -1.0f; UntilBlink = Random.FRandRange(0.8f, 2.5f); bDoubleBlinkPending = false;
    ReflexLeft = ReflexCooldown = 0.0f; AversionLeft = ShyLeft = AversionCooldown = 0.0f; bAverting = false;
    HeadGoalYaw = HeadGoalPitch = 0.0f; bHasLastHead = bHasLastView = false; bHasLastHand[0] = bHasLastHand[1] = false;
    PendingEmotion = Emotion = EGratiaFaceEmotion::Neutral; EmotionLeft = EmotionStrength = 0.0f;
    if (Character.IsValid() && Character->Interaction) LastReactionSerial = Character->Interaction->ReactionSerial;
}

void UGratiaProceduralFace::UpdateLightLevel(float Dt)
{
    if (LightLevelOverride >= 0.0f) { LightLevel = Saturate(LightLevelOverride); return; }
    UntilLightProbe -= Dt;
    if (UntilLightProbe > 0.0f) return;
    UntilLightProbe = 0.5f;
    // Rough exposure at the face: local lights by distance falloff, plus directional and sky light.
    // Mapped to 0..1 with a soft knee; a scene preset can set LightLevelOverride instead.
    const UWorld* World = GetWorld();
    double Sum = 0.0;
    for (TObjectIterator<ULightComponent> It; It; ++It)
    {
        const ULightComponent* Light = *It;
        if (!Light || Light->GetWorld() != World || !Light->IsVisible() || !Light->bAffectsWorld) continue;
        const double Brightness = FMath::Max(0.0f, Light->ComputeLightBrightness());
        if (const ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light))
        {
            const double Radius = FMath::Max(1.0f, Local->AttenuationRadius);
            const double Distance = FVector::Distance(Local->GetComponentLocation(), LastEyeCenter);
            if (Distance >= Radius) continue;
            Sum += Brightness * FMath::Square(1.0 - Distance / Radius) / (1.0 + FMath::Square(Distance / 100.0));
        }
        else Sum += Brightness * 0.5;
    }
    for (TObjectIterator<USkyLightComponent> It; It; ++It)
        if (*It && It->GetWorld() == World && It->IsVisible()) Sum += It->Intensity * 2.0;
    LightLevel = float(1.0 - FMath::Exp(-Sum / 20.0));
}

void UGratiaProceduralFace::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);
    if (!Character.IsValid()) Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    if (!Character.IsValid() || !Character->CharacterMesh || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get();
    USkeletalMesh* Mesh = Character->CharacterMesh->GetSkeletalMeshAsset();
    if (!Profile || !Mesh) return;
    if (BoundProfile.Get() != Profile || BoundMesh.Get() != Mesh) Bind();
    const FGratiaFaceSettings& S = Profile->Face;
    if (!S.bEnabled)
    {
        if (!OwnedMorphs.IsEmpty()) ReleaseAll();
        PushBoneDeltas(false, false);
        return;
    }
    const float Dt = FMath::Min(DeltaSeconds, 0.05f);
    Time += Dt;
    const uint8 Pose = uint8(Character->PreviewPose);
    if (Pose != LastPose) { LastPose = Pose; ResetFace(); }

    const UGratiaInteraction* Interaction = Character->Interaction;
    const bool bIdle = Character->IsIdlePreview();
    const UGratiaAnimInstance* Animation = Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance());
    const bool bCue = Profile->bAuthoredReactionFacialCurves && Animation && Animation->IsReactionCuePlaying();

    // Excitement: body motion owns it; Interaction's reaction weight is the fallback; ExternalExcitement the floor.
    const float Fallback = Interaction ? 0.6f * Interaction->Reaction : 0.0f;
    const float TargetExcitement = FMath::Max(Saturate(ExternalExcitement), Saturate(ReadBodyMotion(TEXT("Excitement"), Fallback)));
    Excitement = FMath::FInterpTo(Excitement, TargetExcitement, Dt, 2.0f);

    // Contact reactions select a snap emotion by mood (archetype) and hand speed.
    if (Interaction && Interaction->ReactionSerial != LastReactionSerial)
    {
        LastReactionSerial = Interaction->ReactionSerial;
        if (Interaction->ReactionSerial != 0)
        {
            const float Speed = Interaction->LastReactionSpeed;
            if (Speed > 0.0f && Speed < S.GoosebumpStrokeSpeedCmPerSecond) Goosebumps = 1.0f;
            if (Interaction->Impulse > 0.6f || Speed > Profile->ContactSettings.StrongReactionSpeedCmPerSecond)
                TriggerEmotion(EGratiaFaceEmotion::Surprise, 1.0f, 0.9f);
            else if (Interaction->Mood == MoodTsundere) TriggerEmotion(EGratiaFaceEmotion::Pout, 0.8f, 1.6f);
            else if (Interaction->Mood == MoodDeredere) TriggerEmotion(EGratiaFaceEmotion::Happy, 0.8f, 1.8f);
            else TriggerEmotion(EGratiaFaceEmotion::Embarrassed, 0.5f, 1.4f);
            Embarrassment = FMath::Min(1.0f, Embarrassment + (Interaction->Mood == MoodKuudere ? 0.08f : 0.2f));
        }
    }
    if (Excitement > S.HeartPupilThreshold && PendingEmotion == EGratiaFaceEmotion::Neutral)
        TriggerEmotion(EGratiaFaceEmotion::Bliss, Excitement, 0.0f);
    else if (PendingEmotion == EGratiaFaceEmotion::Bliss && Excitement < S.HeartPupilThreshold - 0.1f)
        PendingEmotion = EGratiaFaceEmotion::Neutral;
    EmotionLeft -= Dt;
    if (EmotionLeft <= 0.0f) { PendingEmotion = EGratiaFaceEmotion::Neutral; EmotionStrength = 0.0f; }
    Emotion = PendingEmotion;
    Embarrassment = FMath::Max(0.0f, Embarrassment - 0.12f * Dt);

    // Viewer: the pawn camera (as UGratiaInteraction uses it).
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    const UCameraComponent* View = Player ? Player->FindComponentByClass<UCameraComponent>() : nullptr;
    const FVector ViewLocation = View ? View->GetComponentLocation() : FVector::ZeroVector;
    const FQuat ViewRotation = View ? View->GetComponentQuat() : FQuat::Identity;

    // Reflex squint: a controller moving fast near the face.
    if (Player)
    {
        TArray<UMotionControllerComponent*> Controllers;
        Player->GetComponents(Controllers);
        int32 Hand = 0;
        for (const UMotionControllerComponent* Controller : Controllers)
        {
            if (!Controller || Hand >= 2) continue;
            if (!Controller->IsTracked()) { bHasLastHand[Hand++] = false; continue; }
            const FVector Location = Controller->GetComponentLocation();
            if (bHasLastHand[Hand] && ReflexCooldown <= 0.0f)
            {
                const float Speed = float(FVector::Distance(Location, LastHand[Hand]) / Dt);
                const float Urgency = ReflexUrgency(float(FVector::Distance(Location, LastEyeCenter)), Speed, S.ReflexRadiusCm, S.ReflexSpeedCmPerSecond);
                if (Urgency > 0.0f) TriggerReflexSquint(Urgency);
            }
            LastHand[Hand] = Location; bHasLastHand[Hand] = true; ++Hand;
        }
    }
    ReflexLeft = FMath::Max(0.0f, ReflexLeft - Dt);
    ReflexCooldown = FMath::Max(0.0f, ReflexCooldown - Dt);

    UpdateLightLevel(Dt);
    float EyeYawOut = 0.0f, EyePitchOut = 0.0f, ViewYaw = 0.0f;
    UpdateEyesAndNeck(Dt, bIdle, ViewLocation, ViewRotation, View != nullptr, EyeYawOut, EyePitchOut, ViewYaw);
    const float Squint = UpdateLids(Dt, EyePitchOut);
    UpdateExpression(Dt, bIdle && !bCue, ViewYaw, Squint);
    UpdateShaders(Dt, ViewYaw);
    PushBoneDeltas(bIdle, true);
}

void UGratiaProceduralFace::UpdateEyesAndNeck(float Dt, bool bIdle, const FVector& ViewLocation, const FQuat& ViewRotation, bool bHasView,
    float& OutEyeYaw, float& OutEyePitch, float& OutViewYawAroundHead)
{
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get();
    const FGratiaFaceSettings& S = Profile->Face;
    USkeletalMeshComponent* MeshComponent = Character->CharacterMesh;
    const FSegment& HeadSegment = Segments[0];
    if (!HeadSegment.bValid || MeshComponent->GetBoneIndex(HeadSegment.Bone) == INDEX_NONE) return;

    // Current head frame from the evaluated pose (animation + last frame's springs).
    const FQuat HeadQuat = MeshComponent->GetBoneQuaternion(HeadSegment.Bone, EBoneSpaces::WorldSpace);
    const FVector HeadLocation = MeshComponent->GetBoneLocation(HeadSegment.Bone, EBoneSpaces::WorldSpace);
    const FVector Forward = HeadQuat.RotateVector(HeadForwardLocal).GetSafeNormal(), Up = HeadQuat.RotateVector(HeadUpLocal).GetSafeNormal();
    const double Scale = MeshComponent->GetComponentScale().GetAbsMax();
    const float ScaleF = float(Scale);
    LastEyeCenter = HeadLocation + (Forward * S.EyeCenterOffsetCm.X + Up * S.EyeCenterOffsetCm.Y) * Scale;
    LastHeadRight = FVector::CrossProduct(Up, Forward).GetSafeNormal();

    // Head angular velocity/acceleration for the jaw and cheek jelly.
    if (bHasLastHead)
    {
        const FQuat Delta = (HeadQuat * LastHeadQuat.Inverse()).GetNormalized();
        FVector Axis; double Angle = 0.0;
        Delta.ToAxisAndAngle(Axis, Angle);
        if (Angle > PI) Angle -= 2.0 * PI;
        const FVector Rate = Axis * FMath::RadiansToDegrees(Angle) / Dt;
        const float YawRate = float(FVector::DotProduct(Rate, Up)), PitchRate = float(FVector::DotProduct(Rate, LastHeadRight));
        if (FMath::IsFinite(YawRate) && FMath::IsFinite(PitchRate))
        {
            const float Gain = S.JiggleGain;
            JawJiggle.Kick((FMath::Abs(YawRate - LastYawRate) + FMath::Abs(PitchRate - LastPitchRate)) * Gain);
            SideJiggle.Kick(-(YawRate - LastYawRate) * Gain);
            CheekJiggle.Kick(-(PitchRate - LastPitchRate) * Gain);
            LastYawRate = YawRate; LastPitchRate = PitchRate;
        }
    }
    LastHeadQuat = HeadQuat; bHasLastHead = true;

    const FTransform& Actor = Character->GetActorTransform();
    const FVector ActorForward = Actor.TransformVectorNoScale(Profile->ForwardAxis).GetSafeNormal();
    const FVector ActorUp = Actor.TransformVectorNoScale(Profile->UpAxis).GetSafeNormal();
    const UGratiaInteraction* Interaction = Character->Interaction;
    const bool bGaze = Profile->Capabilities.bGaze && bHasView;

    // Viewer distance and approach speed.
    const float Distance = bHasView ? float(FVector::Distance(ViewLocation, LastEyeCenter)) : 1.0e6f;
    const float Approach = bHasLastView ? (LastViewDistance - Distance) / Dt : 0.0f;
    LastViewDistance = Distance; bHasLastView = bHasView;
    AversionCooldown = FMath::Max(0.0f, AversionCooldown - Dt);
    const float ArchetypeAversion = ReadBodyMotion(TEXT("GazeAversion"), 0.0f);
    const bool bCrowded = ShouldAvert(Distance, Approach, S.AversionCloseCm * ScaleF, S.AversionRangeCm * ScaleF, S.AversionApproachCmPerSecond);
    if (bGaze && S.bGazeAversion && !bAverting && AversionCooldown <= 0.0f && (bCrowded || ArchetypeAversion > 0.6f))
        TriggerGazeAversion();
    if (bAverting)
    {
        AversionLeft -= Dt;
        if (AversionLeft <= 0.0f) { bAverting = false; ShyLeft = S.ShyReturnSeconds; AversionCooldown = S.AversionCooldownSeconds; }
    }
    ShyLeft = FMath::Max(0.0f, ShyLeft - Dt);
    // Embarrassment: looking away, the shy return, a reserved/tsundere archetype or a crowding approach; fades back with
    // eye contact.
    Fluster = StepFluster(Fluster, S.bFlusteredDarting && bGaze && (bAverting || ShyLeft > 0.0f || bCrowded
        || ArchetypeAversion > S.FlusterArchetypeThreshold), S.FlusterRiseSeconds, S.FlusterFadeSeconds, Dt);
    const bool bDarting = Fluster >= 0.5f;

    // Fixation target: the viewer's eyes and mouth, or the touching hand while a zone is held.
    UntilSaccade -= Dt;
    const bool bNewSaccade = UntilSaccade <= 0.0f;
    if (bNewSaccade)
    {
        Fixation = NextFixation(Fixation, S.MouthFixationShare, Random);
        const float Faster = 1.0f - 0.25f * Excitement;
        const FVector2D Interval = bDarting ? S.FlusterIntervalSeconds : S.SaccadeIntervalSeconds;
        UntilSaccade = Random.FRandRange(float(Interval.X), float(FMath::Max(Interval.X, Interval.Y))) * Faster;
        // A dart goes to the other extreme.
        DartSide = -DartSide;
        DartPitch = float(S.FlusterDartDegrees.Y) + Random.FRandRange(-1.0f, 1.0f) * S.FlusterDartPitchSpreadDegrees;
        MicroOffset = FVector(0.0, Random.FRandRange(-1.2f, 1.2f), Random.FRandRange(-1.2f, 1.2f));
    }
    const bool bLookAtTouch = Interaction && FVector::DistSquared(Interaction->LookTarget, ViewLocation) > 25.0;
    const FVector FacePoint = bLookAtTouch ? Interaction->LookTarget : ViewLocation;
    const FVector FixationPoint = bLookAtTouch ? FacePoint + ViewRotation.RotateVector(MicroOffset)
        : ViewLocation + ViewRotation.RotateVector(FixationOffset(Fixation, S.ViewerEyeSpacingCm, S.ViewerMouthDropCm));

    float TargetYaw = 0.0f, TargetPitch = 0.0f;
    if (bGaze) DirectionAngles(FixationPoint - LastEyeCenter, Forward, Up, TargetYaw, TargetPitch);
    // In eye contact the jumps between the viewer's eyes and mouth are micro-shifts around the face centre.
    if (bGaze && !bLookAtTouch)
    {
        float CentreYaw = 0.0f, CentrePitch = 0.0f;
        DirectionAngles(ViewLocation - LastEyeCenter, Forward, Up, CentreYaw, CentrePitch);
        LimitSaccade(CentreYaw, CentrePitch, S.ContactSaccadeMaxDegrees, TargetYaw, TargetPitch);
    }
    if (bDarting)
    {
        // Flustered: from one extreme to the other (around the look-away side while averting).
        const float Centre = bAverting ? AversionSide * 0.5f * float(S.AversionEyeDegrees.X) : 0.0f;
        TargetYaw = Centre + DartSide * float(S.FlusterDartDegrees.X);
        TargetPitch = DartPitch;
    }
    else if (bAverting)
    {
        TargetYaw = AversionSide * float(S.AversionEyeDegrees.X); TargetPitch = float(S.AversionEyeDegrees.Y);
    }
    else if (ShyLeft > 0.0f && FMath::Fmod(ShyLeft, 0.5f) > 0.2f)
    {
        // Shy return: brief contact, then half-way back down, a few times.
        TargetYaw = FMath::Lerp(TargetYaw, AversionSide * float(S.AversionEyeDegrees.X), 0.5f);
        TargetPitch = FMath::Lerp(TargetPitch, float(S.AversionEyeDegrees.Y), 0.5f);
    }
    const float Reach = 1.1f;
    TargetYaw = FMath::Clamp(TargetYaw, -S.FullEyeYawDegrees * Reach, S.FullEyeYawDegrees * Reach);
    TargetPitch = FMath::Clamp(TargetPitch, -S.FullEyePitchDegrees * Reach, S.FullEyePitchDegrees * Reach);
    // A large gaze shift often comes with a blink.
    if (FMath::Abs(TargetYaw - LastEyeTargetYaw) + FMath::Abs(TargetPitch - LastEyeTargetPitch) > 20.0f
        && BlinkElapsed < 0.0f && Random.FRand() < S.GazeShiftBlinkChance) UntilBlink = 0.0f;
    LastEyeTargetYaw = TargetYaw; LastEyeTargetPitch = TargetPitch;
    OutEyeYaw = EyeYaw.Step(TargetYaw, S.EyeSpringHz, S.EyeSpringDamping, Dt);
    OutEyePitch = EyePitch.Step(TargetPitch, S.EyeSpringHz, S.EyeSpringDamping, Dt);
    if (bGaze)
    {
        const FEyeMorphs Eyes = EyeMorphs(OutEyeYaw, OutEyePitch, S.FullEyeYawDegrees, S.FullEyePitchDegrees);
        SetMorph(S.bMirrorLookMorphs ? LookRight : LookLeft, Eyes.Left);
        SetMorph(S.bMirrorLookMorphs ? LookLeft : LookRight, Eyes.Right);
        SetMorph(LookUp, Eyes.Up);
        SetMorph(LookDown, Eyes.Down);
    }
    else for (const FName Semantic : { LookLeft, LookRight, LookUp, LookDown }) ReleaseMorph(Semantic);

    // Viewer angle around the head (for the asymmetric mouth).
    float ViewPitch = 0.0f;
    if (bHasView) DirectionAngles(ViewLocation - LastEyeCenter, Forward, Up, OutViewYawAroundHead, ViewPitch);

    // Neck: head, neck and upper chest follow the face in the actor frame, after the eyes.
    float GoalYaw = 0.0f, GoalPitch = 0.0f;
    if (bIdle && bGaze && S.bNeckSpring)
    {
        float Yaw = 0.0f, Pitch = 0.0f;
        const FVector Direction = FacePoint - LastEyeCenter;
        if (FVector::DotProduct(Direction, ActorForward) > 10.0)
        {
            DirectionAngles(Direction, ActorForward, ActorUp, Yaw, Pitch);
            Yaw = FMath::Clamp(Yaw, -Profile->MaxHeadYawDegrees, Profile->MaxHeadYawDegrees);
            Pitch = FMath::Clamp(Pitch, -Profile->MaxHeadPitchDegrees, Profile->MaxHeadPitchDegrees);
            // Hysteresis: small changes stay with the eyes.
            if (FMath::Abs(Yaw - HeadGoalYaw) > S.HeadDeadZoneDegrees || FMath::Abs(Pitch - HeadGoalPitch) > S.HeadDeadZoneDegrees)
            { HeadGoalYaw = Yaw; HeadGoalPitch = Pitch; }
        }
        else HeadGoalYaw = HeadGoalPitch = 0.0f;
        GoalYaw = HeadGoalYaw; GoalPitch = HeadGoalPitch;
        if (bAverting) { GoalYaw += AversionSide * float(S.AversionHeadDegrees.X); GoalPitch += float(S.AversionHeadDegrees.Y); }
    }
    float Hz = S.NeckSpringHz;
    for (int32 I = 0; I < 3; ++I, Hz *= S.SegmentLag)
    {
        FSegment& Segment = Segments[I];
        if (!Segment.bValid) continue;
        Segment.Yaw.Step(GoalYaw * Shares[I], Hz, S.NeckSpringDamping, Dt);
        Segment.Pitch.Step(GoalPitch * Shares[I], Hz, S.NeckSpringDamping, Dt);
    }
}

float UGratiaProceduralFace::UpdateLids(float Dt, float LookPitch)
{
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get();
    const FGratiaFaceSettings& S = Profile->Face;
    if (BlinkElapsed < 0.0f)
    {
        UntilBlink -= Dt;
        if (UntilBlink <= 0.0f)
        {
            BlinkElapsed = 0.0f;
            auto Pick = [this](const FVector2D& Range) { return Random.FRandRange(float(Range.X), float(FMath::Max(Range.X, Range.Y))); };
            BlinkClose = Pick(S.BlinkCloseSeconds); BlinkHold = Pick(S.BlinkHoldSeconds); BlinkOpen = Pick(S.BlinkOpenSeconds);
        }
    }
    float Blink = 0.0f;
    if (BlinkElapsed >= 0.0f)
    {
        BlinkElapsed += Dt;
        Blink = BlinkWeight(BlinkElapsed, BlinkClose, BlinkHold, BlinkOpen);
        if (BlinkElapsed >= BlinkClose + BlinkHold + BlinkOpen)
        {
            BlinkElapsed = -1.0f;
            if (bDoubleBlinkPending) { bDoubleBlinkPending = false; UntilBlink = 0.08f; }
            else
            {
                UntilBlink = NextBlinkInterval(float(S.BlinkIntervalSeconds.X), float(S.BlinkIntervalSeconds.Y), Excitement, Random);
                bDoubleBlinkPending = Random.FRand() < S.DoubleBlinkChance;
            }
        }
    }
    const bool bEmbarrassedSqueeze = Emotion == EGratiaFaceEmotion::Embarrassed && EmotionStrength > 0.7f;
    const float SqueezeTarget = FMath::Max(ReflexLeft > 0.0f ? ReflexStrength : 0.0f, bEmbarrassedSqueeze ? 0.85f * EmotionStrength : 0.0f);
    const float Squint = Saturate(Squeeze.Step(SqueezeTarget, S.SnapSeconds, S.LidOvershoot, S.SettleHz * 1.5f, 0.35f, Dt));
    // Half-lidded at the peak; lids follow the gaze down; they tremble with high excitement.
    const float Base = (Emotion == EGratiaFaceEmotion::Bliss ? 0.35f * EmotionStrength : 0.0f)
        + S.LidFollowDown * Saturate(-LookPitch / FMath::Max(1.0f, S.FullEyePitchDegrees))
        + S.TremorAmplitude * 0.5f * FMath::Square(Excitement) * FMath::Abs(Noise1D(Time * S.TremorHz, 17));
    const float Lid = Saturate(1.0f - (1.0f - Blink) * (1.0f - Squint) * (1.0f - Saturate(Base)));
    if (Profile->Capabilities.bBlink) { SetMorph(BlinkLeft, Lid); SetMorph(BlinkRight, Lid); }
    else { ReleaseMorph(BlinkLeft); ReleaseMorph(BlinkRight); }
    return Squint;
}

void UGratiaProceduralFace::UpdateExpression(float Dt, bool bExpressive, float ViewYawAroundHead, float Squint)
{
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get();
    const FGratiaFaceSettings& S = Profile->Face;
    USkeletalMeshComponent* MeshComponent = Character->CharacterMesh;
    const float Jelly = S.MaxJiggle;
    JawJiggle.Step(0.0f, S.JiggleHz, S.JiggleDamping, Dt);
    SideJiggle.Step(0.0f, S.JiggleHz * 0.8f, S.JiggleDamping, Dt);
    CheekJiggle.Step(0.0f, S.JiggleHz * 1.2f, S.JiggleDamping, Dt);
    JawJiggle.Value = FMath::Clamp(JawJiggle.Value, -Jelly, Jelly);
    SideJiggle.Value = FMath::Clamp(SideJiggle.Value, -Jelly, Jelly);
    CheekJiggle.Value = FMath::Clamp(CheekJiggle.Value, -Jelly, Jelly);

    // Emotion layer targets.
    TMap<FName, float> Layer;
    auto Add = [&Layer](FName Semantic, float Weight) { float& Value = Layer.FindOrAdd(Semantic); Value = FMath::Max(Value, Weight); };
    const float K = EmotionStrength;
    float Stretch = 0.0f, SquashTarget = 0.0f;
    switch (Emotion)
    {
    case EGratiaFaceEmotion::Surprise:
        Add(BrowsUp, K); Add(Surprise, 0.9f * K); Add(MouthOpen, 0.35f * K);
        // The stretch is a momentary pop: full at the snap, gone by the end of the emotion.
        Stretch = K * Saturate(EmotionLeft / 0.6f); break;
    case EGratiaFaceEmotion::Embarrassed:
        Add(BrowsWorry, 0.8f * K); Add(WinkLeft, 0.35f * K); Add(WinkRight, 0.35f * K); Add(LipBite, 0.6f * K); Add(Smile, 0.15f * K);
        SquashTarget = 0.6f * K; break;
    case EGratiaFaceEmotion::Pout:
        Add(MouthPuff, K); Add(BrowsAngry, 0.6f * K); Add(MouthPout, 0.3f * K); break;
    case EGratiaFaceEmotion::Happy:
        Add(Smile, 0.9f * K); Add(WinkLeft, 0.25f * K); Add(WinkRight, 0.25f * K); Add(BrowsUp, 0.2f * K); break;
    case EGratiaFaceEmotion::Bliss:
        Add(EyesSmug, 0.6f * K); Add(MouthOpen, 0.4f * K); Add(BrowsWorry, 0.5f * K); Add(Smile, 0.2f * K);
        Stretch = 0.4f * K * (0.5f + 0.5f * Noise1D(Time * 0.8f, 23)); break;
    default: break;
    }
    // Lingering embarrassment and archetype outputs (body motion) as a light constant layer.
    const float Shy = Saturate((Embarrassment - 0.3f) / 0.7f);
    Add(BrowsWorry, 0.5f * Shy); Add(LipBite, 0.3f * Shy);
    const float Pout = Saturate(ReadBodyMotion(TEXT("Pout"), 0.0f)) * (1.0f - Saturate(ReadBodyMotion(TEXT("Thaw"), 0.0f)));
    Add(MouthPuff, Pout); Add(BrowsAngry, 0.5f * Pout);
    Add(Smile, Saturate(ReadBodyMotion(TEXT("SmileBias"), 0.0f)));
    Add(EyesShrink, 0.6f * Squint); Add(BrowsDown, 0.5f * Squint);
    SquashTarget = FMath::Max(SquashTarget, Squint);
    const float Impulse = Character->Interaction ? Character->Interaction->Impulse : 0.0f;
    Stretch = FMath::Max(Stretch, 0.6f * Saturate(Impulse));

    // Asymmetric mouth: in 3/4 view the mouth corner moves onto the cheek.
    const float Asymmetry = S.bAsymmetricMouth ? MouthAsymmetry(ViewYawAroundHead, S.AsymmetryStartDegrees, S.AsymmetryFullDegrees) : 0.0f;
    // Viewer on the character's right (yaw > 0): the silhouette side is her left.
    const bool bLeftSide = (ViewYawAroundHead > 0.0f) == S.bShiftTowardSilhouette;
    MouthShiftSign = bLeftSide ? -1.0f : 1.0f;
    MouthAsymmetryWeight = Asymmetry;

    const float E2 = FMath::Square(Excitement);
    for (FMorphChannel& Channel : Channels)
    {
        if (!bExpressive)
        {
            if (Channel.bWritten) { ReleaseMorph(Channel.Semantic); Channel.bWritten = false; }
            Channel.Spring.Reset(); Channel.LastWritten = -1.0f;
            continue;
        }
        float Target = 0.0f;
        if (const float* Value = Layer.Find(Channel.Semantic)) Target = *Value;
        if (IsInteractionMorph(Channel.Semantic))
        {
            // UGratiaInteraction wrote its reaction weight this frame; our own last output means it did not.
            const float Current = MeshComponent->GetMorphTarget(Channel.Morph);
            if (!FMath::IsNearlyEqual(Current, Channel.LastWritten, 1.0e-4f)) Channel.InteractionTarget = Current;
            Target = FMath::Max(Target, Channel.InteractionTarget);
        }
        const EChannelKind Kind = EChannelKind(Channel.Kind);
        const float Overshoot = Kind == EChannelKind::Brow ? S.BrowOvershoot : Kind == EChannelKind::Eye ? S.LidOvershoot : S.MouthOvershoot;
        float Out = Channel.Spring.Step(Saturate(Target), S.SnapSeconds, Overshoot, S.SettleHz, S.SettleDamping, Dt);
        // Micro motion after the spring so it is not amplified by the overshoot.
        const float JitterAmp = S.JitterAmplitude, TremorAmp = S.TremorAmplitude * E2;
        if (Channel.Semantic == MouthOpen)
            Out += JitterAmp * (0.5f + 0.5f * GratiaFaceMath::Jitter(Time, S.JitterHz, 1)) + TremorAmp * Noise1D(Time * S.TremorHz, 5) + FMath::Max(0.0f, JawJiggle.Value);
        else if (Channel.Semantic == Smile)
            Out += JitterAmp * GratiaFaceMath::Jitter(Time, S.JitterHz, 2);
        else if (Channel.Semantic == CornerLeft || Channel.Semantic == CornerRight)
        {
            const bool bLeft = Channel.Semantic == CornerLeft;
            const float Speech = Saturate(0.4f + Layer.FindRef(Smile) + Layer.FindRef(MouthOpen) + Channel.InteractionTarget);
            Out += 0.6f * JitterAmp * GratiaFaceMath::Jitter(Time, S.JitterHz, bLeft ? 3 : 4)
                + (bLeft == bLeftSide ? Asymmetry * S.AsymmetryCornerWeight * Speech : 0.0f)
                + (bLeft ? 1.0f : -1.0f) * SideJiggle.Value;
        }
        else if (Channel.Semantic == MouthPuff) Out += FMath::Abs(CheekJiggle.Value) * 0.6f;
        else if (Channel.Semantic == MouthNervous) Out += TremorAmp * FMath::Abs(Noise1D(Time * S.TremorHz * 0.9f, 6));
        else if (Channel.Semantic == BrowsWorry) Out += 0.5f * TremorAmp * Noise1D(Time * S.TremorHz * 0.7f, 7);
        Out = Saturate(Out);
        Channel.LastWritten = Out;
        Channel.bWritten = true;
        SetMorph(Channel.Semantic, Out);
    }

    const float SquashGoal = S.bSquashStretch ? Stretch * S.StretchAmount - SquashTarget * S.SquashAmount : 0.0f;
    Squash.Step(SquashGoal, S.SnapSeconds, 0.3f, S.SettleHz, S.SettleDamping, Dt);
}

void UGratiaProceduralFace::UpdateShaders(float Dt, float ViewYawAroundHead)
{
    const FGratiaFaceSettings& S = Character->CharacterProfile->Face;
    const float K = EmotionStrength;
    const float BlushGoal = Saturate(0.25f * Excitement + 0.8f * Embarrassment + ExtraBlush
        + (Emotion == EGratiaFaceEmotion::Embarrassed || Emotion == EGratiaFaceEmotion::Bliss ? 0.6f * K : 0.0f));
    Blush = Saturate(BlushSpring.Step(BlushGoal, 0.7f, 1.0f, Dt));
    const float SweatRate = Excitement > 0.5f ? (Excitement - 0.5f) * 2.0f : -0.5f;
    Sweat = Saturate(Sweat + SweatRate * Dt / FMath::Max(1.0f, S.SweatBuildSeconds));
    Goosebumps = FMath::Max(0.0f, Goosebumps - Dt / 3.0f);
    const float TearGoal = Saturate(FMath::Max3(ExtraTears, Emotion == EGratiaFaceEmotion::Bliss ? 0.6f * K : 0.0f,
        Emotion == EGratiaFaceEmotion::Embarrassed && K > 0.8f ? 0.4f : 0.0f));
    Tears = Saturate(TearSpring.Step(TearGoal, 0.35f, 1.0f, Dt));
    PupilScale = PupilSpring.Step(GratiaFaceMath::PupilScale(LightLevel, Excitement, S.PupilDarkScale, S.PupilBrightScale, S.PupilExcitementGain), 1.2f, 1.0f, Dt);
    // Hearts pop in with a little overshoot.
    const float HeartGoal = FMath::Max(FMath::SmoothStep(S.HeartPupilThreshold, 1.0f, Excitement), Emotion == EGratiaFaceEmotion::Bliss ? K : 0.0f);
    HeartPupils = FMath::Clamp(HeartSpring.Step(HeartGoal, 2.0f, 0.4f, Dt), 0.0f, 1.25f);
    if (!S.bShaderParameters) return;
    SetScalar(TEXT("GratiaBlush"), Blush);
    SetScalar(TEXT("GratiaSweat"), Sweat);
    SetScalar(TEXT("GratiaGoosebumps"), Goosebumps);
    SetScalar(TEXT("GratiaTears"), Tears);
    SetScalar(TEXT("GratiaPupilScale"), PupilScale);
    SetScalar(TEXT("GratiaHeartPupils"), HeartPupils);
    const double Scale = Character->CharacterMesh->GetComponentScale().GetAbsMax();
    SetScalar(TEXT("GratiaMouthShiftCm"), float(MouthShiftSign * MouthAsymmetryWeight * S.MouthShiftCm * Scale));
    if (!LastHeadRight.Equals(LastHeadRightParam, 0.01))
    {
        LastHeadRightParam = LastHeadRight;
        Character->CharacterMesh->SetVectorParameterValueOnMaterials(TEXT("GratiaHeadRight"), FVector(LastHeadRight));
    }
}

void UGratiaProceduralFace::PushBoneDeltas(bool bIdle, bool bEnabled)
{
    UGratiaAnimInstance* Instance = Cast<UGratiaAnimInstance>(Character->CharacterMesh->GetAnimInstance());
    if (!Instance) return;
    Instance->FaceBoneDeltas.Reset();
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile.Get();
    Instance->bFaceDrivesHead = bEnabled && bIdle && Profile && Profile->Capabilities.bGaze && Profile->Face.bNeckSpring && Segments[0].bValid;
    if (!bEnabled) return;
    for (int32 I = 0; I < 3; ++I)
    {
        const FSegment& Segment = Segments[I];
        if (!Segment.bValid) continue;
        FTransform Delta(FQuat(Segment.YawAxis, FMath::DegreesToRadians(Segment.Yaw.Value)) * FQuat(Segment.NodAxis, FMath::DegreesToRadians(Segment.Pitch.Value)));
        if (I == 0 && Profile && Profile->Face.bSquashStretch)
        {
            float Vertical = 1.0f, Horizontal = 1.0f;
            SquashStretch(Squash.Value, Vertical, Horizontal);
            FVector Scale(Horizontal);
            Scale[HeadVerticalAxis] = Vertical;
            Delta.SetScale3D(Scale);
        }
        if (Delta.GetRotation().IsIdentity(1.0e-6f) && Delta.GetScale3D().Equals(FVector::OneVector, 1.0e-4)) continue;
        Instance->FaceBoneDeltas.Emplace(Segment.Bone, Delta);
    }
}

FString UGratiaProceduralFace::GetFaceDiagnostics() const
{
    static const TCHAR* EmotionNames[] = { TEXT("neutral"), TEXT("surprise"), TEXT("embarrassed"), TEXT("pout"), TEXT("happy"), TEXT("bliss") };
    return FString::Printf(TEXT("face emotion=%s excitement=%.2f embarrassment=%.2f eyes=(%.1f,%.1f) head=(%.1f,%.1f) squash=%.3f blush=%.2f sweat=%.2f tears=%.2f pupil=%.2f hearts=%.2f light=%.2f avert=%d channels=%d missing=[%s]"),
        EmotionNames[FMath::Clamp(int32(Emotion), 0, 5)], Excitement, Embarrassment, EyeYaw.Value, EyePitch.Value,
        Segments[0].Yaw.Value, Segments[0].Pitch.Value, Squash.Value, Blush, Sweat, Tears, PupilScale, HeartPupils, LightLevel,
        bAverting ? 1 : 0, Channels.Num(), *FString::Join(Missing, TEXT(", ")));
}
