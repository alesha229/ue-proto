#include "GratiaClothVerification.h"

#include "GratiaCharacterProfile.h"
#include "GratiaClothInteraction.h"
#include "GratiaInteraction.h"
#include "GratiaPreviewCharacter.h"
#include "ClothingAssetBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "RHI.h"
#include "SkeletalRenderPublic.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaClothQA, Log, All);

UGratiaClothVerification::UGratiaClothVerification()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGratiaClothVerification::BeginPlay()
{
    Super::BeginPlay();
    bRequested = FParse::Param(FCommandLine::Get(), TEXT("GratiaClothQA"));
    SetComponentTickEnabled(bRequested);
    if (!bRequested) return;
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
    if (!Character.IsValid() || !Character->Interaction || !Character->CharacterMesh || !Character->ClothInteraction)
    { Check(false, TEXT("character cloth integration components exist")); Finish(); return; }
    AddTickPrerequisiteComponent(Character->CharacterMesh);
    AddTickPrerequisiteComponent(Character->ClothInteraction);
    bSavedContactTick = Character->Interaction->IsComponentTickEnabled();
    bSavedDemo = Character->Interaction->bDemo;
    bSavedBodyMotion = Character->Interaction->bBodyMotion;
    // This focused test exercises the constraint/tracking gate and native cloth.
    // Reaction playback is excluded so it cannot manufacture the measured motion.
    Character->Interaction->SetComponentTickEnabled(false);
    Character->Interaction->bDemo = false;
    UE_LOG(LogGratiaClothQA, Display, TEXT("CLOTH_QA_START source=synthetic constrained hands; explicit trigger bypasses input actions; desktop only"));
}

void UGratiaClothVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    if (bRequested && Character.IsValid() && Character->Interaction)
    {
        Character->Interaction->bDemo = bSavedDemo;
        Character->Interaction->bBodyMotion = bSavedBodyMotion;
        Character->Interaction->SetComponentTickEnabled(bSavedContactTick);
        if (Character->ClothInteraction) Character->ClothInteraction->ClearHands();
    }
    Super::EndPlay(Reason);
}

bool UGratiaClothVerification::Check(bool bPass, const FString& Description)
{
    if (bPass) { UE_LOG(LogGratiaClothQA, Display, TEXT("CLOTH_QA_CHECK PASS %s"), *Description); }
    else { bFailed = true; UE_LOG(LogGratiaClothQA, Error, TEXT("CLOTH_QA_CHECK FAIL %s"), *Description); }
    return bPass;
}

void UGratiaClothVerification::Advance(EPhase Next)
{
    Phase = Next;
    PhaseSeconds = 0;
}

void UGratiaClothVerification::Finish()
{
    if (bFinished) return;
    bFinished = true;
    SetComponentTickEnabled(false);
    if (Character.IsValid() && Character->ClothInteraction) Character->ClothInteraction->ClearHands();
    UE_LOG(LogGratiaClothQA, Display, TEXT("GRATIA_CLOTH_QA %s regions=%d protected=%.5fcm unaffected=%.5fcm elapsed=%.2fs source=synthetic"),
        bFailed ? TEXT("FAIL") : TEXT("PASS"), RegionIndex, MaxProtectedCm, MaxUnaffectedCm, Elapsed);
    FPlatformMisc::RequestExitWithStatus(false, bFailed ? 1 : 0);
}

const FGratiaSourceClothRegion* UGratiaClothVerification::GetRegion() const
{
    if (!Character.IsValid() || !Character->CharacterProfile) return nullptr;
    const auto& Regions = Character->CharacterProfile->SourceClothRegions;
    return Regions.IsValidIndex(RegionIndex) ? &Regions[RegionIndex] : nullptr;
}

FTransform UGratiaClothVerification::GetRegionAnchor() const
{
    const auto* Region = GetRegion();
    if (!Region || !Character.IsValid() || !Character->CharacterProfile || !Character->CharacterMesh) return FTransform::Identity;
    FTransform Anchor = Character->CharacterMesh->GetSocketTransform(Character->CharacterProfile->ResolveBone(Region->AnchorSemantic));
    // Anchor removes rigid animation motion only. Imported bone scale is not a
    // unit conversion for these already-world-space centimetre snapshots.
    Anchor.SetScale3D(FVector::OneVector);
    return Anchor;
}

