#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaPlayMath.h"
#include "GratiaPlayPose.h"
#include "GratiaPlayBody.generated.h"

class AGratiaPreviewCharacter;
class UGratiaPlaySettings;
class UPhysicalAnimationComponent;

/**
 * Body side of the interaction layer, written into the character's anim proxy each frame:
 *  - limb grabs: grip near a wrist/ankle takes it; the limb follows the controller by two-bone IK, pulls the chest
 *    or pelvis when stretched beyond reach, the other feet stay planted;
 *  - ground: hands, feet, pelvis, chest and head stay above the floor/bed surface under them;
 *  - press: Physical Animation bodies near a hand get a softer drive and more physics blend, then ease back;
 *  - anime soft: KawaiiPhysics chains get exaggerated inertia (multipliers + an inertial kick);
 *  - garments' fabric offsets (UGratiaGarments) are passed through as late offsets.
 * Needs the character's UGratiaAnimInstance; other AnimationClasses keep everything off and are reported.
 */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaPlayBody : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaPlayBody();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play Body") bool bLimbGrabs = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play Body") bool bGround = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play Body") bool bPressSoftening = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play Body") bool bAnimeSoft = true;

    /** Limb held by this hand (definition name) or None. */
    UFUNCTION(BlueprintPure, Category = "Play Body") FName GetHeldLimb(bool bLeft) const;
    UFUNCTION(BlueprintCallable, Category = "Play Body") void ReleaseAll();
    FString GetDiagnostics() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    struct FLimb
    {
        FName Root, Mid, End, Pull;
        int32 Hand = INDEX_NONE;
        float Weight = 0.0f;
        /** Grab point of the limb in the hand's space at the moment of the grab. */
        FVector HandLocal = FVector::ZeroVector;
        FVector Goal = FVector::ZeroVector;
        FVector GoalVelocity = FVector::ZeroVector;
        bool bResolved = false;
    };
    struct FGround
    {
        FName Bone, Root, Mid;
        float Lift = 0.0f;
        bool bResolved = false;
    };
    struct FPress
    {
        float Amount = 0.0f;
        float Written = 0.0f;
    };

    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<const UGratiaPlaySettings> CachedSettings;
    TWeakObjectPtr<const UObject> CachedProfile;
    TWeakObjectPtr<UPhysicalAnimationComponent> Driver;
    TArray<FLimb> Limbs;
    TArray<FGround> Grounds;
    TMap<FName, FPress> Presses;
    float PrevGrip[2] = { 0.0f, 0.0f };
    FVector PelvisVelocity = FVector::ZeroVector;
    FVector PelvisPrev = FVector::ZeroVector;
    FVector PelvisAccel = FVector::ZeroVector;
    bool bPelvisValid = false;
    bool bAnimReported = false;
    int32 LastBodyBones = 0;
    FGratiaPlayPoseInput Pose;
    /** Body pull applied last frame per bone (world cm): taken out when reading the animated pose. */
    TMap<FName, FVector> LastPulls;

    void Resolve(const UGratiaPlaySettings& Settings);
    void UpdateLimbs(const UGratiaPlaySettings& Settings, float Delta);
    void UpdateGround(const UGratiaPlaySettings& Settings, float Delta);
    void UpdatePress(const UGratiaPlaySettings& Settings, float Delta);
    void UpdateSoft(const UGratiaPlaySettings& Settings, float Delta);
    void RestorePress();
    void AddBodyOffset(FName Bone, const FVector& OffsetCS);
    bool TraceSurface(const FVector& Point, float Above, float Below, double& OutZ) const;
};
