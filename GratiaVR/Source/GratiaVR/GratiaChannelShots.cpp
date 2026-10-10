#include "GratiaChannelShots.h"
#include "GratiaMotionProbe.h"
#include "GratiaCharacterProfile.h"
#include "GratiaPenetration.h"
#include "GratiaPenetrator.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaStage1Runtime.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#if WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogGratiaChannelShots, Log, All);

namespace
{
enum EGratiaShotCase : int32
{
    CaseEmpty, CaseFingers, CaseHand, CaseFist, CaseTwoHands, CaseXXL, CaseDeep4XL, CaseBeads, CaseKnot, CaseGape, CaseSmallAfterBig, CaseCount
};

FString GratiaGymDir() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ChannelGym")); }

/** Sets a numeric or boolean field of a struct by its name from a JSON value; false when there is no such field. */
bool GratiaSetField(const UStruct* Struct, void* Data, const FString& Name, const TSharedPtr<FJsonValue>& Value)
{
    FProperty* Property = Struct ? FindFProperty<FProperty>(Struct, *Name) : nullptr;
    if (!Property || !Value.IsValid()) return false;
    void* Field = Property->ContainerPtrToValuePtr<void>(Data);
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property)) { Bool->SetPropertyValue(Field, Value->AsBool()); return true; }
    if (const FFloatProperty* Float = CastField<FFloatProperty>(Property)) { Float->SetPropertyValue(Field, float(Value->AsNumber())); return true; }
    if (const FDoubleProperty* Double = CastField<FDoubleProperty>(Property)) { Double->SetPropertyValue(Field, Value->AsNumber()); return true; }
    if (const FIntProperty* Int = CastField<FIntProperty>(Property)) { Int->SetPropertyValue(Field, int32(Value->AsNumber())); return true; }
    if (const FNameProperty* NameField = CastField<FNameProperty>(Property)) { NameField->SetPropertyValue(Field, FName(*Value->AsString())); return true; }
    return false;
}
}