bool UGratiaClothVerification::BuildRenderCoverage()
{
    auto* Mesh = Character->CharacterMesh.Get();
    const auto* Asset = Mesh->GetSkeletalMeshAsset();
    const auto* RenderData = Asset ? Asset->GetResourceForRendering() : nullptr;
    const auto* Profile = Character->CharacterProfile.Get();
    if (!Check(Profile && RenderData && !RenderData->LODRenderData.IsEmpty(), TEXT("LOD0 render data exists"))) return false;
    const auto& LOD = RenderData->LODRenderData[0];
    const auto& Assets = Asset->GetMeshClothingAssets();
    const auto& RefSkeleton = Asset->GetRefSkeleton();
    const auto& Weights = LOD.SkinWeightVertexBuffer;
    RegionRenderVertices.SetNum(Profile->SourceClothRegions.Num());
    TSet<int32> Affected;
    TArray<int32> ProtectedBones;
    int32 ProtectedMappedLogged = 0;
    for (FName Semantic : {FName(TEXT("Head")), FName(TEXT("LeftFoot")), FName(TEXT("RightFoot"))})
    {
        const int32 Index = Mesh->GetBoneIndex(Profile->ResolveBone(Semantic));
        if (Index != INDEX_NONE) ProtectedBones.Add(Index);
    }
    for (const auto& Section : LOD.RenderSections)
    {
        if (Section.bDisabled) continue;
        const auto* Mapping = Section.ClothMappingDataLODs.IsEmpty() ? nullptr : &Section.ClothMappingDataLODs[0];
        const FName AssetName = Assets.IsValidIndex(Section.CorrespondClothAssetIndex) && Assets[Section.CorrespondClothAssetIndex]
            ? Assets[Section.CorrespondClothAssetIndex]->GetFName() : NAME_None;
        if (Mapping && !Check(Mapping->Num() == Section.GetNumVertices(), TEXT("QA supports one cloth triangle influence per render vertex"))) return false;
        for (int32 Local = 0; Local < Section.GetNumVertices(); ++Local)
        {
            const int32 Vertex = Section.GetVertexBufferIndex() + Local;
            const FMeshToMeshVertData* Map = Mapping && Mapping->IsValidIndex(Local) ? &(*Mapping)[Local] : nullptr;
            if (Map && Map->SourceMeshVertIndices[3] < 0xffff)
            {
                Affected.Add(Vertex);
                for (int32 Region = 0; Region < Profile->SourceClothRegions.Num(); ++Region)
                {
                    const auto& Definition = Profile->SourceClothRegions[Region];
                    bool bSameRegion = Definition.AssetName == AssetName;
                    for (int32 Corner = 0; Corner < 3; ++Corner)
                        bSameRegion &= Map->SourceMeshVertIndices[Corner] >= Definition.FirstParticle
                            && Map->SourceMeshVertIndices[Corner] < Definition.FirstParticle + Definition.ParticleCount;
                    if (bSameRegion) RegionRenderVertices[Region].Add(Vertex);
                }
            }
            uint16 BestWeight = 0;
            int32 DominantBone = INDEX_NONE;
            for (uint32 Influence = 0; Influence < Weights.GetMaxBoneInfluences(); ++Influence)
            {
                const uint16 Weight = Weights.GetBoneWeight(Vertex, Influence);
                const int32 LocalBone = int32(Weights.GetBoneIndex(Vertex, Influence));
                if (Weight > BestWeight && Section.BoneMap.IsValidIndex(LocalBone))
                { BestWeight = Weight; DominantBone = Section.BoneMap[LocalBone]; }
            }
            bool bProtected = false;
            for (int32 Bone = DominantBone; Bone != INDEX_NONE; Bone = RefSkeleton.GetParentIndex(Bone))
                bProtected |= ProtectedBones.Contains(Bone);
            if (bProtected) { ProtectedRenderVertices.Add(Vertex); ProtectedDominantBones.Add(Vertex, DominantBone); }
            if (bProtected && Map && Map->SourceMeshVertIndices[3] < 0xffff && ++ProtectedMappedLogged <= 3)
            {
                FString Influences;
                for (uint32 Influence = 0; Influence < Weights.GetMaxBoneInfluences(); ++Influence)
                    if (const uint16 Weight = Weights.GetBoneWeight(Vertex, Influence))
                    {
                        const int32 LocalBone = int32(Weights.GetBoneIndex(Vertex, Influence));
                        Influences += FString::Printf(TEXT(" %s=%u"), Section.BoneMap.IsValidIndex(LocalBone)
                            ? *RefSkeleton.GetBoneName(Section.BoneMap[LocalBone]).ToString() : TEXT("?"), Weight);
                    }
                const FVector3f P = LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Vertex);
                UE_LOG(LogGratiaClothQA, Warning, TEXT("CLOTH_QA_PROTECTED_MAPPED vertex=%d section_base=%d pos=(%.2f,%.2f,%.2f) sim_weight=%u influences:%s"),
                    Vertex, Section.GetVertexBufferIndex(), P.X, P.Y, P.Z, 65535 - Map->SourceMeshVertIndices[3], *Influences);
            }
            if (!Affected.Contains(Vertex)) UnaffectedRenderVertices.Add(Vertex);
        }
    }
    bool bPass = Check(ProtectedBones.Num() == 3 && !ProtectedRenderVertices.IsEmpty(), TEXT("semantic head and feet have preserved render vertices"));
    bPass &= Check(!UnaffectedRenderVertices.IsEmpty(), TEXT("unmapped render vertices exist"));
    for (int32 Region = 0; Region < RegionRenderVertices.Num(); ++Region)
        bPass &= Check(!RegionRenderVertices[Region].IsEmpty(), FString::Printf(TEXT("region=%s visible mapped vertices=%d"),
            *Profile->SourceClothRegions[Region].Name.ToString(), RegionRenderVertices[Region].Num()));
    return bPass;
}

