#include "GratiaGarments.h"
#include "GratiaPlaySettings.h"
#include "GratiaPlaySubsystem.h"
#include "GratiaPlayPose.h"
#include "GratiaPlayBody.h"
#include "GratiaArousal.h"
#include "GratiaHapticLayers.h"
#include "GratiaVocalLayer.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaStage1Runtime.h"
#include "Components/SkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaGarments, Log, All);

namespace
{
    UGratiaHapticLayers* Haptics(const UObject* Context)
    {
        const UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(Context);
        AGratiaStage1Runtime* Stage = Play ? Play->GetRuntime() : nullptr;
        return Stage ? Stage->FindComponentByClass<UGratiaHapticLayers>() : nullptr;
    }

    bool IsTwoHand(EGratiaGarmentAction Action) { return Action == EGratiaGarmentAction::Unzip || Action == EGratiaGarmentAction::Untie; }
}

UGratiaGarments::UGratiaGarments()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGratiaGarments::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    // Garment handles are small, specific targets: they get the first say over a grip before the limb grabs.
    if (UGratiaPlayBody* Body = GetOwner()->FindComponentByClass<UGratiaPlayBody>()) Body->AddTickPrerequisiteComponent(this);
}

void UGratiaGarments::EndPlay(const EEndPlayReason::Type Reason)
{
    for (FPiece& Piece : Pieces) ReleaseHands(Piece);
    Super::EndPlay(Reason);
}

void UGratiaGarments::Resolve(const UGratiaPlaySettings& Settings)
{
    for (FPiece& Piece : Pieces) ReleaseHands(Piece);
    CachedSettings = &Settings;
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    CachedProfile = Profile;
    const USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    auto Bone = [Profile, Mesh](FName Semantic) -> FName
    {
        const FName Name = Profile && !Semantic.IsNone() ? Profile->ResolveBone(Semantic) : NAME_None;
        return Mesh && !Name.IsNone() && Mesh->GetBoneIndex(Name) != INDEX_NONE ? Name : NAME_None;
    };
    TArray<FPiece> Previous = MoveTemp(Pieces);
    Pieces.SetNum(Settings.Garments.Num());
    int32 Usable = 0;
    for (int32 I = 0; I < Pieces.Num(); ++I)
    {
        const FGratiaGarmentPiece& Definition = Settings.Garments[I];
        FPiece& Piece = Pieces[I];
        // Progress survives a profile refresh when the piece list is the same.
        if (Previous.IsValidIndex(I)) Piece.State = Previous[I].State;
        Piece.State.bHeld = false;
        Piece.HandleBone = Bone(Definition.HandleSemantic);
        Piece.AnchorBone = Bone(Definition.AnchorSemantic);
        for (FName Semantic : Definition.MorphSemantics)
        {
            const FName Morph = Profile ? Profile->ResolveMorph(Semantic) : NAME_None;
            if (!Morph.IsNone()) Piece.Morphs.Add(Morph);
            else UE_LOG(LogGratiaGarments, Warning, TEXT("GARMENT %s: morph %s is not mapped in the profile."), *Definition.Name.ToString(), *Semantic.ToString());
        }
        for (const TPair<FName, float>& Follow : Definition.FollowBones)
            if (Mesh && Mesh->GetBoneIndex(Follow.Key) != INDEX_NONE) Piece.FollowBones.Emplace(Follow.Key, FMath::Clamp(Follow.Value, 0.0f, 1.0f));
        const bool bVisible = !Piece.Morphs.IsEmpty() || !Definition.MaterialParameter.IsNone() || !Piece.FollowBones.IsEmpty();
        const bool bUsable = !Piece.HandleBone.IsNone() && (!IsTwoHand(Definition.Action) || !Piece.AnchorBone.IsNone()) && bVisible;
        if (!bUsable)
        {
            UE_LOG(LogGratiaGarments, Warning, TEXT("GARMENT %s disabled: handle %s, anchor %s, visible result %d."), *Definition.Name.ToString(),
                *Definition.HandleSemantic.ToString(), *Definition.AnchorSemantic.ToString(), bVisible);
            Piece.HandleBone = NAME_None;
        }
        Usable += bUsable;
        Apply(Settings, I, true);
    }
    UE_LOG(LogGratiaGarments, Display, TEXT("GARMENTS resolved %d/%d pieces"), Usable, Pieces.Num());
    if (Pieces.IsEmpty() && !bReported)
        UE_LOG(LogGratiaGarments, Display, TEXT("GARMENTS none defined for this profile (PlaySettings.Garments); see docs/PLAY_LAYER.md."));
    bReported = true;
}

