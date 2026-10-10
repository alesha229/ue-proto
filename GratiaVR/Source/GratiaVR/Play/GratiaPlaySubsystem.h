#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GratiaPlaySubsystem.generated.h"

class AGratiaStage1Runtime;
class AGratiaPreviewCharacter;
class AGratiaPlayProp;
class UGratiaPlaySettings;

/** Which interaction owns a hand's grip this frame (one grip drives one thing). */
enum class EGratiaHandUse : uint8 { None, Limb, Garment, Prop };

/**
 * Glue of the interaction layer (spec sections 4/5). It binds to the stage runtime's explicit TargetCharacter,
 * adds the layer's components to it (no edits of the character class), reads the tracked controllers once per
 * frame for all of them, arbitrates which system a grip belongs to and spawns the props.
 */
UCLASS()
class GRATIAVR_API UGratiaPlaySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    struct FHand
    {
        FTransform World = FTransform::Identity;
        FVector Velocity = FVector::ZeroVector;
        float Speed = 0.0f;
        float Grip = 0.0f;
        float Trigger = 0.0f;
        /** Tracked, past recovery, scene interaction allowed (the runtime's gate). */
        bool bAllowed = false;
        /** Distance travelled while allowed (haptic grain, foley). */
        double Travel = 0.0;
        /** Nearest contact zone of the target character within reach (INDEX_NONE: none) and its gap, cm. */
        int32 Zone = INDEX_NONE;
        FName ZoneName;
        float ZoneGap = 1.0e6f;
    };

    static UGratiaPlaySubsystem* Get(const UObject* WorldContext);
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;

    /** Controllers of this frame (read on first use each frame, so every component sees the same sample). */
    const FHand& GetHand(bool bLeft);
    AGratiaStage1Runtime* GetRuntime() const { return Runtime.Get(); }
    AGratiaPreviewCharacter* GetCharacter() const;
    /** The character's profile settings, or the defaults when its profile names none. */
    static const UGratiaPlaySettings* GetSettings(const AGratiaPreviewCharacter* Character);
    /** Player's head (camera) in world space; false without a player. */
    bool GetHead(FVector& Out) const;

    bool ClaimHand(bool bLeft, EGratiaHandUse Use, const UObject* Owner);
    void ReleaseHand(bool bLeft, const UObject* Owner);
    EGratiaHandUse GetHandUse(bool bLeft) const { return HandUse[bLeft ? 0 : 1]; }
    bool IsHandOwnedBy(bool bLeft, const UObject* Owner) const { return HandOwner[bLeft ? 0 : 1].Get() == Owner; }

    /** Shows the settings' props on a tray in front of the character, or removes them. */
    void SetPropsShown(bool bShown);
    bool ArePropsShown() const { return !Props.IsEmpty(); }
    const TArray<TWeakObjectPtr<AGratiaPlayProp>>& GetProps() const { return Props; }
    FString GetDiagnostics() const;
    static bool IsLayerEnabled();

private:
    TWeakObjectPtr<AGratiaStage1Runtime> Runtime;
    TWeakObjectPtr<AGratiaPreviewCharacter> BoundCharacter;
    TArray<TWeakObjectPtr<AGratiaPlayProp>> Props;
    FHand Hands[2];
    FVector LastPosition[2] = { FVector::ZeroVector, FVector::ZeroVector };
    bool bHadPosition[2] = { false, false };
    uint64 HandsFrame = MAX_uint64;
    double LastHandsTime = 0.0;
    EGratiaHandUse HandUse[2] = { EGratiaHandUse::None, EGratiaHandUse::None };
    TWeakObjectPtr<const UObject> HandOwner[2];
    float BindSeconds = 0.0f;
    void Bind();
    void RefreshHands();
};
