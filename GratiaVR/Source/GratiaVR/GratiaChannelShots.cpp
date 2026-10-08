#include "GratiaChannelShots.h"
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
constexpr int32 GratiaShotCases = 6;
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
    static const TCHAR* Names[GratiaShotCases] = {TEXT("Empty"), TEXT("Fingers"), TEXT("Hand"), TEXT("Fist"), TEXT("TwoHands"), TEXT("PrimitiveXXL")};
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
    const int32 Count = Case == 5 ? 1 : Shapes.Num();
    for (int32 Index = 0; Index < Count; ++Index)
    {
        AGratiaPenetrator* Shaft = GetWorld()->SpawnActor<AGratiaPenetrator>(AGratiaPenetrator::StaticClass(), FTransform::Identity, Parameters);
        if (!Shaft) continue;
        if (Case == 5) Shaft->SetSize(Shaft->Sizes.Num() - 1);
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
    if (ViewIndex == 0 && Seconds <= Delta + UE_SMALL_NUMBER) Arrange(Channel, Case);
    // Tip 1 cm before the entrance for the capture, then in to its depth (two hands side by side).
    const AActor* Body = Runtime->TargetCharacter.Get();
    const FVector Side = Body->GetActorRightVector();
    for (int32 Index = 0; Index < Shafts.Num(); ++Index)
    {
        AGratiaPenetrator* Shaft = Shafts[Index];
        if (!Shaft) continue;
        const double Length = Shaft->GetShaft().Length;
        const double TipDepth = ViewIndex == 0 && Seconds < 0.2f ? -1.0 : Case == 3 ? 4.0 : Case == 5 ? 8.0 : 6.0;
        const FVector Offset = Shafts.Num() > 1 ? Side * (Index == 0 ? -1.6 : 1.6) : FVector::ZeroVector;
        Shaft->SetBase(FTransform(FRotationMatrix::MakeFromX(Inward).ToQuat(), Entrance + Offset - Inward * (Length - TipDepth)));
    }
    // Views: from outside the channel (in front of or behind the body, a little below) and from the side.
    const FVector Up = FVector::UpVector;
    FVector Out = -Inward;
    Out.Z = 0.0;
    if (!Out.Normalize()) Out = Body->GetActorForwardVector();
    const FVector Eye = ViewIndex == 0 ? Entrance + Out * 30.0 - Up * 10.0 : Entrance + Side * 30.0 + Out * 6.0 - Up * 4.0;
    if (!View)
    {
        View = GetWorld()->SpawnActor<ACameraActor>(Eye, (Entrance - Eye).Rotation());
        if (View) View->GetCameraComponent()->FieldOfView = 45.0f;
    }
    if (View)
    {
        View->SetActorLocationAndRotation(Eye, (Entrance + Inward * 2.0 - Eye).Rotation());
        if (Player->GetViewTarget() != View) Player->SetViewTarget(View);
    }
    const float Wait = ViewIndex == 0 ? 1.4f : 0.5f;
    if (Seconds < Wait) return;
    const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/ChannelShots"),
        FString::Printf(TEXT("Channel_%s_%s_%s.png"), *Name.ToString(), *CaseName(Case), ViewIndex == 0 ? TEXT("Axis") : TEXT("Side")));
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