bool UGratiaClothVerification::CaptureRenderDeltas(TArray<FVector>& Deltas)
{
    auto* Mesh = Character->CharacterMesh.Get();
    if (!Check(!GUsingNullRHI && Mesh->IsRegistered() && Mesh->IsRenderStateCreated(),
        TEXT("rendered desktop run is available for CPU cloth readback"))) return false;
    Mesh->WaitForExistingParallelClothSimulation_GameThread();
    const float Blend = Mesh->ClothBlendWeight;
    if (!Check(FMath::IsFinite(Blend) && Blend > 0, TEXT("native cloth blend is active"))) return false;
    const bool bCPUSkinning = Mesh->GetCPUSkinningEnabled();
    TArray<FFinalSkinVertex> ClothVertices, SkinnedVertices;
    // GetCPUSkinnedVertices recreates the render object when switching skinning.
    // Force the switch for each read so a previously cached CPU object cannot
    // report the same stale final vertices for both blend settings.
    Mesh->SetCPUSkinningEnabled(false, true);
    Mesh->GetCPUSkinnedVertices(ClothVertices, 0);
    Mesh->ClothBlendWeight = 0;
    Mesh->SetCPUSkinningEnabled(false, true);
    Mesh->GetCPUSkinnedVertices(SkinnedVertices, 0);
    Mesh->ClothBlendWeight = Blend;
    Mesh->SetCPUSkinningEnabled(bCPUSkinning, true);
    if (!Check(!ClothVertices.IsEmpty() && ClothVertices.Num() == SkinnedVertices.Num(), TEXT("same-frame native cloth / zero-blend CPU render readback"))) return false;
    Deltas.SetNum(ClothVertices.Num());
    const FTransform World = Mesh->GetComponentTransform();
    for (int32 Index = 0; Index < Deltas.Num(); ++Index)
    {
        const FVector Cloth(ClothVertices[Index].Position), Skin(SkinnedVertices[Index].Position);
        if (Cloth.ContainsNaN() || Skin.ContainsNaN()) return Check(false, TEXT("finite rendered mesh vertices"));
        Deltas[Index] = World.TransformVector(Cloth - Skin);
    }
    for (int32 Index : UnaffectedRenderVertices)
        if (Deltas.IsValidIndex(Index)) MaxUnaffectedCm = FMath::Max(MaxUnaffectedCm, Deltas[Index].Size());
    int32 WorstProtected = INDEX_NONE;
    for (int32 Index : ProtectedRenderVertices)
        if (Deltas.IsValidIndex(Index) && Deltas[Index].Size() > MaxProtectedCm) { MaxProtectedCm = Deltas[Index].Size(); WorstProtected = Index; }
    if (WorstProtected != INDEX_NONE && MaxProtectedCm > 0.02)
        UE_LOG(LogGratiaClothQA, Warning, TEXT("CLOTH_QA_PROTECTED_WORST vertex=%d bone=%s cloth_mapped=%s delta=%.4fcm protected_count=%d"),
            WorstProtected, *Character->CharacterMesh->GetBoneName(ProtectedDominantBones.FindRef(WorstProtected)).ToString(), UnaffectedRenderVertices.Contains(WorstProtected) ? TEXT("no") : TEXT("yes"), MaxProtectedCm, ProtectedRenderVertices.Num());
    return Check(MaxUnaffectedCm <= 0.02 && MaxProtectedCm <= 0.02,
        FString::Printf(TEXT("same-frame cloth preserves face/feet %.5fcm and unmapped vertices %.5fcm"), MaxProtectedCm, MaxUnaffectedCm));
}

