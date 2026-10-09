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
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogGratiaChannelShots, Log, All);

namespace
{
constexpr int32 GratiaShotCases = 9;
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
    static const TCHAR* Names[GratiaShotCases] = {TEXT("Empty"), TEXT("Fingers"), TEXT("Hand"), TEXT("Fist"), TEXT("TwoHands"), TEXT("PrimitiveXXL"), TEXT("Deep4XL"),
        TEXT("BeadsXL"), TEXT("KnotL")};
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
        // The primitive: XXL at the entrance, the largest (4XL) deep, XL beads half in, an L knot at the entrance.
        if (Case == 7) { Shaft->SetSize(3); Shaft->SetForm(EGratiaShaftForm::Beads); }
        else if (Case == 8) { Shaft->SetSize(2); Shaft->SetForm(EGratiaShaftForm::Knotted); }
        else if (Case >= 5) Shaft->SetSize(Case == 5 ? Shaft->Sizes.Num() - 3 : Shaft->Sizes.Num() - 1);
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
    // Let the studio, the character and its channels settle first (and, from the editor, new shaders compile: until
    // then the materials draw as the default one).
    if (Step == 0 && (Seconds < 5.0f || Penetration->GetChannelCount() == 0)) return;
#if WITH_EDITOR
    if (Step == 0 && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    const int32 Total = Penetration->GetChannelCount() * GratiaShotCases * GratiaShotViews;
    const int32 Shot = Step == 0 ? 0 : Step - 1;
    if (Step == 0) { Step = 1; Seconds = 0.0f; }
    if (Shot > Total)
    {
        if (Seconds > 1.0f) UKismetSystemLibrary::QuitGame(this, Player, EQuitPreference::Quit, false);
        return;
    }
    if (Shot == Total)
    {
        ShootForms(Player);
        return;
    }
    const int32 Channel = Shot / (GratiaShotCases * GratiaShotViews);
    const int32 Case = (Shot / GratiaShotViews) % GratiaShotCases;
    const int32 ViewIndex = Shot % GratiaShotViews;
    FName Name;
    FVector Entrance, Inward;
    double Depth = 0.0;
    if (!Penetration->GetChannelFrame(Channel, Name, Entrance, Inward, Depth)) { ++Step; Seconds = 0.0f; return; }
    // Tip 1 cm before the entrance until every shaft is captured (or 3 s passed), then the hand pushes in at 25 cm/s
    // until the shafts reach their depth against the channel's resistance (at most its largest lag further), like a
    // hand would (two hands side by side). The deep case first shows the belly with the shaft held at the entrance.
    const bool bFirstFrame = Seconds <= Delta + UE_SMALL_NUMBER;
    const bool bBaseline = Case == 6 && ViewIndex == 0;
    if (ViewIndex == 0 && bFirstFrame) { Arrange(Channel, Case); Inserting = -1.0f; HandDepth = -1.0; Settled = 0.0f; }
    if (Case == 6 && ViewIndex == 1 && bFirstFrame) { Inserting = 0.0f; HandDepth = -1.0; Settled = 0.0f; }
    double Target = Case == 3 ? 4.0 : Case == 5 ? 8.0 : Case == 6 ? Depth * 0.85 : Case == 7 ? 12.0 : 6.0;
    if (Case == 8 && !Shafts.IsEmpty() && Shafts[0]) Target = 0.8 * Shafts[0]->GetShaft().Length;
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
    const bool bInsertView = Case == 6 ? ViewIndex == 1 : ViewIndex == 0;
    if (bInsertView && Inserting >= 0.0f)
    {
        double Shallowest = Target;
        for (const AGratiaPenetrator* Shaft : Shafts)
        {
            FVector ShaftEntrance, ShaftInward;
            double Inserted = 0.0, ChannelDepth = 0.0;
            if (Shaft && Penetration->GetEngagedFrame(Shaft, ShaftEntrance, ShaftInward, Inserted, ChannelDepth)) Shallowest = FMath::Min(Shallowest, Inserted);
        }
        const UGratiaCharacterProfile* Profile = Runtime->TargetCharacter->CharacterProfile.Get();
        const double MaxLead = (Profile ? Profile->Penetration.MaxLagCm : 6.0f) + 2.0;
        if (Shallowest < Target - 0.3 && HandDepth < Target + MaxLead) HandDepth = FMath::Min(HandDepth + Speed * Delta, Target + MaxLead);
        else Settled += Delta;
    }
    const double TipDepth = Inserting < 0.0f || bBaseline ? -1.0 : HandDepth;
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
    // Views: from outside the channel (in front of or behind the body, a little below) and from lower to one side (a
    // true side view is behind a thigh).
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
    if (Inserting < 0.0f || (bBaseline ? Inserting < 0.5f : bInsertView ? Settled < 1.2f : Seconds < 0.5f)) return;
    const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/ChannelShots"),
        FString::Printf(TEXT("Channel_%s_%s_%s.png"), *Name.ToString(), *CaseName(Case),
        Case == 6 ? (ViewIndex == 0 ? TEXT("BellyBefore") : TEXT("Belly")) : ViewIndex == 0 ? TEXT("Axis") : TEXT("Side")));
    FScreenshotRequest::RequestScreenshot(File, false, false);
    UE_LOG(LogGratiaChannelShots, Display, TEXT("CHANNEL_SHOT %s %s"), *File, *Penetration->GetDiagnostics());
    ++Step;
    Seconds = 0.0f;
}

