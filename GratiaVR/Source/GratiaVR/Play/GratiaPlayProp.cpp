#include "GratiaPlayProp.h"
#include "GratiaPlaySubsystem.h"
#include "GratiaArousal.h"
#include "GratiaHapticLayers.h"
#include "GratiaVocalLayer.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaStage1Runtime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaProp, Log, All);

namespace
{
    UGratiaHapticLayers* PropHaptics(const UObject* Context)
    {
        const UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(Context);
        AGratiaStage1Runtime* Stage = Play ? Play->GetRuntime() : nullptr;
        return Stage ? Stage->FindComponentByClass<UGratiaHapticLayers>() : nullptr;
    }
}

AGratiaPlayProp::AGratiaPlayProp()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    // Unscaled root: the tip and attach offsets are centimetres in the prop's own space; the shape is scaled below it.
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetupAttachment(RootComponent);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
}

void AGratiaPlayProp::Setup(const FGratiaPropDefinition& InDefinition, AGratiaPreviewCharacter* InCharacter)
{
    Definition = InDefinition;
    Character = InCharacter;
    UStaticMesh* Shape = Definition.Mesh.Get();
    if (!Shape)
    {
        const TCHAR* Path = Definition.Kind == EGratiaPropKind::Accessory ? TEXT("/Engine/BasicShapes/Cube.Cube")
            : Definition.Kind == EGratiaPropKind::Oil ? TEXT("/Engine/BasicShapes/Cylinder.Cylinder")
            : Definition.Kind == EGratiaPropKind::Feather ? TEXT("/Engine/BasicShapes/Cube.Cube") : TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
        Shape = LoadObject<UStaticMesh>(nullptr, Path);
    }
    Mesh->SetStaticMesh(Shape);
    Mesh->SetRelativeScale3D(Definition.Mesh ? FVector::OneVector : Definition.Scale);
    UMaterialInterface* Base = Definition.Material ? Definition.Material.Get() : LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (Base)
    {
        Material = UMaterialInstanceDynamic::Create(Base, this);
        Material->SetVectorParameterValue(TEXT("Color"), Definition.Color);
        Mesh->SetMaterial(0, Material);
    }
    Home = GetActorLocation();
    LastTip = GetActorTransform().TransformPosition(Definition.Tip);
    if (!Shape) UE_LOG(LogGratiaProp, Warning, TEXT("PROP %s has no mesh."), *Definition.Name.ToString());
}

void AGratiaPlayProp::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Hand != INDEX_NONE)
        if (UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this)) Play->ReleaseHand(Hand == 0, this);
    Hand = INDEX_NONE;
    Super::EndPlay(Reason);
}

void AGratiaPlayProp::Grab(int32 HandIndex, const FTransform& HandWorld)
{
    if (bAttached) DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    bAttached = false;
    const FTransform Shown(Mesh->GetComponentRotation(), Mesh->GetComponentLocation());
    Mesh->SetSimulatePhysics(false);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // Physics moved the shape away from the root while it lay dropped: the root takes its place again.
    if (bDropped)
    {
        Mesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
        SetActorTransform(Shown);
        Mesh->SetRelativeTransform(FTransform(FQuat::Identity, FVector::ZeroVector, Mesh->GetRelativeScale3D()));
    }
    bDropped = false;
    Hand = HandIndex;
    HoldRelative = GetActorTransform().GetRelativeTransform(HandWorld);
    if (const AGratiaPreviewCharacter* Wearer = Character.Get())
        if (UGratiaVocalLayer* Vocal = Wearer->FindComponentByClass<UGratiaVocalLayer>()) Vocal->PlayFoley(EGratiaFoleyBank::PropPickup, GetActorLocation());
    if (UGratiaHapticLayers* PropLayers = PropHaptics(this)) PropLayers->Pulse(HandIndex == 0, 0.3f, 0.4f, 0.04f);
    UE_LOG(LogGratiaProp, Display, TEXT("PROP %s taken hand=%s"), *Definition.Name.ToString(), HandIndex == 0 ? TEXT("L") : TEXT("R"));
}

