#include "GratiaMotionProbe.h"
#include "GratiaPreviewCharacter.h"
#include "GratiaStage1Runtime.h"
#include "GratiaInteraction.h"
#include "GratiaPenetration.h"
#include "Animation/MorphTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaMotionProbe, Log, All);

UGratiaMotionProbe::UGratiaMotionProbe()
{
    PrimaryComponentTick.bCanEverTick = true;
    // After animation and the runtime's hands: what the frame shows.
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGratiaMotionProbe::Bind()
{
    const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner());
    USkeletalMeshComponent* Target = Runtime && Runtime->TargetCharacter.IsValid() ? Runtime->TargetCharacter->CharacterMesh.Get() : nullptr;
    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    USceneComponent* Found[2] = {nullptr, nullptr};
    if (Pawn)
    {
        TInlineComponentArray<USceneComponent*> Components;
        Pawn->GetComponents(Components);
        for (USceneComponent* Component : Components)
        {
            if (Component->GetFName() == TEXT("HandLeft")) Found[0] = Component;
            if (Component->GetFName() == TEXT("HandRight")) Found[1] = Component;
        }
    }
    if (Target == Mesh.Get() && Found[0] == Hands[0].Get() && Found[1] == Hands[1].Get() && !Tracks.IsEmpty()) return;
    Mesh = Target;
    Hands[0] = Found[0];
    Hands[1] = Found[1];
    Tracks.Reset();
    Frames = 0;
    if (Target && Target->GetSkeletalMeshAsset())
    {
        const FReferenceSkeleton& Ref = Target->GetSkeletalMeshAsset()->GetRefSkeleton();
        for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone) Tracks.Add({Ref.GetBoneName(Bone).ToString()});
        Tracks.Add({TEXT("(mesh position)")});
        for (const UMorphTarget* Morph : Target->GetSkeletalMeshAsset()->GetMorphTargets())
        {
            FTrack Track;
            Track.Name = TEXT("morph ") + GetNameSafe(Morph);
            Track.bWeight = true;
            Tracks.Add(Track);
        }
    }
    Tracks.Add({TEXT("(left hand)")});
    Tracks.Add({TEXT("(right hand)")});
    UE_LOG(LogGratiaMotionProbe, Display, TEXT("MOTION_PROBE bound mesh=%s hands=%d/%d tracks=%d"), *GetNameSafe(Target), Found[0] ? 1 : 0, Found[1] ? 1 : 0,
        Tracks.Num());
}

void UGratiaMotionProbe::Sample(FTrack& Track, const FVector& Value, double Time)
{
    Track.Sample[0] = Track.Sample[1];
    Track.Sample[1] = Track.Sample[2];
    Track.Sample[2] = Value;
    if (++Track.Count < 3) return;
    const double Span = Times[2] - Times[0];
    if (Span <= 1.0e-4 || Span > 0.2) return;
    // How far the middle frame is off the line between its neighbours (in time).
    const double W = (Times[1] - Times[0]) / Span;
    const FVector Step = Track.Sample[1] - (Track.Sample[0] + (Track.Sample[2] - Track.Sample[0]) * W);
    const double Size = Step.Size();
    const double Pop = Track.bWeight ? PopWeight : PopCm, Jitter = Track.bWeight ? JitterWeight : JitterCm;
    ++Track.Steps;
    Track.SumStep += Size;
    if (Size > Jitter && Track.LastStep.Size() > Jitter && FVector::DotProduct(Step, Track.LastStep) < 0.0) ++Track.Reversals;
    Track.LastStep = Step;
    if (Size > Track.MaxStep) { Track.MaxStep = Size; Track.MaxTime = Times[1]; Track.MaxContext = Context; }
    if (Size > Pop)
    {
        ++Track.Pops;
        Events.Add({Times[1], Size, Track.Name, Context});
        if (Events.Num() > 400)
        {
            Events.Sort([](const FEvent& A, const FEvent& B) { return A.Size > B.Size; });
            Events.SetNum(200);
        }
    }
}

