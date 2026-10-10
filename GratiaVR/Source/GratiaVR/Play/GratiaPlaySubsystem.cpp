#include "GratiaPlaySubsystem.h"
#include "GratiaPlaySettings.h"
#include "GratiaArousal.h"
#include "GratiaPlayBody.h"
#include "GratiaGarments.h"
#include "GratiaHapticLayers.h"
#include "GratiaVocalLayer.h"
#include "GratiaPlayProp.h"
#include "GratiaStage1Runtime.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaHandInput.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "MotionControllerComponent.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaPlay, Log, All);

static TAutoConsoleVariable<int32> CVarGratiaPlay(TEXT("gratia.Play"), 1,
    TEXT("Interaction layer: limb grabs, ground IK, press softening, anime soft tuning, garments, arousal, haptic layers, breath/foley, props. 0 off, 1 on."));

namespace
{
    template <class T>
    T* EnsureComponent(AActor* Owner, const TCHAR* Name)
    {
        if (T* Found = Owner->FindComponentByClass<T>()) return Found;
        T* Component = NewObject<T>(Owner, Name);
        Owner->AddInstanceComponent(Component);
        Component->RegisterComponent();
        return Component;
    }

    UMotionControllerComponent* FindController(APawn* Pawn, bool bLeft)
    {
        if (!Pawn) return nullptr;
        TInlineComponentArray<UMotionControllerComponent*> Controllers(Pawn);
        const TCHAR* Side = bLeft ? TEXT("Left") : TEXT("Right");
        UMotionControllerComponent* Best = nullptr;
        int32 BestScore = -1;
        for (UMotionControllerComponent* Controller : Controllers)
        {
            const FString Source = Controller ? Controller->MotionSource.ToString() : FString();
            if (!Source.StartsWith(Side)) continue;
            // The grip pose is the hand; the aim pose points forward from it.
            const int32 Score = Source.Equals(Side) || Source.EndsWith(TEXT("Grip")) ? 2 : Source.Contains(TEXT("Aim")) ? 0 : 1;
            if (Score > BestScore) { Best = Controller; BestScore = Score; }
        }
        return Best;
    }

    FAutoConsoleCommandWithWorldAndArgs GratiaPlayPropsCommand(TEXT("gratia.Play.Props"),
        TEXT("Show (1) or remove (0) the interaction props in front of the character; no argument toggles."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(World))
                Play->SetPropsShown(Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !Play->ArePropsShown());
        }));

    FAutoConsoleCommandWithWorldAndArgs GratiaPlayArousalCommand(TEXT("gratia.Play.Arousal"),
        TEXT("Set the target character's arousal meter (0..1) for testing stages and unlocks."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(World);
            AGratiaPreviewCharacter* Character = Play ? Play->GetCharacter() : nullptr;
            UGratiaArousal* Arousal = Character ? Character->FindComponentByClass<UGratiaArousal>() : nullptr;
            if (Arousal && Args.Num() > 0) Arousal->SetArousal(FCString::Atof(*Args[0]));
        }));

    FAutoConsoleCommandWithWorldAndArgs GratiaPlayGarmentsCommand(TEXT("gratia.Play.ResetGarments"),
        TEXT("Put every garment piece of the target character back on."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(World);
            AGratiaPreviewCharacter* Character = Play ? Play->GetCharacter() : nullptr;
            if (UGratiaGarments* Garments = Character ? Character->FindComponentByClass<UGratiaGarments>() : nullptr) Garments->ResetGarments();
        }));

    FAutoConsoleCommandWithWorldAndArgs GratiaPlayReportCommand(TEXT("gratia.Play.Report"),
        TEXT("Log the interaction layer's state (hands, limbs, garments, arousal, voice banks)."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UGratiaPlaySubsystem* Play = UGratiaPlaySubsystem::Get(World))
                UE_LOG(LogGratiaPlay, Display, TEXT("PLAY %s"), *Play->GetDiagnostics());
        }));
}

UGratiaPlaySubsystem* UGratiaPlaySubsystem::Get(const UObject* WorldContext)
{
    const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    return World ? World->GetSubsystem<UGratiaPlaySubsystem>() : nullptr;
}

bool UGratiaPlaySubsystem::IsLayerEnabled() { return CVarGratiaPlay.GetValueOnGameThread() != 0; }

bool UGratiaPlaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UGratiaPlaySubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UGratiaPlaySubsystem, STATGROUP_Tickables);
}

void UGratiaPlaySubsystem::Deinitialize()
{
    SetPropsShown(false);
    Super::Deinitialize();
}

const UGratiaPlaySettings* UGratiaPlaySubsystem::GetSettings(const AGratiaPreviewCharacter* Character)
{
    const UGratiaCharacterProfile* Profile = Character ? Character->CharacterProfile.Get() : nullptr;
    if (Profile && Profile->PlaySettings) return Profile->PlaySettings;
    return GetDefault<UGratiaPlaySettings>();
}