UGratiaChannelShots::UGratiaChannelShots()
{
    PrimaryComponentTick.bCanEverTick = true;
    // After the runtime solved the shafts this frame.
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGratiaChannelShots::BeginPlay()
{
    Super::BeginPlay();
    bLive = FParse::Param(FCommandLine::Get(), TEXT("GratiaChannelGym"));
    if (bLive)
    {
        IFileManager::Get().MakeDirectory(*GratiaGymDir(), true);
        const FString Gym = FPaths::Combine(GratiaGymDir(), TEXT("gym.json"));
        if (!IFileManager::Get().FileExists(*Gym))
            FFileHelper::SaveStringToFile(TEXT("{\n  \"serial\": \"0\",\n  \"cases\": [],\n  \"channels\": [],\n  \"settings\": {},\n  \"channel\": {},\n  \"ring\": {},\n  \"livecoding\": false,\n  \"quit\": false\n}\n"), *Gym);
        // The first pass reads the file as it is now.
        GymStamp = FDateTime::MinValue();
        PollGym();
        GymStamp = IFileManager::Get().GetTimeStamp(*Gym);
    }
}

FString UGratiaChannelShots::CaseName(int32 Case)
{
    static const TCHAR* Names[CaseCount] = {TEXT("Empty"), TEXT("Fingers"), TEXT("Hand"), TEXT("Fist"), TEXT("TwoHands"), TEXT("PrimitiveXXL"),
        TEXT("Deep4XL"), TEXT("BeadsXL"), TEXT("KnotL"), TEXT("Gape"), TEXT("SmallAfterBig")};
    return Names[FMath::Clamp(Case, 0, CaseCount - 1)];
}

void UGratiaChannelShots::BuildPlan(int32 Channels)
{
    Plan.Reset();
    auto Wanted = [this](int32 Case) { return CaseFilter.IsEmpty() || CaseFilter.Contains(CaseName(Case)); };
    for (int32 Channel = 0; Channel < Channels; ++Channel)
    {
        auto Add = [this, Channel](int32 Case, const TCHAR* Label, EAction Action, ECamera Camera, float Wait = 0.5f)
        {
            FShot Shot;
            Shot.Channel = Channel; Shot.Case = Case; Shot.Label = Label; Shot.Action = Action; Shot.Camera = Camera; Shot.Wait = Wait;
            Plan.Add(Shot);
        };
        for (int32 Case = 0; Case < CaseCount; ++Case)
        {
            if (!Wanted(Case)) continue;
            switch (Case)
            {
            case CaseDeep4XL:
                // The belly before (held at the entrance) and after the largest primitive goes deep.
                Add(Case, TEXT("BellyBefore"), EAction::ArrangeHeld, ECamera::Belly);
                Add(Case, TEXT("Belly"), EAction::Push, ECamera::Belly);
                break;
            case CaseGape:
                // An XXL in, then out: the gape right after, a few seconds later and after ten.
                Add(Case, TEXT("In"), EAction::Arrange, ECamera::Axis);
                Add(Case, TEXT("Out_0.3s"), EAction::PullOut, ECamera::Axis, 0.3f);
                Add(Case, TEXT("Out_3s"), EAction::Hold, ECamera::Axis, 2.7f);
                Add(Case, TEXT("Out_10s"), EAction::Hold, ECamera::Axis, 7.0f);
                break;
            case CaseSmallAfterBig:
                // The largest in and out, then three fingers into the gape: it must keep its size.
                Add(Case, TEXT("Big"), EAction::Arrange, ECamera::Axis);
                Add(Case, TEXT("Gape_1s"), EAction::PullOut, ECamera::Axis, 1.0f);
                Add(Case, TEXT("Small"), EAction::Swap, ECamera::Axis);
                break;
            default:
                Add(Case, TEXT("Axis"), EAction::Arrange, ECamera::Axis);
                Add(Case, TEXT("Side"), EAction::Hold, ECamera::Side);
                if (Case == CaseEmpty || Case == CaseFist || Case == CaseXXL) Add(Case, TEXT("Behind"), EAction::Hold, ECamera::Behind);
                // Hands go on with the forearm deep inside: the belly then.
                if (Case == CaseFist || Case == CaseTwoHands) Add(Case, TEXT("BellyDeep"), EAction::Push, ECamera::Belly);
                break;
            }
        }
    }
    bFormsShot = !(CaseFilter.IsEmpty() || CaseFilter.Contains(TEXT("Forms")));
}

void UGratiaChannelShots::ClearShafts()
{
    for (AGratiaPenetrator* Shaft : Shafts) if (Shaft) Shaft->Destroy();
    Shafts.Reset();
    if (auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner())) Runtime->QAShafts.Reset();
}

bool UGratiaChannelShots::Arrange(int32 Channel, int32 Case, bool bSmall)
{
    auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    ClearShafts();
    using namespace GratiaPenetration;
    TArray<TPair<FName, FShaft>> Hands;
    auto Hand = [&Hands](EHandShape Shape, const TCHAR* Label) { Hands.Emplace(FName(Label), HandShaft(Shape)); };
    if (bSmall || Case == CaseFingers) Hand(EHandShape::Fingers, TEXT("fingers"));
    else if (Case == CaseHand) Hand(EHandShape::Hand, TEXT("hand"));
    else if (Case == CaseFist) Hand(EHandShape::Fist, TEXT("fist"));
    else if (Case == CaseTwoHands) { Hand(EHandShape::Fingers, TEXT("fingers")); Hand(EHandShape::Fingers, TEXT("fingers")); }
    const bool bPrimitive = Hands.IsEmpty();
    if (Case == CaseEmpty && !bSmall) return true;
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const int32 Count = bPrimitive ? 1 : Hands.Num();
    for (int32 Index = 0; Index < Count; ++Index)
    {
        AGratiaPenetrator* Shaft = GetWorld()->SpawnActor<AGratiaPenetrator>(AGratiaPenetrator::StaticClass(), FTransform::Identity, Parameters);
        if (!Shaft) continue;
        if (!bPrimitive)
        {
            Shaft->SetShape(Hands[Index].Key, Hands[Index].Value);
            Shaft->bAnchorWhenReleased = false;
        }
        // The primitive: XL beads, an L knot, XXL at the entrance (and for the gape), the largest (4XL) deep and before
        // a small one.
        else if (Case == CaseBeads) { Shaft->SetSize(3); Shaft->SetForm(EGratiaShaftForm::Beads); }
        else if (Case == CaseKnot) { Shaft->SetSize(2); Shaft->SetForm(EGratiaShaftForm::Knotted); }
        else Shaft->SetSize(Case == CaseXXL || Case == CaseGape ? Shaft->Sizes.Num() - 3 : Shaft->Sizes.Num() - 1);
        Shaft->SetHeld(Index);
        Shafts.Add(Shaft);
    }
    Runtime->QAShafts = Shafts;
    UE_LOG(LogGratiaChannelShots, Display, TEXT("CHANNEL_SHOT arrange channel=%d case=%s small=%d shafts=%d"), Channel, *CaseName(Case), bSmall ? 1 : 0, Shafts.Num());
    return true;
}

