#include "GratiaClothInteraction.h"

#include "GratiaCharacterProfile.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "ClothingAssetBase.h"
#include "ClothCollisionData.h"
#include "ClothingSimulationInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "ChaosCloth/ChaosClothingSimulation.h"
#include "ChaosCloth/ChaosClothingSimulationCloth.h"
#include "ChaosCloth/ChaosClothingSimulationFactory.h"
#include "ChaosCloth/ChaosClothingSimulationSolver.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaClothHands, Log, All);

namespace
{
struct FGratiaNativeClothView
{
    Chaos::FClothingSimulationSolver* Solver = nullptr;
    const Chaos::FClothingSimulationCloth* Cloth = nullptr;
    FName AssetName;
    int32 InstanceIndex = INDEX_NONE;
    int32 RangeId = INDEX_NONE;
};

// The factory check is essential: the public interface can contain other solvers.
Chaos::FClothingSimulationSolver* GratiaGetChaosClothSolver(FClothingSimulationInstance& Instance)
{
    if (!Instance.GetClothingSimulation() || !Instance.GetClothingSimulationFactory()
        || !Instance.GetClothingSimulationFactory()->IsA<UChaosClothingSimulationFactory>()) return nullptr;
    // UE 5.8's Chaos implementation still derives from the deprecated facade.
    // We use the current instance API; this cast is limited to that checked factory.
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    return static_cast<Chaos::FClothingSimulation*>(Instance.GetClothingSimulation())->GetSolver();
    PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
}

UGratiaClothInteraction::UGratiaClothInteraction()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGratiaClothInteraction::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    if (Character.IsValid() && Character->CharacterMesh)
    {
        // ClothTickFunction depends on the primary mesh tick in UE 5.8. Therefore
        // this prerequisite also keeps solver edits before the next cloth task.
        Character->CharacterMesh->AddTickPrerequisiteComponent(this);
    }
}

void UGratiaClothInteraction::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Character.IsValid() && Character->CharacterMesh)
    {
        ClearExternalCollisions();
        Character->CharacterMesh->RemoveTickPrerequisiteComponent(this);
    }
    Super::EndPlay(Reason);
}

bool UGratiaClothInteraction::IsEnabled() const
{
    return Character.IsValid() && Character->CharacterProfile && Character->Interaction
        && Character->CharacterProfile->ClothSettings.bEnabled
        && Character->CharacterProfile->Capabilities.bSecondaryPhysics
        && !Character->CharacterProfile->SourceClothCages.IsEmpty()
        && Character->Interaction->bBodyMotion && Character->Interaction->bPhysicalMotion && !bFault;
}

void UGratiaClothInteraction::ClearHands()
{
    for (FClothHand& Hand : Hands) Hand = FClothHand();
    PressDepthCm[0] = PressDepthCm[1] = 0;
    ActiveHandCount = 0;
}

void UGratiaClothInteraction::SubmitHand(bool bLeft, const FVector& Visible, const FVector& Raw, bool bAllowed, float Delta, float Trigger)
{
    FClothHand& Hand = Hands[bLeft ? 0 : 1];
    if (!bAllowed || !IsEnabled() || Visible.ContainsNaN() || Raw.ContainsNaN() || !FMath::IsFinite(Trigger)
        || !FMath::IsFinite(Delta) || Delta <= 0 || Delta > 0.1f)
    { Hand = FClothHand(); return; }
    const FGratiaClothSettings& Settings = Character->CharacterProfile->ClothSettings;
    // Soft tissue yields under the rigid proxy that stops the visible hand.
    const float PressDepth = FMath::IsFinite(Settings.SoftPressDepthCm) ? FMath::Clamp(Settings.SoftPressDepthCm, 0.f, 10.f) : 0.f;
    const FVector Position = Visible + (Raw - Visible).GetClampedToMaxSize(PressDepth);
    PressDepthCm[bLeft ? 0 : 1] = FVector::Distance(Position, Visible);
    const double Travel = FVector::Distance(Hand.Current, Position);
    const bool bContinuous = Hand.bReady && Travel <= Settings.MaxHandTravelCm
        && Travel <= Settings.MaxHandSpeedCmPerSecond * Delta;
    if (!bContinuous) { Hand.ReleaseGrab(); Hand.bGrabArmed = false; }
    Hand.bGrabPressed = false;
    if (Trigger <= 0.25f) { Hand.ReleaseGrab(); Hand.bGrabArmed = true; }
    else if (Trigger >= 0.65f && Hand.bGrabArmed)
    {
        Hand.bGrabPressed = Settings.bAllowGrab && bContinuous;
        Hand.bGrabArmed = false;
    }
    Hand.Previous = bContinuous ? Hand.Current : Position;
    Hand.Current = Position;
    Hand.Delta = Delta;
    Hand.SubmitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0;
    Hand.bPending = bContinuous;
    Hand.bReady = true;
}