bool UGratiaClothVerification::SelectReachableParticle()
{
    const auto* Region = GetRegion();
    TArray<FVector> Particles;
    TArray<float> InverseMasses;
    if (!Region || !Character->ClothInteraction->GetCageParticleSnapshot(Region->AssetName, Particles)
        || !Character->ClothInteraction->GetCageInverseMassSnapshot(Region->AssetName, InverseMasses))
        return Check(false, TEXT("current source cage positions and dynamic mask exist"));
    if (!Check(Region->FirstParticle >= 0 && Region->ParticleCount > 0
        && Region->FirstParticle + Region->ParticleCount <= Particles.Num() && InverseMasses.Num() == Particles.Num(), TEXT("source region bounds match native particles"))) return false;
    const FVector Forward = Character->GetActorTransform().TransformVectorNoScale(Character->CharacterProfile->ForwardAxis).GetSafeNormal();
    const FVector Up = Character->GetActorTransform().TransformVectorNoScale(Character->CharacterProfile->UpAxis).GetSafeNormal();
    const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
    if (!Check(!Forward.IsNearlyZero() && !Right.IsNearlyZero(), TEXT("profile cloth probe axes are valid"))) return false;
    const float Radius = Character->CharacterProfile->ClothSettings.HandRadiusCm;
    double BestGap = DBL_MAX;
    CandidateParticle = INDEX_NONE;
    // A source region can be exposed at the front, back or side. Use its actual
    // dynamic extrema and the same visible-hand constraint used during play.
    for (const FVector& Direction : {Forward, -Forward, Right, -Right})
    {
        double BestProjection = -DBL_MAX;
        int32 Extremum = INDEX_NONE;
        for (int32 Particle = Region->FirstParticle; Particle < Region->FirstParticle + Region->ParticleCount; ++Particle)
        {
            if (InverseMasses[Particle] <= 0) continue;
            const double Projection = FVector::DotProduct(Particles[Particle], Direction);
            if (Projection > BestProjection) { BestProjection = Projection; Extremum = Particle; }
        }
        if (Extremum == INDEX_NONE) continue;
        const FVector Outside = Particles[Extremum] + Direction * (Radius + 12);
        const FVector Touch = Particles[Extremum] + Direction * (Radius * 0.75f);
        const FTransform Constrained = Character->Interaction->ConstrainHand(FTransform(FQuat::Identity, Outside), FTransform(FQuat::Identity, Touch), true);
        // Same soft-press limit as play: the cloth collider may pass the constrained hand toward the raw target.
        const float PressDepth = FMath::Clamp(Character->CharacterProfile->ClothSettings.SoftPressDepthCm, 0.f, 10.f);
        const FVector Press = Constrained.GetLocation() + (Touch - Constrained.GetLocation()).GetClampedToMaxSize(PressDepth);
        const double Gap = FVector::Distance(Press, Particles[Extremum]);
        if (Constrained.ContainsNaN() || Gap >= BestGap) continue;
        // Source cages overlap (AssPhys / ThighsPhys). A grab takes the nearest
        // dynamic particle of any cage, so only accept probes where that particle
        // belongs to the region under test.
        int32 Nearest = INDEX_NONE;
        double NearestDistance = DBL_MAX;
        for (int32 Particle = 0; Particle < Particles.Num(); ++Particle)
        {
            if (InverseMasses[Particle] <= 0) continue;
            const double Distance = FVector::Distance(Press, Particles[Particle]);
            if (Distance < NearestDistance) { NearestDistance = Distance; Nearest = Particle; }
        }
        if (Nearest < Region->FirstParticle || Nearest >= Region->FirstParticle + Region->ParticleCount) continue;
        BestGap = Gap;
        CandidateParticle = Extremum;
        ProbeForward = Direction;
        HandOutside = Outside;
        HandTouch = Touch;
    }
    if (!Check(CandidateParticle != INDEX_NONE && BestGap < Radius,
        FString::Printf(TEXT("dynamic cloth extremum is reachable by constrained visible hand gap=%.3fcm"), BestGap))) return false;
    HandVisual = FTransform(FQuat::Identity, HandOutside);
    UE_LOG(LogGratiaClothQA, Display, TEXT("CLOTH_QA_TARGET region=%s particle=%d source=(%.3f,%.3f,%.3f) hand=(%.3f,%.3f,%.3f)"),
        *Region->Name.ToString(), CandidateParticle, Particles[CandidateParticle].X, Particles[CandidateParticle].Y, Particles[CandidateParticle].Z,
        HandTouch.X, HandTouch.Y, HandTouch.Z);
    return true;
}