FVector UGratiaGarments::PointOn(FName Bone, const FVector& Offset) const
{
    return Character->CharacterMesh->GetSocketTransform(Bone).TransformPosition(Offset);
}

FVector UGratiaGarments::AxisOf(FName Bone, const FVector& Axis) const
{
    return Character->CharacterMesh->GetSocketTransform(Bone).TransformVectorNoScale(Axis).GetSafeNormal();
}

bool UGratiaGarments::IsAvailable(const UGratiaPlaySettings& Settings, int32 Index) const
{
    const FGratiaGarmentPiece& Definition = Settings.Garments[Index];
    const UGratiaArousal* Arousal = GetOwner()->FindComponentByClass<UGratiaArousal>();
    if (Arousal && Arousal->GetStage() < Definition.MinStage) return false;
    for (FName Required : Definition.Requires)
    {
        const int32 Other = Settings.Garments.IndexOfByPredicate([Required](const FGratiaGarmentPiece& G) { return G.Name == Required; });
        if (Pieces.IsValidIndex(Other) && !Pieces[Other].State.bComplete) return false;
    }
    return true;
}

void UGratiaGarments::ReleaseHands(FPiece& Piece)
{
    UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this);
    if (Play && Piece.Hand != INDEX_NONE) Play->ReleaseHand(Piece.Hand == 0, this);
    if (Play && Piece.AnchorHand != INDEX_NONE) Play->ReleaseHand(Piece.AnchorHand == 0, this);
    Piece.Hand = Piece.AnchorHand = INDEX_NONE;
    Piece.GrabSeparation = 0.0;
    Piece.State.bHeld = false;
}