void UGratiaClothInteraction::ClearExternalCollisions()
{
    if (!Character.IsValid() || !Character->CharacterMesh) return;
    auto* Mesh = Character->CharacterMesh.Get();
    Mesh->WaitForExistingParallelClothSimulation_GameThread();
    for (auto& Instance : Mesh->GetClothingSimulationInstances())
        if (GratiaGetChaosClothSolver(Instance)) Instance.ClearExternalCollisions();
}

void UGratiaClothInteraction::StopCloth(bool bReset)
{
    ClearHands(); ClearExternalCollisions();
    if (Character.IsValid() && Character->CharacterMesh)
    {
        if (bReset) Character->CharacterMesh->ForceClothNextUpdateTeleportAndReset();
        Character->CharacterMesh->ClothBlendWeight = 0.0f;
        Character->CharacterMesh->SuspendClothingSimulation();
    }
    bRunning = false;
}

void UGratiaClothInteraction::ResetCloth()
{
    bFault = false; FaultReason.Reset();
    ClearHands(); ClearExternalCollisions();
    if (Character.IsValid() && Character->CharacterMesh)
        Character->CharacterMesh->ForceClothNextUpdateTeleportAndReset();
    bRunning = false;
}

void UGratiaClothInteraction::SetFault(const FString& Reason)
{
    if (bFault) return;
    bFault = true; FaultReason = Reason;
    UE_LOG(LogGratiaClothHands, Error, TEXT("SOURCE_CLOTH_FAULT %s; reset required"), *Reason);
    StopCloth(true);
}