void UGratiaChannelShots::ShootForms(APlayerController* Player)
{
    // Every primitive form at size L standing in a row a metre in front of the character, seen from further out.
    auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    const AActor* Body = Runtime && Runtime->TargetCharacter.IsValid() ? Runtime->TargetCharacter.Get() : nullptr;
    if (!Body) { ++Step; Seconds = 0.0f; return; }
    const UGratiaCharacterProfile* Profile = Runtime->TargetCharacter->CharacterProfile.Get();
    const USceneComponent* Mesh = Runtime->TargetCharacter->CharacterMesh.Get();
    const FTransform Frame = Mesh ? Mesh->GetComponentTransform() : Body->GetActorTransform();
    const FVector Forward = Frame.TransformVectorNoScale(Profile ? Profile->ForwardAxis : FVector::RightVector).GetSafeNormal2D();
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Forward);
    const FVector Origin = Body->GetActorLocation() + Forward * 100.0 + FVector::UpVector * 75.0;
    constexpr int32 Forms = int32(EGratiaShaftForm::Tentacle) + 1;
    if (Seconds <= GetWorld()->GetDeltaSeconds() + UE_SMALL_NUMBER)
    {
        for (AGratiaPenetrator* Shaft : Shafts) if (Shaft) Shaft->Destroy();
        Shafts.Reset();
        Runtime->QAShafts.Reset();
        FActorSpawnParameters Parameters;
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        for (int32 Form = 0; Form < Forms; ++Form)
        {
            AGratiaPenetrator* Shaft = GetWorld()->SpawnActor<AGratiaPenetrator>(AGratiaPenetrator::StaticClass(), FTransform::Identity, Parameters);
            if (!Shaft) continue;
            Shaft->SetSize(2);
            Shaft->SetForm(static_cast<EGratiaShaftForm>(Form));
            Shaft->SetBase(FTransform(FRotationMatrix::MakeFromX(FVector::UpVector).ToQuat(), Origin + Across * ((Form - (Forms - 1) * 0.5) * 13.0)));
            Shaft->SetJoints({});
            Shafts.Add(Shaft);
        }
    }
    const FVector Look = Origin + FVector::UpVector * 10.0;
    const FVector Eye = Look + Forward * 150.0;
    if (View)
    {
        View->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
        if (Player->GetViewTarget() != View) Player->SetViewTarget(View);
    }
    if (Seconds < 1.0f) return;
    const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/ChannelShots/Primitive_Forms.png"));
    FScreenshotRequest::RequestScreenshot(File, false, false);
    UE_LOG(LogGratiaChannelShots, Display, TEXT("CHANNEL_SHOT %s forms=%d"), *File, Shafts.Num());
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
