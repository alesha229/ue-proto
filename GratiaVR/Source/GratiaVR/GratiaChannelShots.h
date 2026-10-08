#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaChannelShots.generated.h"

class AGratiaPenetrator;
class ACameraActor;

/**
 * -GratiaChannelShots (desktop, studio): for every channel of the character's profile and every case (empty,
 * three fingers, flat hand, fist, two hands of three fingers side by side, the largest primitive) puts visible
 * stand-in shafts into the channel, lets the walls settle and captures the entrance along the channel axis and
 * from the side into Saved/Screenshots/ChannelShots, then quits. Layout and deformation review without a headset.
 */
UCLASS()
class GRATIAVR_API UGratiaChannelShots : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaChannelShots();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TArray<TObjectPtr<AGratiaPenetrator>> Shafts;
    UPROPERTY(Transient) TObjectPtr<ACameraActor> View;
    int32 Step = 0;
    float Seconds = 0.0f;
    bool Arrange(int32 Channel, int32 Case);
    FString CaseName(int32 Case) const;
};