void UGratiaClothInteraction::UpdateNativeCloth(float Delta, bool bApplyHands)
{
    if (!Character.IsValid() || !Character->CharacterMesh || !Character->CharacterProfile) return;
    auto* Mesh = Character->CharacterMesh.Get();
    const auto* Profile = Character->CharacterProfile.Get();
    const FGratiaClothSettings& Settings = Profile->ClothSettings;
    if (!FMath::IsFinite(Settings.HandRadiusCm) || Settings.HandRadiusCm <= 0
        || !FMath::IsFinite(Settings.MaxParticleOffsetCm) || Settings.MaxParticleOffsetCm <= 0
        || !FMath::IsFinite(Settings.GrabRadiusCm) || Settings.GrabRadiusCm <= 0
        || !FMath::IsFinite(Settings.GrabBreakDistanceCm) || Settings.GrabBreakDistanceCm <= 0
        || !FMath::IsFinite(Settings.MaxHandTravelCm) || Settings.MaxHandTravelCm <= 0
        || !FMath::IsFinite(Settings.MaxHandSpeedCmPerSecond) || Settings.MaxHandSpeedCmPerSecond <= 0
        || !FMath::IsFinite(Settings.MaxGrabSpeedCmPerSecond) || Settings.MaxGrabSpeedCmPerSecond <= 0
        || !FMath::IsFinite(Settings.GrabStiffness) || Settings.GrabStiffness < 0
        || !FMath::IsFinite(Settings.GrabVelocityBlend) || Settings.GrabVelocityBlend < 0 || Settings.GrabVelocityBlend > 1)
    { SetFault(TEXT("invalid profile cloth settings")); return; }
    // No particle views survive this method. The previous parallel task is joined
    // before reading or changing the public solver buffers on the game thread.
    Mesh->WaitForExistingParallelClothSimulation_GameThread();
    auto& Instances = Mesh->GetClothingSimulationInstances();
    TArray<FGratiaNativeClothView> Views;
    ParticleSnapshot.Reset(); CageParticleSnapshots.Reset(); CageInverseMassSnapshots.Reset(); CageDisplacements.Reset();
    DynamicParticleCount = ManagedCageCount = 0; MaxDisplacementCm = 0;
    const auto* Asset = Mesh->GetSkeletalMeshAsset();
    if (!Asset) return;
    const auto& Assets = Asset->GetMeshClothingAssets();
    for (int32 InstanceIndex = 0; InstanceIndex < Instances.Num(); ++InstanceIndex)
    {
        if (!Instances[InstanceIndex].GetClothingSimulationFactory()
            || !Instances[InstanceIndex].GetClothingSimulationFactory()->IsA<UChaosClothingSimulationFactory>())
        { SetFault(TEXT("unsupported clothing simulation factory")); return; }
        auto* Solver = GratiaGetChaosClothSolver(Instances[InstanceIndex]);
        if (!Solver) continue;
        const double Scale = Solver->GetLocalSpaceScale();
        const FVector Origin(Solver->GetLocalSpaceLocation());
        if (!FMath::IsFinite(Scale) || Scale <= UE_SMALL_NUMBER || Origin.ContainsNaN())
        { SetFault(TEXT("invalid solver transform")); return; }
        for (const auto* Cloth : Solver->GetCloths())
        {
            const int32 ClothId = int32(Cloth->GetGroupId());
            if (!Assets.IsValidIndex(ClothId) || !Assets[ClothId]) continue;
            const FName Name = Assets[ClothId]->GetFName();
            if (!Profile->SourceClothCages.ContainsByPredicate([Name](const auto& Cage) { return Cage.AssetName == Name; })) continue;
            const int32 RangeId = Cloth->GetParticleRangeId(Solver);
            if (RangeId == INDEX_NONE) continue;
            const auto Positions = Solver->GetParticleXsView(RangeId);
            const auto Velocities = Solver->GetParticleVsView(RangeId);
            const auto InverseMasses = Solver->GetParticleInvMassesView(RangeId);
            const auto Animation = Solver->GetAnimationPositionsView(RangeId);
            if (Positions.Num() != Animation.Num() || Positions.Num() != InverseMasses.Num() || Positions.Num() != Velocities.Num())
            { SetFault(TEXT("inconsistent particle buffers")); return; }
            auto& CageSnapshot = CageParticleSnapshots.FindOrAdd(Name);
            auto& CageInverseMassSnapshot = CageInverseMassSnapshots.FindOrAdd(Name);
            float CageDisplacement = 0;
            double DisplacementSum = 0;
            int32 DynamicInCage = 0;
            for (int32 Index = 0; Index < Positions.Num(); ++Index)
            {
                const FVector Position(Positions[Index]), Animated(Animation[Index]), Velocity(Velocities[Index]);
                if (Position.ContainsNaN() || Animated.ContainsNaN() || Velocity.ContainsNaN() || !FMath::IsFinite(InverseMasses[Index]))
                { SetFault(Name.ToString() + TEXT(" non-finite particle")); return; }
                const FVector WorldPosition = Position * Scale + Origin;
                if (WorldPosition.ContainsNaN()) { SetFault(TEXT("non-finite world particle")); return; }
                ParticleSnapshot.Add(WorldPosition); CageSnapshot.Add(WorldPosition);
                CageInverseMassSnapshot.Add(InverseMasses[Index]);
                if (InverseMasses[Index] > 0)
                {
                    ++DynamicParticleCount;
                    const float Offset = float(FVector::Distance(Position, Animated) * Scale);
                    CageDisplacement = FMath::Max(CageDisplacement, Offset);
                    DisplacementSum += Offset; ++DynamicInCage;
                }
            }
            CageDisplacements.Add(Name, CageDisplacement);
            RegionDisplacementCm.SetNumZeroed(Profile->SourceClothRegions.Num());
            for (int32 Region = 0; Region < Profile->SourceClothRegions.Num(); ++Region)
            {
                const auto& Definition = Profile->SourceClothRegions[Region];
                if (Definition.AssetName != Name) continue;
                double Sum = 0, Max = 0;
                int32 Count = 0;
                for (int32 Index = Definition.FirstParticle; Index < Definition.FirstParticle + Definition.ParticleCount && Index < Positions.Num(); ++Index)
                {
                    if (InverseMasses[Index] <= 0) continue;
                    const double Offset = FVector::Distance(FVector(Positions[Index]), FVector(Animation[Index])) * Scale;
                    Sum += Offset; Max = FMath::Max(Max, Offset); ++Count;
                }
                RegionDisplacementCm[Region] = FVector2f(Count ? float(Sum / Count) : 0.f, float(Max));
            }
            MeanDisplacementCm = DynamicInCage ? float(DisplacementSum / DynamicInCage) : 0.f;
            MaxDisplacementCm = FMath::Max(MaxDisplacementCm, CageDisplacement);
            if (CageDisplacement > Settings.MaxParticleOffsetCm)
            { SetFault(FString::Printf(TEXT("%s displacement %.2fcm exceeds %.2fcm"), *Name.ToString(), CageDisplacement, Settings.MaxParticleOffsetCm)); return; }
            Views.Add({Solver, Cloth, Name, InstanceIndex, RangeId});
            ++ManagedCageCount;
        }
    }

    if (!bApplyHands) return;
    FClothCollisionData Collisions;
    ActiveHandCount = 0;
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0;
    const FTransform ComponentToWorld = Mesh->GetComponentTransform();
    const double ComponentScale = ComponentToWorld.GetScale3D().GetAbs().GetMax();
    if (ComponentToWorld.ContainsNaN() || ComponentScale <= UE_SMALL_NUMBER) { SetFault(TEXT("invalid component transform")); return; }
    for (int32 HandIndex = 0; HandIndex < 2; ++HandIndex)
    {
        auto& Hand = Hands[HandIndex];
        if (Now - Hand.SubmitTime > 0.1) { Hand = FClothHand(); continue; }
        if (!Hand.bPending) continue;
        Hand.bPending = false;
        ++ActiveHandCount;
        const FVector HandVelocity = ((Hand.Current - Hand.Previous) / Hand.Delta).GetClampedToMaxSize(Settings.MaxHandSpeedCmPerSecond);
        if (Hand.bGrabPressed)
        {
            Hand.bGrabPressed = false;
            const double SearchCm = Settings.HandRadiusCm + Settings.GrabRadiusCm;
            double BestDistance = SearchCm, NearestAnyCm = DBL_MAX;
            for (const auto& View : Views)
            {
                const auto Positions = View.Solver->GetParticleXsView(View.RangeId);
                const auto InverseMasses = View.Solver->GetParticleInvMassesView(View.RangeId);
                const double Scale = View.Solver->GetLocalSpaceScale();
                const FVector Origin(View.Solver->GetLocalSpaceLocation());
                for (int32 Index = 0; Index < Positions.Num(); ++Index)
                {
                    if (InverseMasses[Index] <= 0) continue;
                    const auto& Other = Hands[1 - HandIndex];
                    if (Other.InstanceIndex == View.InstanceIndex && Other.ParticleRangeId == View.RangeId && Other.ParticleIndex == Index) continue;
                    const FVector Position = FVector(Positions[Index]) * Scale + Origin;
                    const double Distance = FVector::Distance(Hand.Current, Position);
                    NearestAnyCm = FMath::Min(NearestAnyCm, Distance);
                    if (Distance >= BestDistance) continue;
                    BestDistance = Distance; Hand.InstanceIndex = View.InstanceIndex;
                    Hand.ClothId = int32(View.Cloth->GetGroupId()); Hand.ParticleRangeId = View.RangeId;
                    Hand.ParticleIndex = Index; Hand.GrabOffset = Position - Hand.Current;
                }
            }
            UE_LOG(LogGratiaClothHands, Display, TEXT("SOURCE_CLOTH_GRAB hand=%d particle=%d nearest_dynamic_cm=%.2f search_cm=%.1f press_cm=%.1f"),
                HandIndex, Hand.ParticleIndex, NearestAnyCm, SearchCm, PressDepthCm[HandIndex]);
        }
        if (Hand.ParticleIndex != INDEX_NONE)
        {
            const auto* View = Views.FindByPredicate([&Hand](const auto& Value) { return Value.InstanceIndex == Hand.InstanceIndex
                && Value.RangeId == Hand.ParticleRangeId && int32(Value.Cloth->GetGroupId()) == Hand.ClothId; });
            if (!Settings.bAllowGrab || !View) { Hand.ReleaseGrab(); continue; }
            const auto Positions = View->Solver->GetParticleXsView(View->RangeId);
            auto Velocities = View->Solver->GetParticleVsView(View->RangeId);
            const auto Animation = View->Solver->GetAnimationPositionsView(View->RangeId);
            const auto InverseMasses = View->Solver->GetParticleInvMassesView(View->RangeId);
            if (!Positions.IsValidIndex(Hand.ParticleIndex) || InverseMasses[Hand.ParticleIndex] <= 0) { Hand.ReleaseGrab(); continue; }
            const double Scale = View->Solver->GetLocalSpaceScale();
            const FVector Origin(View->Solver->GetLocalSpaceLocation());
            const FVector Position = FVector(Positions[Hand.ParticleIndex]) * Scale + Origin;
            const FVector Target = Hand.Current + Hand.GrabOffset;
            if (FVector::Distance(Target, Position) > Settings.GrabBreakDistanceCm) { Hand.ReleaseGrab(); continue; }
            const FVector Animated = FVector(Animation[Hand.ParticleIndex]) * Scale + Origin;
            const FVector BoundedTarget = Animated + (Target - Animated).GetClampedToMaxSize(Settings.MaxParticleOffsetCm * 0.85f);
            const FVector DesiredVelocity = (HandVelocity + (BoundedTarget - Position) * Settings.GrabStiffness)
                .GetClampedToMaxSize(Settings.MaxGrabSpeedCmPerSecond) / Scale;
            // A bounded velocity drive keeps all cloth constraints, masses and
            // pinned particles under the solver's control. No teleport or pin edit.
            const FVector Result = FMath::Lerp(FVector(Velocities[Hand.ParticleIndex]), DesiredVelocity,
                FMath::Clamp(Settings.GrabVelocityBlend * Hand.Delta * 90.0f, 0.0f, 1.0f));
            if (Result.ContainsNaN()) { SetFault(TEXT("invalid grab velocity")); return; }
            Velocities[Hand.ParticleIndex] = Chaos::Softs::FSolverVec3(Result);
            continue;
        }
        // The capsule covers the entire frame segment, including a fast crossing
        // whose endpoint is beyond a thin cage. New/recovered samples are seeded.
        const int32 Start = Collisions.Spheres.Num();
        Collisions.Spheres.Emplace(Settings.HandRadiusCm / ComponentScale, ComponentToWorld.InverseTransformPosition(Hand.Previous));
        Collisions.Spheres.Emplace(Settings.HandRadiusCm / ComponentScale, ComponentToWorld.InverseTransformPosition(Hand.Current));
        Collisions.SphereConnections.Emplace(Start, Start + 1);
    }
    for (auto& Instance : Instances)
    {
        if (!GratiaGetChaosClothSolver(Instance)) continue;
        // This component owns the avatar's external cloth hand-collision slot.
        // Internal PhysicsAsset collisions and registered collision sources persist.
        Instance.ClearExternalCollisions();
        if (!Collisions.IsEmpty()) Instance.AddExternalCollisions(Collisions);
    }
}