AGratiaPreviewCharacter* UGratiaPlaySubsystem::GetCharacter() const
{
    const AGratiaStage1Runtime* Stage = Runtime.Get();
    return Stage ? Stage->TargetCharacter.Get() : nullptr;
}

bool UGratiaPlaySubsystem::GetHead(FVector& Out) const
{
    const AGratiaStage1Runtime* Stage = Runtime.Get();
    const UCameraComponent* Camera = Stage ? Stage->GetPlayerCamera() : nullptr;
    if (!Camera) return false;
    Out = Camera->GetComponentLocation();
    return true;
}

void UGratiaPlaySubsystem::Tick(float DeltaTime)
{
    BindSeconds -= DeltaTime;
    if (BindSeconds <= 0.0f || !Runtime.IsValid() || GetCharacter() != BoundCharacter.Get())
    {
        BindSeconds = 1.0f;
        Bind();
    }
    for (int32 I = Props.Num() - 1; I >= 0; --I)
        if (!Props[I].IsValid()) Props.RemoveAt(I);
}

void UGratiaPlaySubsystem::Bind()
{
    if (!Runtime.IsValid())
        for (TActorIterator<AGratiaStage1Runtime> It(GetWorld()); It; ++It) { Runtime = *It; break; }
    AGratiaStage1Runtime* Stage = Runtime.Get();
    if (!Stage) return;
    EnsureComponent<UGratiaHapticLayers>(Stage, TEXT("GratiaHapticLayers"));
    AGratiaPreviewCharacter* Character = GetCharacter();
    if (Character != BoundCharacter.Get())
    {
        ReleaseHand(true, nullptr);
        ReleaseHand(false, nullptr);
        SetPropsShown(false);
    }
    BoundCharacter = Character;
    if (!Character) return;
    EnsureComponent<UGratiaArousal>(Character, TEXT("GratiaArousal"));
    EnsureComponent<UGratiaPlayBody>(Character, TEXT("GratiaPlayBody"));
    EnsureComponent<UGratiaGarments>(Character, TEXT("GratiaGarments"));
    EnsureComponent<UGratiaVocalLayer>(Character, TEXT("GratiaVocalLayer"));
}

const UGratiaPlaySubsystem::FHand& UGratiaPlaySubsystem::GetHand(bool bLeft)
{
    if (HandsFrame != GFrameCounter) RefreshHands();
    return Hands[bLeft ? 0 : 1];
}

void UGratiaPlaySubsystem::RefreshHands()
{
    HandsFrame = GFrameCounter;
    const UWorld* World = GetWorld();
    const double Now = World ? World->GetTimeSeconds() : 0.0;
    const float Dt = float(FMath::Clamp(Now - LastHandsTime, 0.0, 0.1));
    LastHandsTime = Now;
    AGratiaStage1Runtime* Stage = Runtime.Get();
    APawn* Pawn = Stage ? Stage->GetPlayerPawn() : nullptr;
    const AGratiaPreviewCharacter* Character = GetCharacter();
    const UGratiaInteraction* Interaction = Character ? Character->Interaction.Get() : nullptr;
    const double Scale = Character ? FMath::Max(0.01, double(Character->GetActorScale3D().GetAbs().GetMax())) : 1.0;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const bool bLeft = Index == 0;
        FHand& Hand = Hands[Index];
        UMotionControllerComponent* Controller = FindController(Pawn, bLeft);
        Hand.bAllowed = Stage && Controller && IsLayerEnabled() && Stage->IsHandInteractionAllowed(bLeft) && Stage->IsSceneInteractionAllowed();
        Hand.Grip = Stage && Stage->HandInput ? Stage->HandInput->GetGrip(bLeft) : 0.0f;
        Hand.Trigger = Stage && Stage->HandInput ? Stage->HandInput->GetTrigger(bLeft) : 0.0f;
        if (!Hand.bAllowed)
        {
            Hand.Velocity = FVector::ZeroVector;
            Hand.Speed = 0.0f;
            Hand.Zone = INDEX_NONE;
            Hand.ZoneName = NAME_None;
            Hand.ZoneGap = 1.0e6f;
            bHadPosition[Index] = false;
            if (HandOwner[Index].IsValid() || HandUse[Index] != EGratiaHandUse::None) ReleaseHand(bLeft, nullptr);
            continue;
        }
        Hand.World = Controller->GetComponentTransform();
        const FVector Position = Hand.World.GetLocation();
        const FVector Velocity = bHadPosition[Index] && Dt > 0.0f ? (Position - LastPosition[Index]) / Dt : FVector::ZeroVector;
        // A short low-pass removes tracking noise without delaying a stroke noticeably.
        Hand.Velocity = FMath::Lerp(Hand.Velocity, Velocity.ContainsNaN() ? FVector::ZeroVector : Velocity, Dt > 0.0f ? 1.0f - FMath::Exp(-Dt / 0.04f) : 1.0f);
        Hand.Speed = float(Hand.Velocity.Size());
        Hand.Travel += Hand.Speed * Dt;
        LastPosition[Index] = Position;
        bHadPosition[Index] = true;

        Hand.Zone = INDEX_NONE;
        Hand.ZoneName = NAME_None;
        Hand.ZoneGap = 1.0e6f;
        if (!Interaction) continue;
        for (int32 Zone = 0; Zone < Interaction->Zones.Num(); ++Zone)
        {
            const FGratiaContactZone& Definition = Interaction->Zones[Zone];
            if (Definition.bSceneActor || Definition.Radius <= 0.0f) continue;
            const float Gap = float(FVector::Distance(Position, Interaction->GetZoneWorldPosition(Zone)) - Definition.Radius * Scale);
            // Gap is from the grip pose: the palm is a few centimetres ahead of it.
            if (Gap < Hand.ZoneGap && Gap <= 6.0f) { Hand.Zone = Zone; Hand.ZoneName = Definition.Name; Hand.ZoneGap = Gap - 3.0f; }
        }
    }
}