void AGratiaPlayProp::Release(const FVector& Velocity)
{
    if (UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this)) Play->ReleaseHand(Hand == 0, this);
    Hand = INDEX_NONE;
    TouchedZone = INDEX_NONE;
    AGratiaPreviewCharacter* Wearer = Character.Get();
    if (Definition.Kind == EGratiaPropKind::Accessory && Wearer && Wearer->CharacterMesh && Wearer->CharacterProfile)
    {
        const FName Bone = Wearer->CharacterProfile->ResolveBone(Definition.AttachSemantic);
        if (!Bone.IsNone() && Wearer->CharacterMesh->GetBoneIndex(Bone) != INDEX_NONE
            && FVector::Distance(GetActorLocation(), Wearer->CharacterMesh->GetSocketTransform(Bone).TransformPosition(Definition.AttachTransform.GetLocation())) <= 15.0)
        {
            AttachToComponent(Wearer->CharacterMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Bone);
            SetActorRelativeTransform(Definition.AttachTransform);
            bAttached = true;
            UE_LOG(LogGratiaProp, Display, TEXT("PROP %s attached to %s"), *Definition.Name.ToString(), *Bone.ToString());
            return;
        }
    }
    Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Mesh->SetSimulatePhysics(true);
    Mesh->SetPhysicsLinearVelocity(Velocity.GetClampedToMaxSize(400.0));
    bDropped = true;
    if (Wearer)
        if (UGratiaVocalLayer* Vocal = Wearer->FindComponentByClass<UGratiaVocalLayer>()) Vocal->PlayFoley(EGratiaFoleyBank::PropDrop, GetActorLocation());
}

void AGratiaPlayProp::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this);
    if (!Play) return;
    if (Hand != INDEX_NONE)
    {
        const UGratiaPlaySubsystem::FHand& Holder = Play->GetHand(Hand == 0);
        if (!Holder.bAllowed || Holder.Grip < 0.35f || !Play->IsHandOwnedBy(Hand == 0, this)) { Release(Holder.Velocity); return; }
        SetActorTransform(HoldRelative * Holder.World);
        if (Definition.Kind == EGratiaPropKind::Toy && Holder.Trigger >= 0.6f && PrevTrigger < 0.6f) Mode = (Mode + 1) % 4;
        UseOnBody(DeltaSeconds);
        PrevTrigger = Holder.Trigger;
        return;
    }
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const bool bLeft = Index == 0;
        const UGratiaPlaySubsystem::FHand& Candidate = Play->GetHand(bLeft);
        const bool bPressed = Candidate.Grip >= 0.6f && PrevGrip[Index] < 0.6f;
        PrevGrip[Index] = Candidate.Grip;
        if (!bPressed || !Candidate.bAllowed || Play->GetHandUse(bLeft) != EGratiaHandUse::None) continue;
        if (FVector::Distance(Mesh->GetComponentLocation(), Candidate.World.GetLocation()) > 12.0) continue;
        const UGratiaArousal* Arousal = Character.IsValid() ? Character->FindComponentByClass<UGratiaArousal>() : nullptr;
        if (Arousal && !Arousal->IsUnlocked(Definition.RequiredUnlock))
        {
            // Not yet: a short double tick in the hand instead of the pickup.
            if (UGratiaHapticLayers* PropLayers = PropHaptics(this)) PropLayers->Pulse(bLeft, 0.35f, 0.2f, 0.06f);
            continue;
        }
        if (Play->ClaimHand(bLeft, EGratiaHandUse::Prop, this)) { Grab(Index, Candidate.World); return; }
    }
    if (!bDropped && !bAttached) SetActorLocation(Home + FVector(0, 0, 0.6 * FMath::Sin(Age * 1.5)));
}