bool UGratiaClothVerification::StartRegion()
{
    const auto* Region = GetRegion();
    if (!Check(Region != nullptr, TEXT("validation region exists in active profile"))) return false;
    const FName Anchor = Character->CharacterProfile->ResolveBone(Region->AnchorSemantic);
    if (!Check(!Anchor.IsNone() && Character->CharacterMesh->GetBoneIndex(Anchor) != INDEX_NONE,
        TEXT("region anchor semantic resolves to the active skeleton"))) return false;
    Character->ClothInteraction->ClearHands();
    Character->Interaction->ResetState();
    BaselineParticlesLocal.Reset(); BaselineClothDeltas.Reset();
    PressureParticleCm = PressureRenderCm = PullParticleCm = PullRenderCm = 0;
    bSawReadyHand = bSawGrab = bSawPressure = bSawPull = false;
    Advance(EPhase::Baseline);
    return true;
}

void UGratiaClothVerification::SubmitSyntheticHand(const FVector& Target, float Delta, float Trigger, bool bTracked)
{
    auto* Contact = Character->Interaction.Get();
    const FTransform Raw(FQuat::Identity, Target);
    HandVisual = Contact->ConstrainHand(HandVisual, Raw, true);
    Contact->SetHandSample(true, Raw, HandVisual, bTracked);
    const bool bReady = Contact->IsHandSampleReady(true);
    bSawReadyHand |= bReady;
    Character->ClothInteraction->SubmitHand(true, HandVisual.GetLocation(), Target, bReady, Delta, Trigger);
    Character->ClothInteraction->SubmitHand(false, FVector::ZeroVector, FVector::ZeroVector, false, Delta, 0);
}

void UGratiaClothVerification::SampleRegionMotion(double& ParticleCm, double& RenderCm, bool bCaptureRender)
{
    const auto* Region = GetRegion();
    TArray<FVector> Current;
    if (!Region || !Character->ClothInteraction->GetCageParticleSnapshot(Region->AssetName, Current)) return;
    const FTransform Anchor = GetRegionAnchor();
    for (int32 Local = 0; Local < BaselineParticlesLocal.Num(); ++Local)
    {
        const int32 Particle = Region->FirstParticle + Local;
        if (Current.IsValidIndex(Particle)) ParticleCm = FMath::Max(ParticleCm,
            FVector::Distance(Anchor.InverseTransformPosition(Current[Particle]), BaselineParticlesLocal[Local]));
    }
    if (!bCaptureRender) return;
    TArray<FVector> CurrentDeltas;
    if (!CaptureRenderDeltas(CurrentDeltas)) return;
    for (int32 Vertex : RegionRenderVertices[RegionIndex])
        if (CurrentDeltas.IsValidIndex(Vertex) && BaselineClothDeltas.IsValidIndex(Vertex))
            RenderCm = FMath::Max(RenderCm, FVector::Distance(CurrentDeltas[Vertex], BaselineClothDeltas[Vertex]));
}

