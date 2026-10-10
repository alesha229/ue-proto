#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaPlayMath.h"
#include "GratiaGarments.generated.h"

class AGratiaPreviewCharacter;
class UGratiaPlaySettings;
struct FGratiaPlayBoneOffset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGratiaGarmentEvent, FName, Piece, float, Progress);

/**
 * Interactive clothing: the hands push straps, lift the skirt, pinch buttons, pull zippers and ties.
 * Each piece's progress (0 on .. 1 undone) drives its morphs, a material parameter and fabric bones, and stays
 * where the hand left it. Pieces open only from their arousal stage and after the pieces they require.
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaGarments : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaGarments();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

    UFUNCTION(BlueprintPure, Category = "Garments") float GetProgress(FName Piece) const;
    UFUNCTION(BlueprintPure, Category = "Garments") bool IsUndone(FName Piece) const;
    UFUNCTION(BlueprintCallable, Category = "Garments") void ResetGarments();
    /** Sets a piece directly (menu/customisation); 1 completes it. */
    UFUNCTION(BlueprintCallable, Category = "Garments") void SetProgress(FName Piece, float Progress);
    /** A piece was undone (Progress 1) or put back (0). */
    UPROPERTY(BlueprintAssignable, Category = "Garments") FGratiaGarmentEvent OnGarmentChanged;
    /** The hand tried a piece she does not allow yet (stage or order). */
    UPROPERTY(BlueprintAssignable, Category = "Garments") FGratiaGarmentEvent OnGarmentRefused;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garments") bool bEnabled = true;

    /** Fabric bones that keep where the hand left them (component space), for the anim proxy. */
    void AppendLateOffsets(TArray<FGratiaPlayBoneOffset>& Out) const;
    FString GetDiagnostics() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    struct FPiece
    {
        GratiaPlay::FGarmentState State;
        int32 Hand = INDEX_NONE;
        int32 AnchorHand = INDEX_NONE;
        FName HandleBone, AnchorBone;
        TArray<FName> Morphs;
        TArray<TPair<FName, float>> FollowBones;
        FVector GrabHand = FVector::ZeroVector;
        double GrabSeparation = 0.0;
        float Written = -1.0f;
        float LastTick = 0.0f;
        bool bWasComplete = false;
    };
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<const UGratiaPlaySettings> CachedSettings;
    TWeakObjectPtr<const UObject> CachedProfile;
    TArray<FPiece> Pieces;
    float PrevPinch[2] = { 0.0f, 0.0f };
    double RefusedTime[2] = { -10.0, -10.0 };
    bool bReported = false;

    void Resolve(const UGratiaPlaySettings& Settings);
    bool IsAvailable(const UGratiaPlaySettings& Settings, int32 Index) const;
    FVector PointOn(FName Bone, const FVector& Offset) const;
    FVector AxisOf(FName Bone, const FVector& Axis) const;
    void Apply(const UGratiaPlaySettings& Settings, int32 Index, bool bForce);
    void ReleaseHands(FPiece& Piece);
};
