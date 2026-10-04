#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaInteraction.generated.h"
class AGratiaPreviewCharacter;
class UTextRenderComponent;

enum class EGratiaContactState : uint8 { Idle, Hovered, Touched, Held, Cooldown };
struct FGratiaContactZone
{
    FName Name;
    FName Bone;
    FVector Offset = FVector::ZeroVector;
    float Radius = 8.0f;
    bool bCanHold = true;
    int32 Priority = 0;
    EGratiaContactState State = EGratiaContactState::Idle;
    int32 Hand = INDEX_NONE;
    float Elapsed = 0.0f;
    int32 Reactions = 0;
    void Step(bool bLeft, bool bRight, bool bHover, float Delta);
};

/** Bone-following contact zones and a bounded reaction controller. */
UCLASS()
class GRATIAVR_API UGratiaInteraction : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaInteraction();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
    void SetHandSample(bool bLeft, const FTransform& Raw, const FTransform& Visual, bool bAllowed);
    FTransform ConstrainHand(const FTransform& From, const FTransform& Target) const;
    void ResetState();
    bool RunChecks(FString& Failure);
    FVector LookTarget = FVector::ZeroVector;
    float Reaction = 0.0f;
    float Impulse = 0.0f;
    uint32 ReactionSerial = 0;
    int32 Mood = 0;
    int32 Quality = 1;
    bool bDemo = false;
    bool bBodyMotion = true;
    bool bHairMotion = true;
    bool bEarMotion = true;
    bool bClothMotion = true;
    bool bLocalSpring = true;
    bool bPhysicalMotion = true;
    bool bSound = true;
    int32 ActiveZone = INDEX_NONE;
    TArray<FGratiaContactZone> Zones;
protected:
    virtual void BeginPlay() override;
private:
    struct FHandSample { FTransform Raw, Visual; FVector Last = FVector::ZeroVector; float Speed = 0.0f; bool bAllowed = false; };
    FHandSample Hands[2];
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    UPROPERTY() TObjectPtr<UTextRenderComponent> Caption;
    float CaptionSeconds = 0.0f;
    float DemoSeconds = 0.0f;
    float ReactionSeconds = 0.0f;
    float LastChime = -10.0f;
    FVector ZonePosition(const FGratiaContactZone& Zone) const;
    void React(int32 ZoneIndex);
    void MakeZones();
};