void AGratiaPlayProp::UseOnBody(float Delta)
{
    AGratiaPreviewCharacter* Wearer = Character.Get();
    UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(this);
    const UGratiaPlaySettings* Settings = UGratiaPlaySubsystem::GetSettings(Wearer);
    if (!Wearer || !Play || !Settings || !Wearer->Interaction || Delta <= 0.0f) return;
    const FVector Tip = GetActorTransform().TransformPosition(Definition.Tip);
    const float Speed = float(FVector::Distance(Tip, LastTip) / FMath::Max(Delta, 1.0e-3f));
    LastTip = Tip;
    const UGratiaInteraction* Interaction = Wearer->Interaction;
    const double Scale = FMath::Max(0.01, double(Wearer->GetActorScale3D().GetAbs().GetMax()));
    int32 Zone = INDEX_NONE;
    double Gap = 1.0e6;
    for (int32 I = 0; I < Interaction->Zones.Num(); ++I)
    {
        const FGratiaContactZone& Definition_ = Interaction->Zones[I];
        if (Definition_.bSceneActor || Definition_.Radius <= 0.0f) continue;
        const double ZoneGap = FVector::Distance(Tip, Interaction->GetZoneWorldPosition(I)) - Definition_.Radius * Scale;
        if (ZoneGap < Gap) { Gap = ZoneGap; Zone = I; }
    }
    const bool bTouch = Zone != INDEX_NONE && Gap <= 1.5;
    const FName ZoneName = bTouch ? Interaction->Zones[Zone].Name : NAME_None;
    UGratiaArousal* Arousal = Wearer->FindComponentByClass<UGratiaArousal>();
    UGratiaHapticLayers* PropLayers = PropHaptics(this);
    const bool bLeft = Hand == 0;

    float Buzz = 0.0f, BuzzFrequency = 0.6f;
    if (Definition.Kind == EGratiaPropKind::Toy)
    {
        Buzz = Mode == 1 ? 0.25f : Mode == 2 ? (FMath::Fmod(Age, 0.5) < 0.25 ? 0.45f : 0.0f) : Mode == 3 ? 0.55f : 0.0f;
        BuzzFrequency = Mode == 3 ? 0.9f : 0.6f;
        if (bTouch && Buzz > 0.0f) Buzz = FMath::Min(1.0f, Buzz + 0.2f);
    }
    else if (Definition.Kind == EGratiaPropKind::Feather && bTouch && Speed > 2.0f) { Buzz = 0.06f; BuzzFrequency = 0.95f; }
    if (PropLayers && Buzz > 0.0f) PropLayers->SetSustain(bLeft, Definition.Name, Buzz, BuzzFrequency);

    if (bTouch && Arousal && Definition.Kind != EGratiaPropKind::Oil && Definition.Kind != EGratiaPropKind::Accessory)
    {
        // A running toy stimulates without being moved: it counts as a calm stroke.
        const float Factor = Definition.Kind == EGratiaPropKind::Toy ? (Mode == 0 ? 0.3f : Mode == 3 ? 1.5f : 1.0f) : 1.0f;
        const float Effective = Definition.Kind == EGratiaPropKind::Toy && Mode > 0 ? FMath::Max(Speed, 12.0f) : Speed;
        Arousal->AddStimulus(ZoneName, Effective, Definition.Stimulus * Factor);
    }
    if (bTouch && TouchedZone != Zone && Wearer->Interaction) Wearer->Interaction->ExternalReaction(ZoneName, Hand, Speed);
    TouchedZone = bTouch ? Zone : INDEX_NONE;

    if (Definition.Kind == EGratiaPropKind::Oil)
    {
        const UGratiaPlaySubsystem::FHand& Holder = Play->GetHand(bLeft);
        const bool bPouring = Holder.Trigger >= 0.5f && Zone != INDEX_NONE && Gap <= 30.0;
        if (bPouring && PrevTrigger < 0.5f)
            if (UGratiaVocalLayer* Vocal = Wearer->FindComponentByClass<UGratiaVocalLayer>()) Vocal->PlayFoley(EGratiaFoleyBank::OilPour, Tip);
        if (bPouring && !Settings->OilParameter.IsNone() && Wearer->CharacterMesh)
        {
            Oil = FMath::Min(1.0f, Oil + Settings->OilPerSecond * Delta);
            Wearer->CharacterMesh->SetScalarParameterValueOnMaterials(Settings->OilParameter, Oil);
            if (PropLayers) PropLayers->SetSustain(bLeft, Definition.Name, 0.08f, 0.3f);
        }
    }
}