double UGratiaChannelShots::TargetDepth(int32 Case, double ChannelDepth) const
{
    if (bSwapped) return 6.0;
    if (Plan.IsValidIndex(Step) && Plan[Step].Camera == ECamera::Belly && (Case == CaseFist || Case == CaseTwoHands)) return ChannelDepth * 0.6;
    switch (Case)
    {
    case CaseFist: return 4.0;
    case CaseXXL: case CaseGape: case CaseSmallAfterBig: return 8.0;
    case CaseDeep4XL: return ChannelDepth * 0.85;
    case CaseBeads: return 12.0;
    case CaseKnot: return !Shafts.IsEmpty() && Shafts[0] ? 0.8 * Shafts[0]->GetShaft().Length : 6.0;
    default: return 6.0;
    }
}

bool UGratiaChannelShots::PollGym()
{
    const FString Gym = FPaths::Combine(GratiaGymDir(), TEXT("gym.json"));
    const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Gym);
    if (Stamp == FDateTime::MinValue() || Stamp == GymStamp) return false;
    GymStamp = Stamp;
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *Gym) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    {
        UE_LOG(LogGratiaChannelShots, Warning, TEXT("CHANNEL_GYM cannot read %s"), *Gym);
        return false;
    }
    auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    AGratiaPreviewCharacter* Character = Runtime && Runtime->TargetCharacter.IsValid() ? Runtime->TargetCharacter.Get() : nullptr;
    UGratiaCharacterProfile* Profile = Character ? Character->CharacterProfile.Get() : nullptr;
    if (Root->HasTypedField<EJson::Boolean>(TEXT("quit")) && Root->GetBoolField(TEXT("quit")))
    {
        if (APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0)) UKismetSystemLibrary::QuitGame(this, Player, EQuitPreference::Quit, false);
        return false;
    }
    Serial = Root->HasField(TEXT("serial")) ? Root->GetStringField(TEXT("serial")) : FString();
    auto Strings = [&Root](const TCHAR* Field)
    {
        TArray<FString> Out;
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (Root->TryGetArrayField(Field, Values)) for (const TSharedPtr<FJsonValue>& Value : *Values) Out.Add(Value->AsString());
        return Out;
    };
    CaseFilter = Strings(TEXT("cases"));
    ChannelFilter = Strings(TEXT("channels"));
    TArray<FString> Applied;
    // Material slots to hide for a look under a layer ("hide": ["Default cloth 2"]); the others show again.
    if (USkeletalMeshComponent* Mesh = Character ? Character->CharacterMesh.Get() : nullptr)
    {
        Mesh->ShowAllMaterialSections(0);
        const FSkeletalMeshRenderData* Render = Mesh->GetSkeletalMeshAsset() ? Mesh->GetSkeletalMeshAsset()->GetResourceForRendering() : nullptr;
        for (const FString& Slot : Strings(TEXT("hide")))
        {
            const int32 Material = Mesh->GetMaterialIndex(FName(*Slot));
            if (!Render || Material == INDEX_NONE || Render->LODRenderData.IsEmpty()) { Applied.Add(TEXT("hide ") + Slot + TEXT("?")); continue; }
            const TArray<FSkelMeshRenderSection>& Sections = Render->LODRenderData[0].RenderSections;
            for (int32 Section = 0; Section < Sections.Num(); ++Section)
                if (Sections[Section].MaterialIndex == Material) Mesh->ShowMaterialSection(Material, Section, false, 0);
            Applied.Add(TEXT("hide ") + Slot);
        }
    }
    if (Profile)
    {
        FGratiaPenetrationSettings& Settings = Profile->Penetration;
        const TSharedPtr<FJsonObject>* Object = nullptr;
        // Penetration settings by field name ("GapeShare": 0.9).
        if (Root->TryGetObjectField(TEXT("settings"), Object))
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
                Applied.Add(Pair.Key + (GratiaSetField(FGratiaPenetrationSettings::StaticStruct(), &Settings, Pair.Key, Pair.Value) ? TEXT("") : TEXT("?")));
        // Channel fields by channel name ("Anal": {"RestRadiusCm": 0.3}).
        if (Root->TryGetObjectField(TEXT("channel"), Object))
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
                for (FGratiaPenetrationChannel& Channel : Settings.Channels)
                    if (Channel.Name.ToString() == Pair.Key && Pair.Value->Type == EJson::Object)
                        for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Pair.Value->AsObject()->Values)
                            Applied.Add(Pair.Key + TEXT(".") + Field.Key + (GratiaSetField(FGratiaPenetrationChannel::StaticStruct(), &Channel, Field.Key, Field.Value) ? TEXT("") : TEXT("?")));
        // Wall bones by "Channel/bone prefix": [response, start opening cm, max offset cm] or {"Response": ..}.
        if (Root->TryGetObjectField(TEXT("ring"), Object))
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
            {
                FString ChannelName, Prefix;
                if (!Pair.Key.Split(TEXT("/"), &ChannelName, &Prefix)) continue;
                for (FGratiaPenetrationChannel& Channel : Settings.Channels)
                {
                    if (Channel.Name.ToString() != ChannelName) continue;
                    for (FGratiaChannelBone& Bone : Channel.Bones)
                    {
                        if (!Bone.Bone.ToString().StartsWith(Prefix)) continue;
                        if (Pair.Value->Type == EJson::Array && Pair.Value->AsArray().Num() >= 3)
                        {
                            const TArray<TSharedPtr<FJsonValue>>& Values = Pair.Value->AsArray();
                            Bone.Response = float(Values[0]->AsNumber()); Bone.StartOpeningCm = float(Values[1]->AsNumber()); Bone.MaxOffsetCm = float(Values[2]->AsNumber());
                        }
                        else if (Pair.Value->Type == EJson::Object)
                            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Pair.Value->AsObject()->Values)
                                GratiaSetField(FGratiaChannelBone::StaticStruct(), &Bone, Field.Key, Field.Value);
                        Applied.Add(Pair.Key + TEXT(":") + Bone.Bone.ToString());
                    }
                }
            }
    }