void UGratiaMotionProbe::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    Bind();
    if (QuitSeconds < 0.0)
    {
        QuitSeconds = 0.0;
        FParse::Value(FCommandLine::Get(), TEXT("GratiaMotionProbeSeconds="), QuitSeconds);
    }
    if (QuitSeconds > 0.0 && Elapsed >= QuitSeconds)
    {
        Write();
        QuitSeconds = 0.0;
        FPlatformMisc::RequestExitWithStatus(false, 0, TEXT("GratiaMotionProbe"));
    }
    // What the character is doing, when no shot names it: pose, performance part, touched zone, channels in use.
    if (const auto* Runtime = Cast<AGratiaStage1Runtime>(GetOwner()); Runtime && Runtime->TargetCharacter.IsValid() && !bExternalContext)
    {
        const AGratiaPreviewCharacter* Character = Runtime->TargetCharacter.Get();
        Context = Character->GetPreviewPoseLabel();
        if (Character->PreviewPose == EGratiaPreviewPose::Performance) Context += FString::Printf(TEXT(" part %d"), Character->PerformancePart);
        if (Character->Interaction && Character->Interaction->ActiveZone != INDEX_NONE) Context += TEXT(" zone ") + Character->Interaction->LastReactionZoneName.ToString();
        if (Character->Penetration && Character->Penetration->GetEngagementCount() > 0) Context += FString::Printf(TEXT(" shafts %d"), Character->Penetration->GetEngagementCount());
    }
    Elapsed += Delta;
    Times[0] = Times[1];
    Times[1] = Times[2];
    Times[2] = Elapsed;
    ++Frames;
    // Loading and placing the scene are not measured; a teleport of the character (scene start, recenter) is an
    // intended jump: its frames are skipped, what the springs do after it is measured.
    if (const USkeletalMeshComponent* Target = Mesh.Get())
    {
        const FVector Location = Target->GetComponentLocation();
        if (bHaveLastMesh && FVector::Distance(Location, LastMesh) > 10.0) { Skip(); ++Teleports; }
        LastMesh = Location;
        bHaveLastMesh = true;
    }
    if (Elapsed < WarmupSeconds) Skip(1);
    if (SkipFrames > 0)
    {
        --SkipFrames;
        for (FTrack& Track : Tracks) { Track.Count = 0; Track.LastStep = FVector::ZeroVector; }
    }
    int32 Index = 0;
    if (const USkeletalMeshComponent* Target = Mesh.Get())
    {
        const TArray<FTransform>& Pose = Target->GetComponentSpaceTransforms();
        const int32 Bones = Target->GetSkeletalMeshAsset() ? Target->GetSkeletalMeshAsset()->GetRefSkeleton().GetNum() : 0;
        for (int32 Bone = 0; Bone < Bones; ++Bone, ++Index)
            if (Pose.IsValidIndex(Bone)) Sample(Tracks[Index], Pose[Bone].GetLocation(), Elapsed);
        Sample(Tracks[Index++], Target->GetComponentLocation(), Elapsed);
        const int32 Morphs = Target->GetSkeletalMeshAsset() ? Target->GetSkeletalMeshAsset()->GetMorphTargets().Num() : 0;
        for (int32 Morph = 0; Morph < Morphs; ++Morph, ++Index)
            Sample(Tracks[Index], FVector(Target->MorphTargetWeights.IsValidIndex(Morph) ? Target->MorphTargetWeights[Morph] : 0.0f, 0.0, 0.0), Elapsed);
    }
    for (int32 Side = 0; Side < 2 && Tracks.IsValidIndex(Index); ++Side, ++Index)
        if (const USceneComponent* Hand = Hands[Side].Get())
            Sample(Tracks[Index], Hand->GetComponentLocation() + Hand->GetForwardVector() * 10.0, Elapsed);
    if (Elapsed >= NextWrite)
    {
        NextWrite = Elapsed + 5.0;
        Write();
    }
}