void UGratiaClothInteraction::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    if (!Character.IsValid() || !Character->CharacterProfile || !Character->CharacterMesh) return;
    if (LastProfile.Get() != Character->CharacterProfile.Get())
    { LastProfile = Character->CharacterProfile; ResetCloth(); }
    if (!IsEnabled())
    {
        const auto* Mesh = Character->CharacterMesh.Get();
        const bool bHasNativeCloth = !Mesh->GetClothingSimulationInstances().IsEmpty()
            || (Mesh->GetSkeletalMeshAsset() && !Mesh->GetSkeletalMeshAsset()->GetMeshClothingAssets().IsEmpty());
        if (bRunning || (bHasNativeCloth && (!Mesh->IsClothingSimulationSuspended() || Mesh->ClothBlendWeight > 0)))
            StopCloth(true);
        ClearHands(); return;
    }
    if (!bRunning)
    {
        Character->CharacterMesh->ClothBlendWeight = 1.0f;
        Character->CharacterMesh->ResumeClothingSimulation();
        Character->CharacterMesh->ForceClothNextUpdateTeleportAndReset();
        bRunning = true; ClearHands();
    }
    const bool bValidDelta = FMath::IsFinite(Delta) && Delta > 0 && Delta <= 0.1f;
    if (!bValidDelta) { ClearHands(); ClearExternalCollisions(); }
    UpdateNativeCloth(Delta, bValidDelta);
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0;
    if (Now >= NextDiagnosticTime)
    {
        NextDiagnosticTime = Now + 2;
        UE_LOG(LogGratiaClothHands, Display, TEXT("SOURCE_CLOTH %s"), *GetDiagnostics());
        FString Regions;
        const auto* Profile = Character->CharacterProfile.Get();
        for (int32 Region = 0; Region < RegionDisplacementCm.Num() && Profile && Region < Profile->SourceClothRegions.Num(); ++Region)
            Regions += FString::Printf(TEXT(" %s=%.3f/%.3f"), *Profile->SourceClothRegions[Region].Name.ToString(),
                RegionDisplacementCm[Region].X, RegionDisplacementCm[Region].Y);
        if (!Regions.IsEmpty()) UE_LOG(LogGratiaClothHands, Display, TEXT("SOURCE_CLOTH_REGIONS mean/max cm:%s"), *Regions);
    }
}