#if WITH_LIVE_CODING
    // A code change: patch the running game first (Live Coding builds the changed sources of the loaded modules).
    if (Root->HasTypedField<EJson::Boolean>(TEXT("livecoding")) && Root->GetBoolField(TEXT("livecoding")))
        if (ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME))
        {
            if (!LiveCoding->IsEnabledForSession()) LiveCoding->EnableForSession(true);
            ELiveCodingCompileResult Result = ELiveCodingCompileResult::Failure;
            LiveCoding->Compile(ELiveCodingCompileFlags::WaitForCompletion, &Result);
            Applied.Add(FString::Printf(TEXT("livecoding=%d"), int32(Result)));
        }
#endif
    // Bones and morphs are read when the channels resolve: start them fresh with the new values.
    if (Character && Character->Penetration) Character->Penetration->Reload();
    UE_LOG(LogGratiaChannelShots, Display, TEXT("CHANNEL_GYM serial=%s cases=[%s] channels=[%s] applied=[%s]"), *Serial, *FString::Join(CaseFilter, TEXT(",")),
        *FString::Join(ChannelFilter, TEXT(",")), *FString::Join(Applied, TEXT(",")));
    return true;
}

void UGratiaChannelShots::FinishPass()
{
    ClearShafts();
    const FString Done = FString::Printf(TEXT("%s\n%d\n"), *Serial, Pass);
    FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *FPaths::Combine(GratiaGymDir(), TEXT("report.txt")));
    FFileHelper::SaveStringToFile(Done, *FPaths::Combine(GratiaGymDir(), TEXT("done.txt")));
    UE_LOG(LogGratiaChannelShots, Display, TEXT("CHANNEL_GYM done serial=%s pass=%d shots=%d"), *Serial, Pass, Report.Num());
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
    if (Step == INDEX_NONE)
    {
        Warmup += Delta;
        if (Warmup < 5.0f || Penetration->GetChannelCount() == 0) return;
#if WITH_EDITOR
        if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
        BuildPlan(Penetration->GetChannelCount());
        Report.Reset();
        ++Pass;
        Step = 0;
        Seconds = 0.0f;
    }
    if (bWaiting)
    {
        // Gym: wait for the next change of gym.json.
        if ((PollSeconds += Delta) < 0.5f) return;
        PollSeconds = 0.0f;
        if (!PollGym()) return;
        bWaiting = false;
        Step = INDEX_NONE;
        Warmup = 5.0f;
        return;
    }
    if (Step >= Plan.Num())
    {
        if (!bFormsShot) { bFormsShot = ShootForms(Player); return; }
        if (bLive) { FinishPass(); bWaiting = true; return; }
        if (Seconds > 1.0f) UKismetSystemLibrary::QuitGame(this, Player, EQuitPreference::Quit, false);
        return;
    }
    const FShot& Shot = Plan[Step];
    FName Name;
    FVector Entrance, Inward;
    double Depth = 0.0;
    if (!Penetration->GetChannelFrame(Shot.Channel, Name, Entrance, Inward, Depth) || (!ChannelFilter.IsEmpty() && !ChannelFilter.Contains(Name.ToString())))
    {
        ++Step; Seconds = 0.0f; return;
    }
    // First frame of a shot: its action.
    if (Seconds <= Delta + UE_SMALL_NUMBER)
    {
        if (UGratiaMotionProbe* Probe = GetOwner()->FindComponentByClass<UGratiaMotionProbe>())
        {
            Probe->SetContext(FString::Printf(TEXT("%s_%s_%s"), *Name.ToString(), *CaseName(Shot.Case), *Shot.Label));
            // A new case starts from a closed channel and new shafts: an intended jump.
            if (Shot.Action == EAction::Arrange || Shot.Action == EAction::ArrangeHeld || Shot.Action == EAction::Swap) Probe->Skip();
        }
        switch (Shot.Action)
        {
        case EAction::Arrange: case EAction::ArrangeHeld:
            // A case starts on a closed channel.
            Penetration->ResetPenetration();
            bSwapped = false;
            Arrange(Shot.Channel, Shot.Case, false);
            bPush = Shot.Action == EAction::Arrange; Inserting = -1.0f; HandDepth = -1.0; Settled = 0.0f;
            break;
        case EAction::Push: bPush = true; HandDepth = -1.0; Settled = 0.0f; break;
        case EAction::PullOut: ClearShafts(); bPush = false; break;
        case EAction::Swap:
            bSwapped = true;
            Arrange(Shot.Channel, Shot.Case, true);
            bPush = true; Inserting = -1.0f; HandDepth = -1.0; Settled = 0.0f;
            break;
        default: break;
        }
    }
    // Tip 1 cm before the entrance until every shaft is captured (or 3 s passed), then the hand pushes in at 25 cm/s
    // until the shafts reach their depth against the channel's resistance (at most its largest lag further), like a
    // hand would (two hands side by side).
    const double Target = TargetDepth(Shot.Case, Depth);
    constexpr double Speed = 25.0;
    if (!Shafts.IsEmpty())
    {
        if (Inserting < 0.0f)
        {
            // Engagements of earlier shafts are dropped by the next solve.
            if (Seconds > 0.1f && Penetration->GetEngagementCount() >= Shafts.Num()) Inserting = 0.0f;
            else if (Seconds > 3.0f)
            {
                UE_LOG(LogGratiaChannelShots, Warning, TEXT("CHANNEL_SHOT not captured channel=%s case=%s engaged=%d of %d"), *Name.ToString(),
                    *CaseName(Shot.Case), Penetration->GetEngagementCount(), Shafts.Num());
                Inserting = 0.0f;
            }
        }
        else Inserting += Delta;
        if (bPush && Inserting >= 0.0f)
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
    }
    const double TipDepth = Inserting < 0.0f || !bPush ? -1.0 : HandDepth;
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
    // Cameras: along the axis from outside (in front of or behind the body, a little below), from lower to one side (a
    // true side view is behind a thigh), from behind the entrance, and the belly three-quarter from the side.
    const FVector Up = FVector::UpVector;
    FVector Out = -Inward;
    Out.Z = 0.0;
    if (!Out.Normalize()) Out = Body->GetActorForwardVector();
    FVector Eye = Entrance + Out * 30.0 - Up * 10.0, Look = Entrance + Inward * 2.0;
    if (Shot.Camera == ECamera::Side) Eye = Entrance + Side * 30.0 + Out * 6.0 - Up * 4.0;
    // Behind: low and a little to the side, under the hair and the coat's tails.
    if (Shot.Camera == ECamera::Behind) { Eye = Entrance + Out * 38.0 + Side * 14.0 - Up * 2.0; Look = Entrance + Up * 3.0; }
    if (Shot.Camera == ECamera::Belly)
    {
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
        View = GetWorld()->SpawnActor<ACameraActor>(Eye, (Look - Eye).Rotation());
        if (View) View->GetCameraComponent()->FieldOfView = 45.0f;
    }
    if (View)
    {
        View->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
        if (Player->GetViewTarget() != View) Player->SetViewTarget(View);
    }
    // Ready: pushed shafts settle 1.2 s at their depth; held ones 0.5 s after the capture; otherwise the shot's wait.
    const bool bInserting = (Shot.Action == EAction::Arrange || Shot.Action == EAction::Push || Shot.Action == EAction::Swap) && !Shafts.IsEmpty();
    if (bInserting ? (Inserting < 0.0f || Settled < 1.2f) && Seconds < 15.0f
        : Shot.Action == EAction::ArrangeHeld ? Inserting < 0.5f && Seconds < 5.0f : Seconds < Shot.Wait) return;
    const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/ChannelShots"),
        FString::Printf(TEXT("Channel_%s_%s_%s.png"), *Name.ToString(), *CaseName(Shot.Case), *Shot.Label));
    FScreenshotRequest::RequestScreenshot(File, false, false);
    const FString Line = FString::Printf(TEXT("%s_%s_%s %s"), *Name.ToString(), *CaseName(Shot.Case), *Shot.Label, *Penetration->GetDiagnostics());
    Report.Add(Line);
    UE_LOG(LogGratiaChannelShots, Display, TEXT("CHANNEL_SHOT %s %s"), *File, *Line);
    ++Step;
    Seconds = 0.0f;
}

