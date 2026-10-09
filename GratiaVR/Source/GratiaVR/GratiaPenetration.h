#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaCharacterProfile.h"
#include "GratiaPenetrationMath.h"
#include "GratiaPenetration.generated.h"

class AGratiaPreviewCharacter;
class AGratiaPenetrator;

/**
 * Character side of penetration. Resolves the profile channels on the current pose, captures jointed shafts at
 * the entrances - the selected primitive and the player's hands (three fingers, a flat hand or a fist) - lays
 * their joints along the curve from the hand into the channel, opens and drags the wall bones (UGratiaAnimInstance
 * applies the offsets after the soft body), drives the opening morph, and reports reactions and haptics. Both hands
 * and the primitive can be inside at once, in different channels or two in one channel (side by side, the walls
 * open for both). The shafts are solved right after the hands moved (Solve from the runtime); otherwise once per tick.
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaPenetration : public UActorComponent
{
    GENERATED_BODY()

public:
    UGratiaPenetration();

    /** Explicit selection: the primitive this character responds to (nullptr clears it). */
    UFUNCTION(BlueprintCallable, Category = "Penetration")
    void SetPenetrator(AGratiaPenetrator* Shaft);
    AGratiaPenetrator* GetPenetrator() const { return Penetrator.Get(); }
    /** Further shafts that may enter besides the primitive: the player's hands. */
    void SetCandidates(const TArray<AGratiaPenetrator*>& Shafts);
    /** Shafts inside a channel now. */
    int32 GetEngagementCount() const { return Engagements.Num(); }
    /** The first shaft inside a channel (nullptr when none). */
    AGratiaPenetrator* GetEngagedShaft() const { return Engagements.IsEmpty() ? nullptr : Engagements[0].Shaft.Get(); }
    bool IsEngaged(const AGratiaPenetrator* Shaft) const { return FindEngagement(Shaft) != nullptr; }
    /** Shafts one channel takes at once (side by side). */
    static constexpr int32 MaxShaftsPerChannel = 2;
    /** For a shaft inside: its entrance (shifted aside when it shares the channel), inward axis (world), tip depth past
     *  the entrance and channel depth (cm). */
    bool GetEngagedFrame(const AGratiaPenetrator* Shaft, FVector& Entrance, FVector& Inward, double& Inserted, double& Depth) const;
    /** Distance from Point to the nearest channel entrance this frame (a large value when there is none). */
    double GetEntranceGap(const FVector& Point) const;
    /** Channel Index this frame: name, entrance and inward axis (world) and depth (cm); false without a frame. */
    bool GetChannelFrame(int32 Index, FName& Name, FVector& Entrance, FVector& Inward, double& Depth) const;
    /** Releases every shaft and returns every wall bone to its pose. */
    UFUNCTION(BlueprintCallable, Category = "Penetration")
    void ResetPenetration();
    void Solve(float Delta);

    bool IsEnabled() const;
    int32 GetChannelCount() const { return Channels.Num(); }
    /** The first engagement's channel, depth and opening (diagnostics). */
    FName GetEngagedChannel() const { return Engagements.IsEmpty() ? NAME_None : Channels[Engagements[0].Channel].Name; }
    float GetDepthCm() const { return Engagements.IsEmpty() ? 0.0f : float(FMath::Max(0.0, Engagements[0].Inserted)); }
    float GetOpeningCm() const { return Engagements.IsEmpty() ? 0.0f : float(Engagements[0].Opening); }
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
        /** Rest distance from the channel axis (component cm): the bone only moves as far as the shaft surface
         *  reaches past it, so lips and the slit's ends far from the axis stay put for a thin shaft. */
        double RestDistance = 0.0;
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
        // Dynamics: the widest opening since the channel last closed, how long it has been open, how long since
        // the opening last grew, the current contraction (1 at its start, 0 when over) and when the next one comes.
        double Peak = 0.0;
        double OpenSeconds = 0.0;
        double SinceWidest = 0.0;
        double Clench = 0.0;
        double NextClench = 0.0;
        float MorphWeight = 0.0f;
        /** Last value sent to the stretch material parameter. */
        float StretchSent = 0.0f;
        int32 Inside = 0;
        /** Swellings along the channel (morphs present on the mesh), their depths (component cm) and weights. */
        TArray<FName> BulgeMorphs;
        TArray<double> BulgeDepths;
        TArray<float> BulgeWeights;
    };
    struct FEngagement
    {
        int32 Channel = INDEX_NONE;
        TWeakObjectPtr<AGratiaPenetrator> Shaft;
        double Inserted = 0.0;
        double Velocity = 0.0;
        double Travel = 0.0;
        double Opening = 0.0;
        double Pulse = 0.0;
        /** First solve after the capture: no speed or travel yet. */
        bool bFresh = true;
        bool bShaftWasHeld = true;
        /** Sideways shift (world) when two shafts share the channel; zero alone. */
        FVector Lateral = FVector::ZeroVector;
        /** 0..1 how hard the hand pushes against a tight place (the shaft lags behind it). */
        double Strain = 0.0;
        /** Hand lead over the shaft (cm, negative pulling back): the walls are dragged with it. */
        double Push = 0.0;
        /** The shaft did not move last frame (static friction holds it). */
        bool bStuck = true;
        /** Released while inside: the base stays on the body (anchor-bone space). */
        bool bAnchored = false;
        FTransform AnchoredBase = FTransform::Identity;
    };

    void ResolveChannels();
    bool UpdateFrame(FChannel& Channel) const;
    const FGratiaPenetrationChannel* Definition(const FChannel& Channel) const;
    /** Ends one engagement (by index) or every one. */
    void Release(int32 Index, const TCHAR* Reason);
    void ReleaseAll(const TCHAR* Reason);
    const FEngagement* FindEngagement(const AGratiaPenetrator* Shaft) const;
    /** A free shaft slides over the body surface (joints near an entrance may pass). */
    void CollideWithBody(const GratiaPenetration::FShaft& Shaft, TArray<FVector>& Joints) const;
    /** The tip enters within this distance of the entrance (a larger shaft finds it from further). */
    double CaptureRadius(const FChannel& Channel, const GratiaPenetration::FShaft& Shaft) const;
    /** World offset a wall bone moves to for a shaft inserted to Inserted (cm) at Velocity (cm/s). */
    FVector WallTarget(const FChannel& Channel, const FWallBone& Bone, const GratiaPenetration::FShaft& Shaft, double Inserted, double Velocity,
        double Push = 0.0) const;
    /** Push of the rings against the shaft with its tip at Inserted (cm of hand lead; positive resists going in) and the
     *  walls' rubbing along it. */
    void RingForces(const FChannel& Channel, const GratiaPenetration::FShaft& Shaft, double Inserted, double& Ring, double& Friction) const;
    /** Walls and morph: open with the shafts at once, after narrowing hold, then close slowly (slower after a wide
     *  and long opening), and clench now and then while a shaft is inside. */
    void UpdateWalls(float Delta);
    /** A contraction of Channel now: the walls squeeze in for ClenchSeconds; the shafts inside get a pulse. */
    void Clench(int32 Channel);
    void PushToAnimation();
    /** Index of a free channel Shaft's tip can enter now, or INDEX_NONE. */
    int32 FindCapture(const AGratiaPenetrator& Shaft) const;
    /** Every shaft the character responds to: the primitive first, then the hands. */
    TArray<AGratiaPenetrator*> GetShafts() const;

    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<AGratiaPenetrator> Penetrator;
    TArray<TWeakObjectPtr<AGratiaPenetrator>> Candidates;
    TWeakObjectPtr<const UGratiaCharacterProfile> ResolvedProfile;
    TWeakObjectPtr<const USkeletalMesh> ResolvedMesh;
    TArray<FChannel> Channels;
    /** At most one per channel and one per shaft. */
    TArray<FEngagement> Engagements;
    /** The drawn primitive's joints and the easing residual of its last shape switch. */
    TArray<FVector> Shown;
    TArray<FVector> Residual;
    uint64 SolvedFrame = 0;
    bool bWasEnabled = false;
    bool bShownEngaged = false;
    double NextDiagnosticTime = 0.0;
};