void UGratiaClothVerification::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    if (!bRequested || bFinished) return;
    Elapsed += Delta;
    if (Elapsed > 75 || !Character.IsValid() || !Character->CharacterProfile || Character->ClothInteraction->HasFault())
    { Check(false, TEXT("cloth QA timeout or native safety fault")); Finish(); return; }
    if (!FMath::IsFinite(Delta) || Delta <= 0 || Delta > 0.1f) return;
    PhaseSeconds += Delta;
    FString Failure;
    if (Phase != EPhase::Settle && Phase != EPhase::Disabled && Phase != EPhase::Resumed
        && !Character->ClothInteraction->RunChecks(Failure))
    { Check(false, Failure); Finish(); return; }
    const auto* Region = GetRegion();
    const float Step = Delta;
    switch (Phase)
    {
    case EPhase::Settle:
        if (PhaseSeconds < 3) break;
        InitialCollisionProxyCount = Character->CharacterProfile->CollisionProxies.Num();
        if (!Check(Character->CharacterProfile->ClothSettings.bEnabled && !Character->CharacterProfile->SourceClothRegions.IsEmpty(),
            TEXT("profile opts into native source cloth and named validation regions"))
            || !Check(Character->ClothInteraction->RunChecks(Failure), Failure.IsEmpty() ? TEXT("native particle safety and full cage coverage") : Failure)
            || !BuildRenderCoverage() || !StartRegion()) { Finish(); }
        break;
    case EPhase::Baseline:
        if (PhaseSeconds < 0.4f) break;
        {
            TArray<FVector> Positions;
            if (!Check(Region && Character->ClothInteraction->GetCageParticleSnapshot(Region->AssetName, Positions),
                    TEXT("settled source cage snapshot exists"))
                || !SelectReachableParticle() || !CaptureRenderDeltas(BaselineClothDeltas)) { Finish(); break; }
            const FTransform Anchor = GetRegionAnchor();
            for (int32 Index = Region->FirstParticle; Index < Region->FirstParticle + Region->ParticleCount; ++Index)
                BaselineParticlesLocal.Add(Anchor.InverseTransformPosition(Positions[Index]));
        }
        Advance(EPhase::Approach);
        break;
    case EPhase::Approach:
        SubmitSyntheticHand(FMath::Lerp(HandOutside, HandTouch, FMath::Clamp(PhaseSeconds / 0.8f, 0.0f, 1.0f)), Step, 0);
        if (PhaseSeconds >= 0.8f) Advance(EPhase::Push);
        break;
    case EPhase::Push:
        SubmitSyntheticHand(HandTouch, Step, 0);
        SampleRegionMotion(PressureParticleCm, PressureRenderCm, PhaseSeconds >= 0.8f);
        bSawPressure |= Character->ClothInteraction->GetActiveHandCount() > 0;
        if (PhaseSeconds >= 0.8f)
        {
            Check(bSawReadyHand && bSawPressure, TEXT("pressure passed the constrained visible hand / tracking gate"));
            Check(PressureParticleCm >= 0.05 && PressureRenderCm >= 0.05, FString::Printf(TEXT("region=%s pressure particles=%.4fcm visible mesh=%.4fcm minimum=0.05cm"),
                *Region->Name.ToString(), PressureParticleCm, PressureRenderCm));
            Advance(EPhase::Arm);
        }
        break;
    case EPhase::Arm:
        SubmitSyntheticHand(HandTouch, Step, 0);
        if (PhaseSeconds >= 0.3f) Advance(EPhase::Grab);
        break;
    case EPhase::Grab:
        SubmitSyntheticHand(HandTouch, Step, 1);
        {
            const int32 Grabbed = Character->ClothInteraction->GetGrabbedParticle(true);
            bSawGrab |= Grabbed >= Region->FirstParticle && Grabbed < Region->FirstParticle + Region->ParticleCount;
        }
        if (PhaseSeconds >= 0.3f)
        { Check(bSawGrab, TEXT("explicit synthetic trigger acquires a dynamic particle in this region")); Advance(EPhase::Pull); }
        break;
    case EPhase::Pull:
        // A held pull of 3cm over 0.6s: a small but realistic grab gesture.
        SubmitSyntheticHand(HandTouch + ProbeForward * 3.0f * FMath::Clamp(PhaseSeconds / 0.6f, 0.0f, 1.0f), Step, 1);
        SampleRegionMotion(PullParticleCm, PullRenderCm, PhaseSeconds >= 0.6f);
        bSawPull |= Character->ClothInteraction->GetGrabbedParticle(true) != INDEX_NONE;
        if (PhaseSeconds >= 0.6f)
        {
            Check(bSawPull && PullParticleCm >= 0.05 && PullRenderCm >= 0.05,
                FString::Printf(TEXT("region=%s held pull particles=%.4fcm visible mesh=%.4fcm minimum=0.05cm"),
                    *Region->Name.ToString(), PullParticleCm, PullRenderCm));
            Advance(EPhase::Release);
        }
        break;
    case EPhase::Release:
        SubmitSyntheticHand(HandTouch, Step, 0);
        if (PhaseSeconds >= 0.15f)
        { Check(Character->ClothInteraction->GetGrabbedParticle(true) == INDEX_NONE, TEXT("trigger release clears held cloth particle")); Advance(EPhase::ReArm); }
        break;
    case EPhase::ReArm:
        SubmitSyntheticHand(HandTouch, Step, 0);
        if (PhaseSeconds >= 0.25f) Advance(EPhase::ReGrab);
        break;
    case EPhase::ReGrab:
        SubmitSyntheticHand(HandTouch, Step, 1);
        if (PhaseSeconds >= 0.25f)
        { Check(Character->ClothInteraction->GetGrabbedParticle(true) != INDEX_NONE, TEXT("released trigger can intentionally reacquire")); Advance(EPhase::Lost); }
        break;
    case EPhase::Lost:
        SubmitSyntheticHand(HandTouch, Step, 1, false);
        if (PhaseSeconds >= 0.15f)
        { Check(Character->ClothInteraction->GetGrabbedParticle(true) == INDEX_NONE, TEXT("tracking loss immediately clears cloth hold")); Advance(EPhase::HeldRecovery); }
        break;
    case EPhase::HeldRecovery:
        SubmitSyntheticHand(HandTouch, Step, 1);
        if (PhaseSeconds >= 0.25f)
        { Check(Character->ClothInteraction->GetGrabbedParticle(true) == INDEX_NONE, TEXT("held trigger does not reacquire during tracking recovery")); Advance(EPhase::FinishRegion); }
        break;
    case EPhase::FinishRegion:
        Character->ClothInteraction->ClearHands();
        UE_LOG(LogGratiaClothQA, Display, TEXT("CLOTH_QA_REGION %s pressure particle=%.4f render=%.4f pull particle=%.4f render=%.4f source=synthetic"),
            *Region->Name.ToString(), PressureParticleCm, PressureRenderCm, PullParticleCm, PullRenderCm);
        ++RegionIndex;
        if (RegionIndex < Character->CharacterProfile->SourceClothRegions.Num())
        { if (!StartRegion()) Finish(); }
        else { Character->ClothInteraction->ResetCloth(); Advance(EPhase::Reset); }
        break;
    case EPhase::Reset:
        if (PhaseSeconds < 1) break;
        Check(!Character->ClothInteraction->HasFault() && Character->ClothInteraction->GetGrabbedParticle(true) == INDEX_NONE
            && Character->ClothInteraction->GetGrabbedParticle(false) == INDEX_NONE, TEXT("explicit reset restores finite cloth with no held particles"));
        Character->Interaction->bBodyMotion = false;
        Advance(EPhase::Disabled);
        break;
    case EPhase::Disabled:
        if (PhaseSeconds < 0.2f) break;
        Check(FMath::IsFinite(Character->CharacterMesh->ClothBlendWeight) && Character->CharacterMesh->ClothBlendWeight == 0.0f,
            TEXT("body motion menu setting disabled native cloth render blend"));
        Check(Character->CharacterMesh->IsClothingSimulationSuspended(), TEXT("body motion disabled native cloth simulation"));
        Check(Character->ClothInteraction->GetGrabbedParticle(true) == INDEX_NONE
            && Character->ClothInteraction->GetGrabbedParticle(false) == INDEX_NONE,
            TEXT("body motion disabled clears both cloth holds"));
        Check(InitialCollisionProxyCount > 0 && Character->CharacterProfile->CollisionProxies.Num() == InitialCollisionProxyCount,
            FString::Printf(TEXT("body motion disabled preserves %d independent safe collision proxy definitions"), InitialCollisionProxyCount));
        Character->Interaction->bBodyMotion = true;
        Advance(EPhase::Resumed);
        break;
    case EPhase::Resumed:
        if (PhaseSeconds < 0.5f) break;
        Check(Character->ClothInteraction->RunChecks(Failure), Failure.IsEmpty() ? TEXT("body motion reenabled native source cage checks") : Failure);
        Check(!Character->CharacterMesh->IsClothingSimulationSuspended() && Character->CharacterMesh->ClothBlendWeight > 0
            && Character->ClothInteraction->GetDynamicParticleCount() > 0 && !Character->ClothInteraction->HasFault(),
            TEXT("body motion reenabled resumes finite native cloth with dynamic particles and render blend"));
        Finish();
        break;
    }
}