void UGratiaGarments::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Character.Get());
    UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this);
    if (!Settings || !Play || !Character.IsValid() || !Character->CharacterMesh) return;
    if (CachedSettings.Get() != Settings || CachedProfile.Get() != Character->CharacterProfile.Get() || Pieces.Num() != Settings->Garments.Num())
        Resolve(*Settings);
    const bool bActive = bEnabled && UGratiaPlaySubsystem::IsLayerEnabled();
    const double Scale = FMath::Max(0.01, double(Character->CharacterMesh->GetComponentTransform().GetMaximumAxisScale()));
    const double Now = GetWorld()->GetTimeSeconds();

    // New pinches/grips take the nearest handle within reach.
    for (int32 HandIndex = 0; HandIndex < 2 && bActive; ++HandIndex)
    {
        const bool bLeft = HandIndex == 0;
        const UGratiaPlaySubsystem::FHand& Hand = Play->GetHand(bLeft);
        const float Pinch = FMath::Max(Hand.Grip, Hand.Trigger);
        const bool bPressed = Pinch >= Settings->GarmentGrabThreshold && PrevPinch[HandIndex] < Settings->GarmentGrabThreshold;
        PrevPinch[HandIndex] = Pinch;
        if (!bPressed || !Hand.bAllowed || Play->GetHandUse(bLeft) != EGratiaHandUse::None) continue;
        int32 Best = INDEX_NONE;
        double BestDistance = TNumericLimits<double>::Max();
        for (int32 I = 0; I < Pieces.Num(); ++I)
        {
            const FGratiaGarmentPiece& Definition = Settings->Garments[I];
            const FPiece& Piece = Pieces[I];
            if (Piece.HandleBone.IsNone() || Piece.State.bComplete || Piece.Hand != INDEX_NONE) continue;
            // A button wants the pinch (trigger), the rest any grip.
            if (Definition.Action == EGratiaGarmentAction::Unbutton && Hand.Trigger < Settings->GarmentGrabThreshold) continue;
            const double Distance = FVector::Distance(PointOn(Piece.HandleBone, Definition.HandleOffset), Hand.World.GetLocation());
            if (Distance <= (Definition.HandleRadiusCm + 2.0) * Scale && Distance < BestDistance) { Best = I; BestDistance = Distance; }
        }
        if (Best == INDEX_NONE) continue;
        const FGratiaGarmentPiece& Definition = Settings->Garments[Best];
        if (!IsAvailable(*Settings, Best))
        {
            if (Now - RefusedTime[HandIndex] > 2.0)
            {
                RefusedTime[HandIndex] = Now;
                OnGarmentRefused.Broadcast(Definition.Name, Pieces[Best].State.Progress);
                if (UGratiaHapticLayers* Layers = Haptics(this)) { Layers->Pulse(bLeft, 0.35f, 0.2f, 0.06f); }
                // She answers the attempt like a touch of her own (rate-limited by the interaction).
                if (Character->Interaction) Character->Interaction->ExternalReaction(Definition.Name, HandIndex, Hand.Speed);
                UE_LOG(LogGratiaGarments, Display, TEXT("GARMENT %s refused (stage/order)"), *Definition.Name.ToString());
            }
            continue;
        }
        if (!Play->ClaimHand(bLeft, EGratiaHandUse::Garment, this)) continue;
        FPiece& Piece = Pieces[Best];
        Piece.Hand = HandIndex;
        Piece.GrabHand = Hand.World.GetLocation();
        Piece.GrabSeparation = 0.0;
        if (UGratiaHapticLayers* Layers = Haptics(this)) Layers->Pulse(bLeft, 0.25f, 0.5f, 0.03f);
        UE_LOG(LogGratiaGarments, Display, TEXT("GARMENT %s taken hand=%s"), *Definition.Name.ToString(), bLeft ? TEXT("L") : TEXT("R"));
    }

    for (int32 I = 0; I < Pieces.Num(); ++I)
    {
        FPiece& Piece = Pieces[I];
        const FGratiaGarmentPiece& Definition = Settings->Garments[I];
        bool bHeld = false, bAnchored = false;
        float Drag = 0.0f;
        if (Piece.Hand != INDEX_NONE)
        {
            const bool bLeft = Piece.Hand == 0;
            const UGratiaPlaySubsystem::FHand& Hand = Play->GetHand(bLeft);
            const float Pinch = Definition.Action == EGratiaGarmentAction::Unbutton ? Hand.Trigger : FMath::Max(Hand.Grip, Hand.Trigger);
            bHeld = bActive && Hand.bAllowed && Play->IsHandOwnedBy(bLeft, this) && Pinch >= Settings->GarmentGrabThreshold * 0.6f;
            if (!bHeld) ReleaseHands(Piece);
            else
            {
                const FVector HandPoint = Hand.World.GetLocation();
                if (IsTwoHand(Definition.Action))
                {
                    // The other hand must hold the fabric (zipper) or the other end (tie).
                    const int32 Other = 1 - Piece.Hand;
                    const UGratiaPlaySubsystem::FHand& Second = Play->GetHand(Other == 0);
                    const bool bNear = FVector::Distance(Second.World.GetLocation(), PointOn(Piece.AnchorBone, Definition.AnchorOffset))
                        <= Definition.HandleRadiusCm * 1.5 * Scale;
                    const bool bGripping = Second.bAllowed && FMath::Max(Second.Grip, Second.Trigger) >= Settings->GarmentGrabThreshold * 0.6f;
                    if (Piece.AnchorHand == INDEX_NONE && bGripping && bNear && Play->ClaimHand(Other == 0, EGratiaHandUse::Garment, this))
                    {
                        Piece.AnchorHand = Other;
                        Piece.GrabSeparation = FVector::Distance(HandPoint, Second.World.GetLocation());
                        Piece.State.GrabProgress = Piece.State.Progress;
                        Piece.GrabHand = HandPoint;
                    }
                    else if (Piece.AnchorHand != INDEX_NONE && !bGripping)
                    {
                        Play->ReleaseHand(Other == 0, this);
                        Piece.AnchorHand = INDEX_NONE;
                    }
                    bAnchored = Piece.AnchorHand != INDEX_NONE;
                    if (Definition.Action == EGratiaGarmentAction::Untie && bAnchored)
                        Drag = float((FVector::Distance(HandPoint, Second.World.GetLocation()) - Piece.GrabSeparation) / (Definition.TravelCm * Scale));
                }
                if (Definition.Action != EGratiaGarmentAction::Untie)
                    Drag = float(FVector::DotProduct(HandPoint - Piece.GrabHand, AxisOf(Piece.HandleBone, Definition.DragAxis)) / (Definition.TravelCm * Scale));
            }
        }
        const bool bWasComplete = Piece.State.bComplete;
        GratiaPlay::StepGarment(Piece.State, bHeld, Drag, bAnchored, IsTwoHand(Definition.Action),
            Definition.SnapBack, Definition.Complete, Settings->GarmentStuckLimit, Delta);
        if (Piece.State.bComplete && !bWasComplete) ReleaseHands(Piece);
        Apply(*Settings, I, false);
    }
}