bool UGratiaChannelShots::ShootForms(APlayerController* Player)
{
    // Every primitive form at size L standing in a row a metre in front of the character, seen from further out.
    auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    const AActor* Body = Runtime && Runtime->TargetCharacter.IsValid() ? Runtime->TargetCharacter.Get() : nullptr;
    if (!Body) return true;
    const UGratiaCharacterProfile* Profile = Runtime->TargetCharacter->CharacterProfile.Get();
    const USceneComponent* Mesh = Runtime->TargetCharacter->CharacterMesh.Get();
    const FTransform Frame = Mesh ? Mesh->GetComponentTransform() : Body->GetActorTransform();
    const FVector Forward = Frame.TransformVectorNoScale(Profile ? Profile->ForwardAxis : FVector::RightVector).GetSafeNormal2D();
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Forward);
    const FVector Origin = Body->GetActorLocation() + Forward * 100.0 + FVector::UpVector * 75.0;
    constexpr int32 Forms = int32(EGratiaShaftForm::Tentacle) + 1;
    if (Seconds <= GetWorld()->GetDeltaSeconds() + UE_SMALL_NUMBER)
    {
        ClearShafts();
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
    if (Seconds < 1.0f) return false;
    const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/ChannelShots/Primitive_Forms.png"));
    FScreenshotRequest::RequestScreenshot(File, false, false);
    UE_LOG(LogGratiaChannelShots, Display, TEXT("CHANNEL_SHOT %s forms=%d"), *File, Shafts.Num());
    Seconds = 0.0f;
    return true;
}

void UGratiaChannelShots::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearShafts();
    if (View) View->Destroy();
    Super::EndPlay(Reason);
}
