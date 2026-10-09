#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaChannelShots.generated.h"

class AGratiaPenetrator;
class ACameraActor;
class APlayerController;

/**
 * Channel shots (desktop, studio). -GratiaChannelShots: for every channel of the character's profile runs a plan of
 * cases (empty, three fingers, flat hand, fist, two hands side by side, an XXL primitive, the largest primitive deep with
 * the belly before and after, XL beads, an L knot at the entrance, the gape after an XXL leaves at 0.3, 3 and 10 s, a
 * small shaft after a large one): holds the stand-in shafts at the entrance until captured, pushes them in against the
 * resistance, lets the walls settle and captures the entrance along the axis, from the side and from behind into
 * Saved/Screenshots/ChannelShots; last, every primitive form side by side; then quits.
 * -GratiaChannelGym: the same, then it stays running and watches Saved/ChannelGym/gym.json: on a change it applies the
 * overrides there to the loaded profile (penetration settings, channel fields, outer-ring bones), runs a Live Coding
 * patch when asked, and shoots the requested cases again (report.txt and done.txt next to gym.json). See
 * docs/CHANNEL_GYM.md.
 */
UCLASS()
class GRATIAVR_API UGratiaChannelShots : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaChannelShots();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    /** What a shot does first, then where the camera looks and how long the scene settles. */
    enum class EAction : uint8 { Arrange, ArrangeHeld, Push, PullOut, Swap, Hold };
    enum class ECamera : uint8 { Axis, Side, Behind, Belly };
    struct FShot
    {
        int32 Channel = 0;
        int32 Case = 0;
        FString Label;
        EAction Action = EAction::Hold;
        ECamera Camera = ECamera::Axis;
        float Wait = 0.5f;
    };
    UPROPERTY(Transient) TArray<TObjectPtr<AGratiaPenetrator>> Shafts;
    UPROPERTY(Transient) TObjectPtr<ACameraActor> View;
    TArray<FShot> Plan;
    int32 Step = INDEX_NONE;
    float Seconds = 0.0f;
    float Warmup = 0.0f;
    /** Seconds since every shaft of the shot was captured (-1: still approaching). */
    float Inserting = -1.0f;
    /** Depth of the hands' tips (cm; the shafts may lag behind) and how long the shafts have been at their depth. */
    double HandDepth = -1.0;
    float Settled = 0.0f;
    /** The hand pushes the shafts in (false: held at the entrance); the shafts are the swapped small ones. */
    bool bPush = false;
    bool bSwapped = false;
    bool bFormsShot = false;
    /** Gym: keep running and watch gym.json. */
    bool bLive = false;
    bool bWaiting = false;
    FDateTime GymStamp;
    float PollSeconds = 0.0f;
    int32 Pass = 0;
    FString Serial;
    TArray<FString> CaseFilter, ChannelFilter;
    TArray<FString> Report;

    static FString CaseName(int32 Case);
    void BuildPlan(int32 Channels);
    bool Arrange(int32 Channel, int32 Case, bool bSmall);
    void ClearShafts();
    double TargetDepth(int32 Case, double ChannelDepth) const;
    /** Last shot of a full plan: every primitive form side by side (Primitive_Forms.png). */
    bool ShootForms(APlayerController* Player);
    /** Gym: reads gym.json when it changed; returns true when a new pass should start. */
    bool PollGym();
    void FinishPass();
};