void UGratiaGarments::Apply(const UGratiaPlaySettings& Settings, int32 Index, bool bForce)
{
    FPiece& Piece = Pieces[Index];
    const FGratiaGarmentPiece& Definition = Settings.Garments[Index];
    USkeletalMeshComponent* Mesh = Character->CharacterMesh;
    const float Progress = Piece.State.Progress;
    // Morphs are re-sent every frame while open (a profile change clears all morph targets).
    if (Progress > 0.0f || bForce || Piece.Written > 0.0f)
        for (FName Morph : Piece.Morphs) Mesh->SetMorphTarget(Morph, Progress, false);
    if (!Definition.MaterialParameter.IsNone() && (bForce || FMath::Abs(Progress - Piece.Written) > 0.002f))
        Mesh->SetScalarParameterValueOnMaterials(Definition.MaterialParameter, Progress);
    const bool bHand = Piece.Hand != INDEX_NONE;
    UGratiaVocalLayer* Vocal = GetOwner()->FindComponentByClass<UGratiaVocalLayer>();
    const FVector Where = Piece.HandleBone.IsNone() ? GetOwner()->GetActorLocation() : PointOn(Piece.HandleBone, Definition.HandleOffset);
    if (Definition.Action == EGratiaGarmentAction::Unzip && bHand && FMath::Abs(Progress - Piece.LastTick) >= 0.06f)
    {
        Piece.LastTick = Progress;
        if (Vocal) Vocal->PlayFoley(EGratiaFoleyBank::ZipperTick, Where);
        if (UGratiaHapticLayers* Layers = Haptics(this)) Layers->Pulse(Piece.Hand == 0, 0.2f, 0.8f, 0.015f);
    }
    if (Piece.State.bComplete != Piece.bWasComplete)
    {
        Piece.bWasComplete = Piece.State.bComplete;
        if (Piece.State.bComplete && !bForce)
        {
            const EGratiaFoleyBank Bank = Definition.Action == EGratiaGarmentAction::Unbutton ? EGratiaFoleyBank::ButtonPop
                : Definition.Action == EGratiaGarmentAction::Slide ? EGratiaFoleyBank::StrapSlip : EGratiaFoleyBank::ClothSnap;
            if (Vocal) Vocal->PlayFoley(Bank, Where);
            UE_LOG(LogGratiaGarments, Display, TEXT("GARMENT %s undone"), *Definition.Name.ToString());
        }
        OnGarmentChanged.Broadcast(Definition.Name, Progress);
    }
    Piece.Written = Progress;
}