bool UGratiaPlaySubsystem::ClaimHand(bool bLeft, EGratiaHandUse Use, const UObject* Owner)
{
    const int32 Index = bLeft ? 0 : 1;
    if (HandUse[Index] != EGratiaHandUse::None && HandOwner[Index].IsValid() && HandOwner[Index].Get() != Owner) return false;
    HandUse[Index] = Use;
    HandOwner[Index] = Owner;
    return true;
}

void UGratiaPlaySubsystem::ReleaseHand(bool bLeft, const UObject* Owner)
{
    const int32 Index = bLeft ? 0 : 1;
    if (Owner && HandOwner[Index].Get() != Owner) return;
    HandUse[Index] = EGratiaHandUse::None;
    HandOwner[Index] = nullptr;
}

void UGratiaPlaySubsystem::SetPropsShown(bool bShown)
{
    for (const TWeakObjectPtr<AGratiaPlayProp>& Prop : Props)
        if (Prop.IsValid()) Prop->Destroy();
    Props.Reset();
    AGratiaPreviewCharacter* Character = GetCharacter();
    UWorld* World = GetWorld();
    const UGratiaPlaySettings* Settings = GetSettings(Character);
    if (!bShown || !Character || !World || !Settings || !Character->CharacterMesh) return;
    // A floating tray at waist height in front of her, props side by side; dropped props fall with physics.
    const UGratiaCharacterProfile* Profile = Character->CharacterProfile;
    const FTransform MeshTransform = Character->CharacterMesh->GetComponentTransform();
    const FVector Forward = MeshTransform.TransformVectorNoScale(Profile ? Profile->ForwardAxis.GetSafeNormal() : FVector::ForwardVector).GetSafeNormal2D();
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    const FBoxSphereBounds Bounds = Character->CharacterMesh->Bounds;
    const FVector Base = FVector(Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z - Bounds.BoxExtent.Z + 85.0) + Forward * 45.0;
    const int32 Count = Settings->Props.Num();
    for (int32 I = 0; I < Count; ++I)
    {
        const FVector Location = Base + Right * ((I - 0.5 * (Count - 1)) * 14.0);
        FActorSpawnParameters Parameters;
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AGratiaPlayProp* Prop = World->SpawnActor<AGratiaPlayProp>(AGratiaPlayProp::StaticClass(), FTransform(Forward.Rotation(), Location), Parameters);
        if (!Prop) continue;
        Prop->Setup(Settings->Props[I], Character);
        Props.Add(Prop);
    }
    UE_LOG(LogGratiaPlay, Display, TEXT("PLAY props shown=%d"), Props.Num());
}

FString UGratiaPlaySubsystem::GetDiagnostics() const
{
    FString Text = FString::Printf(TEXT("enabled=%d runtime=%d character=%s props=%d"), IsLayerEnabled(), Runtime.IsValid(),
        GetCharacter() ? *GetCharacter()->GetName() : TEXT("none"), Props.Num());
    for (int32 I = 0; I < 2; ++I)
        Text += FString::Printf(TEXT(" | %s allowed=%d speed=%.0f grip=%.2f zone=%s gap=%.1f use=%d"), I == 0 ? TEXT("L") : TEXT("R"),
            Hands[I].bAllowed, Hands[I].Speed, Hands[I].Grip, *Hands[I].ZoneName.ToString(), Hands[I].ZoneGap, int32(HandUse[I]));
    if (const AGratiaPreviewCharacter* Character = GetCharacter())
    {
        if (const UGratiaArousal* Arousal = Character->FindComponentByClass<UGratiaArousal>()) Text += TEXT(" | ") + Arousal->GetDiagnostics();
        if (const UGratiaPlayBody* Body = Character->FindComponentByClass<UGratiaPlayBody>()) Text += TEXT(" | ") + Body->GetDiagnostics();
        if (const UGratiaGarments* Garments = Character->FindComponentByClass<UGratiaGarments>()) Text += TEXT(" | ") + Garments->GetDiagnostics();
        if (const UGratiaVocalLayer* Vocal = Character->FindComponentByClass<UGratiaVocalLayer>()) Text += TEXT(" | ") + Vocal->GetDiagnostics();
    }
    return Text;
}
