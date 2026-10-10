#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GratiaPlaySettings.h"
#include "GratiaPlayProp.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class AGratiaPreviewCharacter;

/**
 * A prop the hands pick up with the grip and use on the character (spec 4: oils, toys, accessories).
 * Its working tip stimulates the touched zone (arousal, reactions), a toy buzzes the holding hand, oil raises
 * the wet/oil gloss, an accessory released near its bone attaches there. Dropped props fall with physics.
 */
UCLASS()
class GRATIAVR_API AGratiaPlayProp : public AActor
{
    GENERATED_BODY()
public:
    AGratiaPlayProp();
    virtual void Tick(float DeltaSeconds) override;
    void Setup(const FGratiaPropDefinition& InDefinition, AGratiaPreviewCharacter* InCharacter);
    const FGratiaPropDefinition& GetDefinition() const { return Definition; }
    bool IsHeld() const { return Hand != INDEX_NONE; }
    /** Toy: 0 off, 1 low, 2 pulse, 3 high (trigger cycles). */
    int32 GetMode() const { return Mode; }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop")
    TObjectPtr<UStaticMeshComponent> Mesh;

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    FGratiaPropDefinition Definition;
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
    int32 Hand = INDEX_NONE;
    FTransform HoldRelative = FTransform::Identity;
    FVector Home = FVector::ZeroVector;
    FVector LastTip = FVector::ZeroVector;
    float PrevGrip[2] = { 0.0f, 0.0f };
    float PrevTrigger = 0.0f;
    int32 Mode = 0;
    int32 TouchedZone = INDEX_NONE;
    float Oil = 0.0f;
    double Age = 0.0;
    bool bAttached = false;
    bool bDropped = false;
    void Grab(int32 HandIndex, const FTransform& HandWorld);
    void Release(const FVector& Velocity);
    void UseOnBody(float Delta);
};
