#include "GratiaChannelShots.h"
#include "GratiaCharacterProfile.h"
#include "GratiaPenetration.h"
#include "GratiaPenetrator.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaStage1Runtime.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaChannelShots, Log, All);

namespace
{
constexpr int32 GratiaShotCases = 7;
constexpr int32 GratiaShotViews = 2;
}

UGratiaChannelShots::UGratiaChannelShots()
{
    PrimaryComponentTick.bCanEverTick = true;
    // After the runtime solved the shafts this frame.
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

FString UGratiaChannelShots::CaseName(int32 Case) const
{
    static const TCHAR* Names[GratiaShotCases] = {TEXT("Empty"), TEXT("Fingers"), TEXT("Hand"), TEXT("Fist"), TEXT("TwoHands"), TEXT("PrimitiveXXL"), TEXT("Deep4XL")};
    return Names[FMath::Clamp(Case, 0, GratiaShotCases - 1)];
}

bool UGratiaChannelShots::Arrange(int32 Channel, int32 Case)
{
    auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    UGratiaPenetration* Penetration = Runtime && Runtime->TargetCharacter.IsValid() ? Runtime->TargetCharacter->Penetration.Get() : nullptr;
    FName Name;
    FVector Entrance, Inward;
    double Depth = 0.0;
    if (!Penetration || !Penetration->GetChannelFrame(Channel, Name, Entrance, Inward, Depth)) return false;
    for (AGratiaPenetrator* Shaft : Shafts) if (Shaft) Shaft->Destroy();
    Shafts.Reset();
    using namespace GratiaPenetration;
    TArray<TPair<FGratiaShaftSize, FShaft>> Shapes;
    auto Hand = [&Shapes](EHandShape Shape, const TCHAR* Label)
    {
        FGratiaShaftSize Size; Size.Name = Label;
        Shapes.Emplace(Size, HandShaft(Shape));
    };
    if (Case == 1) Hand(EHandShape::Fingers, TEXT("fingers"));
    if (Case == 2) Hand(EHandShape::Hand, TEXT("hand"));
    if (Case == 3) Hand(EHandShape::Fist, TEXT("fist"));
    if (Case == 4) { Hand(EHandShape::Fingers, TEXT("fingers")); Hand(EHandShape::Fingers, TEXT("fingers")); }
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const int32 Count = Case >= 5 ? 1 : Shapes.Num();
    for (int32 Index = 0; Index < Count; ++Index)
    {
        AGratiaPenetrator* Shaft = GetWorld()->SpawnActor<AGratiaPenetrator>(AGratiaPenetrator::StaticClass(), FTransform::Identity, Parameters);
        if (!Shaft) continue;
        // The primitive: XXL at the entrance, the largest (4XL) deep.
        if (Case >= 5) Shaft->SetSize(Case == 5 ? Shaft->Sizes.Num() - 3 : Shaft->Sizes.Num() - 1);
        else
        {
            Shaft->SetShape(Shapes[Index].Key.Name, Shapes[Index].Value);
            Shaft->bAnchorWhenReleased = false;
        }
        Shaft->SetHeld(Index);
        Shafts.Add(Shaft);
    }
    Runtime->QAShafts = Shafts;
    UE_LOG(LogGratiaChannelShots, Display, TEXT("CHANNEL_SHOT arrange channel=%s case=%s shafts=%d"), *Name.ToString(), *CaseName(Case), Shafts.Num());
    return true;
}

void UGratiaChannelShots::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    UGratiaPenetration* Penetration = Runtime && Runtime->TargetCharacter.IsValid() ? Runtime->TargetCharacter->Penetration.Get() : nullptr;
    APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0);
    if (!Penetration || !Player) return;
    Seconds += Delta;
    // Let the studio, the character and its channels settle first.
    if (Step == 0 && (Seconds < 5.0f || Penetration->GetChannelCount() == 0)) return;
    const int32 Total = Penetration->GetChannelCount() * GratiaShotCases * GratiaShotViews;
    const int32 Shot = Step == 0 ? 0 : Step - 1;
    if (Step == 0) { Step = 1; Seconds = 0.0f; }
    if (Shot >= Total)
    {
        if (Seconds > 1.0f) UKismetSystemLibrary::QuitGame(this, Player, EQuitPreference::Quit, false);
        return;
    }
    const int32 Channel = Shot / (GratiaShotCases * GratiaShotViews);
    const int32 Case = (Shot / GratiaShotViews) % GratiaShotCases;
    const int32 ViewIndex = Shot % GratiaShotViews;
    FName Name;
    FVector Entrance, Inward;
    double Depth = 0.0;
    if (!Penetration->GetChannelFrame(Channel, Name, Entrance, Inward, Depth)) { ++Step; Seconds = 0.0f; return; }
    // Tip 1 cm before the entrance until every shaft is captured (or 3 s passed), then in at 25 cm/s to its depth
    // (two hands side by side). The deep case first shows the belly with the shaft held at the entrance, then goes in.
    const bool bFirstFrame = Seconds <= Delta + UE_SMALL_NUMBER;
    const bool bBaseline = Case == 6 && ViewIndex == 0;
    if (ViewIndex == 0 && bFirstFrame) { Arrange(Channel, Case); Inserting = -1.0f; }
    if (Case == 6 && ViewIndex == 1 && bFirstFrame) Inserting = 0.0f;
    const double Target = Case == 3 ? 4.0 : Case == 5 ? 8.0 : Case == 6 ? Depth * 0.85 : 6.0;
    constexpr double Speed = 25.0;
    if (ViewIndex == 0 && Inserting < 0.0f)
    {
        // Engagements of the previous case's shafts are dropped by the next solve.
        if (Seconds > 0.1f && Penetration->GetEngagementCount() >= Shafts.Num()) Inserting = 0.0f;
        else if (Seconds > 3.0f)
        {
            UE_LOG(LogGratiaChannelShots, Warning, TEXT("CHANNEL_SHOT not captured channel=%s case=%s engaged=%d of %d"), *Name.ToString(),
                *CaseName(Case), Penetration->GetEngagementCount(), Shafts.Num());
            Inserting = 0.0f;
        }
    }
    else Inserting += Delta;
    const double TipDepth = Inserting < 0.0f || bBaseline ? -1.0 : FMath::Min(Target, -1.0 + Inserting * Speed);
    const AActor* Body = Runtime->TargetCharacter.Get();
    const FVector Side = Body->GetActorRightVector();
    for (int32 Index = 0; Index < Shafts.Num(); ++Index)
    {
        AGratiaPenetrator* Shaft = Shafts[Index];
        if (!Shaft) continue;
        const double Length = Shaft->GetShaft().Length;
        const FVector Offset = Shafts.Num() > 1 ? Side * (Index == 0 ? -1.6 : 1.6) : FVector::ZeroVector;
        Shaft->SetBase(FTransform(FRotationMatrix::MakeFromX(Inward).ToQuat(), Entrance + Offset - Inward * (Length - TipDepth)));
    }
    // Views: from outside the channel (in front of or behind the body, a little below) and from the side.
    const FVector Up = FVector::UpVector;
    FVector Out = -Inward;
    Out.Z = 0.0;
    if (!Out.Normalize()) Out = Body->GetActorForwardVector();
    FVector Eye = ViewIndex == 0 ? Entrance + Out * 30.0 - Up * 10.0 : Entrance + Side * 30.0 + Out * 6.0 - Up * 4.0;
    FVector Look = Entrance + Inward * 2.0;
    if (Case == 6)
    {
        // The belly in front of the deep shaft, before and after, three-quarter from the side (a hanging arm hides it
        // from straight beside it; the character's own forward axis).
        const UGratiaCharacterProfile* Profile = Runtime->TargetCharacter->CharacterProfile.Get();
        const USceneComponent* Mesh = Runtime->TargetCharacter->CharacterMesh.Get();
        const FTransform Frame = Mesh ? Mesh->GetComponentTransform() : Body->GetActorTransform();
        const FVector Forward = Frame.TransformVectorNoScale(Profile ? Profile->ForwardAxis : FVector::RightVector).GetSafeNormal2D();
        const FVector Across = FVector::CrossProduct(FVector::UpVector, Forward);
        Look = Entrance + Inward * (Depth * 0.5);
        Eye = Look + Across * 50.0 + Forward * 50.0;
    }
    if (!View)
    {
        View = GetWorld()->SpawnActor<ACameraActor>(Eye, (Entrance - Eye).Rotation());
        if (View) View->GetCameraComponent()->FieldOfView = 45.0f;
    }
    if (View)
    {
        View->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
        if (Player->GetViewTarget() != View) Player->SetViewTarget(View);
    }
    // Walls and morphs settle 1.2 s after the shafts reached their depth.
    const bool bInsertView = Case == 6 ? ViewIndex == 1 : ViewIndex == 0;
    if (Inserting < 0.0f || (bBaseline ? Inserting < 0.5f : bInsertView ? Inserting < (Target + 1.0) / Speed + 1.2 : Seconds < 0.5f)) return;
    const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/ChannelShots"),
        FString::Printf(TEXT("Channel_%s_%s_%s.png"), *Name.ToString(), *CaseName(Case),
        Case == 6 ? (ViewIndex == 0 ? TEXT("BellyBefore") : TEXT("Belly")) : ViewIndex == 0 ? TEXT("Axis") : TEXT("Side")));
    FScreenshotRequest::RequestScreenshot(File, false, false);
    UE_LOG(LogGratiaChannelShots, Display, TEXT("CHANNEL_SHOT %s %s"), *File, *Penetration->GetDiagnostics());
    ++Step;
    Seconds = 0.0f;
}

void UGratiaChannelShots::EndPlay(const EEndPlayReason::Type Reason)
{
    for (AGratiaPenetrator* Shaft : Shafts) if (Shaft) Shaft->Destroy();
    Shafts.Reset();
    if (auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner())) Runtime->QAShafts.Reset();
    if (View) View->Destroy();
    Super::EndPlay(Reason);
}