void UGratiaGarments::AppendLateOffsets(TArray<FGratiaPlayBoneOffset>& Out) const
{
    const UGratiaPlaySettings* Settings = CachedSettings.Get();
    if (!Settings || !Character.IsValid() || !Character->CharacterMesh) return;
    const FTransform ToWorld = Character->CharacterMesh->GetComponentTransform();
    for (int32 I = 0; I < Pieces.Num() && I < Settings->Garments.Num(); ++I)
    {
        const FPiece& Piece = Pieces[I];
        if (Piece.State.Progress <= 0.0f || Piece.FollowBones.IsEmpty() || Piece.HandleBone.IsNone()) continue;
        const FGratiaGarmentPiece& Definition = Settings->Garments[I];
        const FVector AxisCS = ToWorld.InverseTransformVectorNoScale(AxisOf(Piece.HandleBone, Definition.DragAxis));
        for (const TPair<FName, float>& Follow : Piece.FollowBones)
        {
            FGratiaPlayBoneOffset Offset;
            Offset.Bone = Follow.Key;
            Offset.OffsetCS = AxisCS * (Definition.TravelCm * Piece.State.Progress * Follow.Value);
            Out.Add(Offset);
        }
    }
}

float UGratiaGarments::GetProgress(FName Piece) const
{
    const UGratiaPlaySettings* Settings = CachedSettings.Get();
    const int32 Index = Settings ? Settings->Garments.IndexOfByPredicate([Piece](const FGratiaGarmentPiece& G) { return G.Name == Piece; }) : INDEX_NONE;
    return Pieces.IsValidIndex(Index) ? Pieces[Index].State.Progress : 0.0f;
}

bool UGratiaGarments::IsUndone(FName Piece) const
{
    const UGratiaPlaySettings* Settings = CachedSettings.Get();
    const int32 Index = Settings ? Settings->Garments.IndexOfByPredicate([Piece](const FGratiaGarmentPiece& G) { return G.Name == Piece; }) : INDEX_NONE;
    return Pieces.IsValidIndex(Index) && Pieces[Index].State.bComplete;
}

void UGratiaGarments::SetProgress(FName PieceName, float Progress)
{
    const UGratiaPlaySettings* Settings = CachedSettings.Get();
    const int32 Index = Settings ? Settings->Garments.IndexOfByPredicate([PieceName](const FGratiaGarmentPiece& G) { return G.Name == PieceName; }) : INDEX_NONE;
    if (!Pieces.IsValidIndex(Index) || !FMath::IsFinite(Progress)) return;
    FPiece& Piece = Pieces[Index];
    ReleaseHands(Piece);
    Piece.State = GratiaPlay::FGarmentState();
    Piece.State.Progress = FMath::Clamp(Progress, 0.0f, 1.0f);
    Piece.State.bComplete = Piece.State.Progress >= 1.0f;
    Apply(*Settings, Index, false);
}

void UGratiaGarments::ResetGarments()
{
    const UGratiaPlaySettings* Settings = CachedSettings.Get();
    for (int32 I = 0; I < Pieces.Num(); ++I)
    {
        ReleaseHands(Pieces[I]);
        Pieces[I].State = GratiaPlay::FGarmentState();
        Pieces[I].LastTick = 0.0f;
        if (Settings && Settings->Garments.IsValidIndex(I)) Apply(*Settings, I, true);
    }
}

FString UGratiaGarments::GetDiagnostics() const
{
    FString Text = FString::Printf(TEXT("garments=%d"), Pieces.Num());
    const UGratiaPlaySettings* Settings = CachedSettings.Get();
    for (int32 I = 0; I < Pieces.Num() && Settings && I < Settings->Garments.Num(); ++I)
        Text += FString::Printf(TEXT(" %s=%.2f%s%s"), *Settings->Garments[I].Name.ToString(), Pieces[I].State.Progress,
            Pieces[I].State.bComplete ? TEXT("(undone)") : TEXT(""), Pieces[I].HandleBone.IsNone() ? TEXT("(off)") : TEXT(""));
    return Text;
}
