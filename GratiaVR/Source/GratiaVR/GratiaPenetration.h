#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaCharacterProfile.h"
#include "GratiaPenetrationMath.h"
#include "GratiaPenetration.generated.h"

class AGratiaPreviewCharacter;
class AGratiaPenetrator;

/**
 * Character side of penetration. Resolves the profile channels on the current pose, captures the
 * selected jointed shaft at an entrance, lays its joints along the curve from the hand into the
 * channel, opens and drags the wall bones (UGratiaAnimInstance applies the offsets after the soft
 * body), drives the opening morph, and reports reactions and haptics. The held shaft is solved right
 * after the hands moved (Solve from the runtime); otherwise once per tick.
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaPenetration : public UActorComponent
{
    GENERATED_BODY()

public:
    UGratiaPenetration();

    /** Explicit selection: the shaft this character responds to (nullptr clears it). */
    UFUNCTION(BlueprintCallable, Category = "Penetration")
    void SetPenetrator(AGratiaPenetrator* Shaft);
    AGratiaPenetrator* GetPenetrator() const { return Penetrator.Get(); }
    /** Releases the shaft and returns every wall bone to its pose. */
    UFUNCTION(BlueprintCallable, Category = "Penetration")
    void ResetPenetration();
    void Solve(float Delta);

    bool IsEnabled() const;
    int32 GetChannelCount() const { return Channels.Num(); }
    FName GetEngagedChannel() const { return Channels.IsValidIndex(Engaged.Channel) ? Channels[Engaged.Channel].Name : NAME_None; }
    float GetDepthCm() const { return Channels.IsValidIndex(Engaged.Channel) ? float(FMath::Max(0.0, Engaged.Inserted)) : 0.0f; }
    float GetOpeningCm() const { return float(Engaged.Opening); }
    /** Largest wall bone offset this frame (cm, world). */
    float GetMaxWallOffsetCm() const;
    bool RunChecks(FString& Failure);
    FString GetDiagnostics() const;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

private:
    struct FWallBone
    {
        FName Name;
        /** Distance along the channel from the entrance (component cm). */
        double Depth = 0.0;
        /** Unit direction away from the channel axis, anchor-bone space. */
        FVector Radial = FVector::ZeroVector;
        FGratiaChannelBone Settings;
        FVector Offset = FVector::ZeroVector;
        FVector Target = FVector::ZeroVector;
    };
    struct FChannel
    {
        FName Name;
        int32 Definition = INDEX_NONE;
        int32 Anchor = INDEX_NONE;
        FVector EntranceLocal = FVector::ZeroVector;
        FVector InwardLocal = FVector::XAxisVector;
        TArray<FWallBone> Bones;
        FName Morph;
        bool bMorphSet = false;
        // World frame this solve.
        FTransform AnchorWorld = FTransform::Identity;
        GratiaPenetration::FPath Path;
        FVector Entrance = FVector::ZeroVector;
        FVector Inward = FVector::XAxisVector;
        double Scale = 1.0;
        bool bFrame = false;
    };
    struct FEngagement
    {
        int32 Channel = INDEX_NONE;
        double Inserted = 0.0;
        double Velocity = 0.0;
        double Travel = 0.0;
        double Opening = 0.0;
        double Pulse = 0.0;
        /** First solve after the capture: no speed or travel yet. */
        bool bFresh = true;
        /** Released while inside: the base stays on the body (anchor-bone space). */
        bool bAnchored = false;
        FTransform AnchoredBase = FTransform::Identity;
    };

    void ResolveChannels();
    bool UpdateFrame(FChannel& Channel) const;
    const FGratiaPenetrationChannel* Definition(const FChannel& Channel) const;
    void Release(const TCHAR* Reason);
    /** A free shaft slides over the body surface (joints near an entrance may pass). */
    void CollideWithBody(const GratiaPenetration::FShaft& Shaft, TArray<FVector>& Joints) const;
    /** The tip enters within this distance of the entrance (a larger shaft finds it from further). */
    double CaptureRadius(const FChannel& Channel, const GratiaPenetration::FShaft& Shaft) const;
    /** World offset a wall bone moves to for a shaft inserted to Inserted (cm) at Velocity (cm/s). */
    FVector WallTarget(const FChannel& Channel, const FWallBone& Bone, const GratiaPenetration::FShaft& Shaft, double Inserted, double Velocity) const;
    void UpdateWalls(const GratiaPenetration::FShaft& Shaft, float Delta);
    void PushToAnimation();

    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<AGratiaPenetrator> Penetrator;
    TWeakObjectPtr<const UGratiaCharacterProfile> ResolvedProfile;
    TWeakObjectPtr<const USkeletalMesh> ResolvedMesh;
    TArray<FChannel> Channels;
    FEngagement Engaged;
    TArray<FVector> Shown;
    TArray<FVector> Residual;
    uint64 SolvedFrame = 0;
    bool bWasEnabled = false;
    bool bShaftWasHeld = false;
    bool bShownEngaged = false;
    double NextDiagnosticTime = 0.0;
};
