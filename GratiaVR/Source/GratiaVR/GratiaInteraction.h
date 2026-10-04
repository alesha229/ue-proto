#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaCharacterProfile.h"
#include "GratiaContactSolver.h"
#include "GratiaInteraction.generated.h"
class AGratiaPreviewCharacter;

enum class EGratiaContactState : uint8 { Idle, Hovered, Touched, Held, Cooldown };
struct FGratiaContactZone
{
    FName Name;
    FName Bone;
    FVector Offset = FVector::ZeroVector;
    float Radius = 0.0f;
    bool bCanHold = true;
    bool bSceneActor = false;
    int32 Priority = 0;
    FGratiaContactSettings Settings;
    EGratiaContactState State = EGratiaContactState::Idle;
    int32 Hand = INDEX_NONE;
    float Elapsed = 0.0f;
    int32 Reactions = 0;
    void Step(bool bLeft, bool bRight, bool bHover, float Delta);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FGratiaContactReactionEvent, FName, ZoneName, int32, HandIndex, float, HandSpeed, int32, Mood);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGratiaContactResetEvent);

/** Model-independent contact state, bone-following collision and bounded response controls. */
UCLASS()
class GRATIAVR_API UGratiaInteraction : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaInteraction();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
    void SetHandSample(bool bLeft, const FTransform& Raw, const FTransform& Visual, bool bAllowed);
    bool IsHandSampleReady(bool bLeft) const { return Hands[bLeft ? 0 : 1].bAllowed; }
    FTransform ConstrainHand(const FTransform& From, const FTransform& Target, bool bLeft = false) const;
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void RebuildProfileZones();
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void SetSceneContactActor(AActor* Actor);
    UFUNCTION(BlueprintPure, Category = "Interaction|Diagnostics")
    FString GetContactDiagnostics() const;
    UFUNCTION(BlueprintPure, Category = "Interaction")
    FVector GetZoneWorldPosition(int32 ZoneIndex) const;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
    TObjectPtr<AActor> SceneContactActor;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Diagnostics")
    bool bShowContactDebug = false;
    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FGratiaContactReactionEvent OnContactReaction;
    /** Presentation listeners clear pending output after reset, prop rebinding or profile replacement. */
    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FGratiaContactResetEvent OnContactReset;
    void ResetState();
    bool RunChecks(FString& Failure);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Response")
    FVector LookTarget = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Response")
    float Reaction = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Response")
    float Impulse = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Response")
    int32 ReactionSerial = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Response")
    FName LastReactionZoneName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Settings", meta = (ClampMin = "0", ClampMax = "2"))
    int32 Mood = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Settings", meta = (ClampMin = "0", ClampMax = "2"))
    int32 Quality = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Settings")
    bool bDemo = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Settings")
    bool bBodyMotion = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Settings")
    bool bHairMotion = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Settings")
    bool bEarMotion = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Settings")
    bool bClothMotion = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Settings")
    bool bLocalSpring = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Settings")
    bool bPhysicalMotion = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Settings")
    bool bSound = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Response")
    int32 ActiveZone = INDEX_NONE;
    TArray<FGratiaContactZone> Zones;
protected:
    virtual void BeginPlay() override;
private:
    struct FHandSample
    {
        FTransform Raw, Visual;
        FVector Last = FVector::ZeroVector;
        float Speed = 0.0f;
        bool bAllowed = false;
        bool bWasAllowed = false;
        FName GateReason = TEXT("No sample");
        FName NearestZone;
        double NearestGapCm = 0.0;
    };
    FHandSample Hands[2];
    mutable FGratiaContactSolveResult LastConstraint[2];
    mutable double ContactRecoveryUntil[2] = { 0.0, 0.0 };
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<UGratiaCharacterProfile> ActiveProfile;
    FGratiaContactSettings ContactSettings;
    float DemoSeconds = 0.0f;
    float ReactionSeconds = 0.0f;
    double LastResponseTime = -100.0;
    double NextDiagnosticTime = 0.0;
    FString LastDiagnostic;
    FVector ZonePosition(const FGratiaContactZone& Zone) const;
    bool ZoneGap(const FGratiaContactZone& Zone, const FVector& Point, double& Gap) const;
    double CharacterScale() const;
    bool ResolveProxyPoint(FName Semantic, const FVector& Offset, FVector& Point) const;
    void GatherCollisionShapes(TArray<FGratiaContactShape>& Shapes) const;
    bool SceneBounds(FVector& Center, FVector& Extent) const;
    void React(int32 ZoneIndex);
};