void UGratiaMotionProbe::Write() const
{
    TArray<const FTrack*> ByPop, ByJitter;
    int32 Pops = 0;
    for (const FTrack& Track : Tracks)
    {
        Pops += Track.Pops;
        if (Track.Steps > 0) { ByPop.Add(&Track); ByJitter.Add(&Track); }
    }
    ByPop.Sort([](const FTrack& A, const FTrack& B) { return (A.bWeight ? A.MaxStep * 5.0 : A.MaxStep) > (B.bWeight ? B.MaxStep * 5.0 : B.MaxStep); });
    ByJitter.Sort([](const FTrack& A, const FTrack& B) { return A.Reversals > B.Reversals; });
    FString Out = FString::Printf(TEXT("frames=%d seconds=%.1f tracks=%d pops=%d (bones/hands > %.2f cm, morphs > %.2f off the line between neighbour frames)\n\n"),
        Frames, Elapsed, Tracks.Num(), Pops, PopCm, PopWeight);
    // Totals per group (the bone name's first word, morphs, hands): the numbers to compare before and after a change.
    struct FGroup { int32 Tracks = 0, Pops = 0, Reversals = 0; double Max = 0.0, Sum = 0.0; int64 Steps = 0; };
    TMap<FString, FGroup> Groups;
    for (const FTrack& Track : Tracks)
    {
        if (Track.Steps == 0) continue;
        FString Key = Track.bWeight ? FString(TEXT("morphs")) : Track.Name;
        if (!Track.bWeight && !Track.Name.StartsWith(TEXT("(")))
        {
            Key.RemoveFromStart(TEXT("DEF-"));
            Key.RemoveFromStart(TEXT("ORG-"));
            int32 Cut = INDEX_NONE;
            if (Key.FindChar(TEXT('_'), Cut)) Key.LeftInline(Cut);
            if (Key.FindChar(TEXT('.'), Cut)) Key.LeftInline(Cut);
            while (!Key.IsEmpty() && FChar::IsDigit(Key[Key.Len() - 1])) Key.LeftChopInline(1);
        }
        FGroup& Group = Groups.FindOrAdd(Key);
        ++Group.Tracks; Group.Pops += Track.Pops; Group.Reversals += Track.Reversals; Group.Max = FMath::Max(Group.Max, Track.MaxStep);
        Group.Sum += Track.SumStep; Group.Steps += Track.Steps;
    }
    Groups.ValueSort([](const FGroup& A, const FGroup& B) { return A.Pops + A.Reversals > B.Pops + B.Reversals; });
    Out += FString::Printf(TEXT("Measured after %.0f s; character teleports skipped: %d\n"), WarmupSeconds, Teleports);
    Out += TEXT("Groups (group, tracks, pops, reversals, max, mean step):\n");
    for (const TPair<FString, FGroup>& Group : Groups)
        Out += FString::Printf(TEXT("  %-24s %4d %7d %7d %9.3f %8.4f\n"), *Group.Key, Group.Value.Tracks, Group.Value.Pops, Group.Value.Reversals, Group.Value.Max,
            Group.Value.Steps ? Group.Value.Sum / Group.Value.Steps : 0.0);
    Out += TEXT("\nLargest steps (track, max, mean, pops, at s, while):\n");
    for (int32 I = 0; I < FMath::Min(30, ByPop.Num()); ++I)
    {
        const FTrack& T = *ByPop[I];
        Out += FString::Printf(TEXT("  %-40s %8.3f%s %7.4f %5d %8.2f  %s\n"), *T.Name, T.MaxStep, T.bWeight ? TEXT("w ") : TEXT("cm"), T.SumStep / T.Steps, T.Pops,
            T.MaxTime, *T.MaxContext);
    }
    Out += TEXT("\nMost direction reversals (jitter: steps above the jitter size that turn back; track, reversals of steps, max):\n");
    for (int32 I = 0; I < FMath::Min(30, ByJitter.Num()) && ByJitter[I]->Reversals > 0; ++I)
    {
        const FTrack& T = *ByJitter[I];
        Out += FString::Printf(TEXT("  %-40s %6d / %-6d %8.3f%s\n"), *T.Name, T.Reversals, T.Steps, T.MaxStep, T.bWeight ? TEXT("w") : TEXT("cm"));
    }
    TArray<FEvent> Sorted = Events;
    Sorted.Sort([](const FEvent& A, const FEvent& B) { return A.Size > B.Size; });
    Out += TEXT("\nWorst pops (at s, size, track, while):\n");
    for (int32 I = 0; I < FMath::Min(40, Sorted.Num()); ++I)
        Out += FString::Printf(TEXT("  %8.2f %8.3f  %-40s %s\n"), Sorted[I].Time, Sorted[I].Size, *Sorted[I].Track, *Sorted[I].Context);
    const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MotionProbe"));
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(Out, *FPaths::Combine(Dir, TEXT("report.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogGratiaMotionProbe, Display, TEXT("MOTION_PROBE frames=%d pops=%d worst=%s %.3f"), Frames, Pops, ByPop.IsEmpty() ? TEXT("-") : *ByPop[0]->Name,
        ByPop.IsEmpty() ? 0.0 : ByPop[0]->MaxStep);
}

void UGratiaMotionProbe::EndPlay(const EEndPlayReason::Type Reason)
{
    Write();
    Super::EndPlay(Reason);
}