FString UGratiaClothInteraction::GetDiagnostics() const
{
    return FString::Printf(TEXT("cages=%d dynamic=%d displacement=%.3fcm mean=%.3fcm hands=%d press L=%.1f R=%.1fcm grab L=%d R=%d state=%s%s"),
        ManagedCageCount, DynamicParticleCount, MaxDisplacementCm, MeanDisplacementCm, ActiveHandCount, PressDepthCm[0], PressDepthCm[1],
        Hands[0].ParticleIndex, Hands[1].ParticleIndex, bFault ? TEXT("FAULT ") : bRunning ? TEXT("running") : TEXT("disabled"), *FaultReason);
}

bool UGratiaClothInteraction::GetCageParticleSnapshot(FName AssetName, TArray<FVector>& Positions) const
{
    if (const auto* Snapshot = CageParticleSnapshots.Find(AssetName)) { Positions = *Snapshot; return true; }
    Positions.Reset(); return false;
}

bool UGratiaClothInteraction::GetCageInverseMassSnapshot(FName AssetName, TArray<float>& InverseMasses) const
{
    if (const auto* Snapshot = CageInverseMassSnapshots.Find(AssetName)) { InverseMasses = *Snapshot; return true; }
    InverseMasses.Reset(); return false;
}

bool UGratiaClothInteraction::RunChecks(FString& Failure)
{
    if (!Character.IsValid() || !Character->CharacterProfile || !Character->CharacterMesh)
    { Failure = TEXT("Source cloth requires a character, profile and mesh"); return false; }
    const auto* Profile = Character->CharacterProfile.Get();
    if (!Profile->ClothSettings.bEnabled || Profile->SourceClothCages.IsEmpty())
    {
        UE_LOG(LogGratiaClothHands, Display, TEXT("SOURCE_CLOTH_CHECK skip=profile capability unavailable"));
        return true;
    }
    UpdateNativeCloth(0, false);
    if (bFault) { Failure = TEXT("Source cloth safety fault: ") + FaultReason; return false; }
    const auto& Instances = Character->CharacterMesh->GetClothingSimulationInstances();
    for (const auto& Instance : Instances)
        if (!Instance.GetClothingSimulationFactory() || !Instance.GetClothingSimulationFactory()->IsA<UChaosClothingSimulationFactory>())
        { Failure = TEXT("Source cloth requires the supported native Chaos factory"); return false; }
    for (const auto& Cage : Profile->SourceClothCages)
    {
        const auto* Snapshot = CageParticleSnapshots.Find(Cage.AssetName);
        if (!Snapshot || Snapshot->IsEmpty() || (Cage.ExpectedParticleCount > 0 && Snapshot->Num() != Cage.ExpectedParticleCount))
        { Failure = TEXT("Missing active source cage or unexpected particle count: ") + Cage.AssetName.ToString(); return false; }
    }
    if (ManagedCageCount != Profile->SourceClothCages.Num() || DynamicParticleCount <= 0)
    { Failure = TEXT("Source cloth has no active dynamic particles or incomplete cage coverage"); return false; }
    return true;
}
