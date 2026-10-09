#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GratiaPenetrationMath.h"
#include "GratiaPenetrator.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPoseableMeshComponent;
class UProceduralMeshComponent;
class USkeletalMesh;
class UStaticMesh;
class UStaticMeshComponent;

USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaShaftSize
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive", meta = (ClampMin = "4", ClampMax = "60", Units = "cm"))
    float LengthCm = 18.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive", meta = (ClampMin = "0.5", ClampMax = "10", Units = "cm"))
    float RadiusCm = 2.0f;
};

/** Primitive forms the menu cycles (GratiaPenetration::EShaftForm). */
UENUM(BlueprintType)
enum class EGratiaShaftForm : uint8
{
    Smooth, Realistic, Knotted, Beads, Cone, Ribbed, Flared, Tentacle
};

/**
 * Jointed primitive shaft: JointCount joints from the base (actor origin, +X toward the tip) with a
 * handle behind the base for the hand. Free, it is straight; engaged in a character channel
 * (UGratiaPenetration), the joints follow the curve from the hand into the channel. Drawn as a smooth tube
 * with the form's profile along the joints, or with ChainMesh whose ChainBones (base first) follow the joints.
 */
UCLASS()
class GRATIAVR_API AGratiaPenetrator : public AActor
{
    GENERATED_BODY()

public:
    AGratiaPenetrator();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive", meta = (ClampMin = "3", ClampMax = "16"))
    int32 JointCount = 8;
    /** Selectable sizes, small to very large; the menu cycles them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive", meta = (TitleProperty = "Name"))
    TArray<FGratiaShaftSize> Sizes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive")
    int32 SizeIndex = 1;
    /** Profile along the length: smooth, realistic head, knot, beads, cone, ribs, flared head, tentacle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive")
    EGratiaShaftForm Form = EGratiaShaftForm::Smooth;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive", meta = (ClampMin = "0.2", ClampMax = "10", Units = "cm"))
    float TipTaperCm = 2.5f;
    /** Base radius relative to the body radius (above 1: wider toward the base). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive", meta = (ClampMin = "0.5", ClampMax = "2"))
    float BaseRadiusScale = 1.08f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive", meta = (ClampMin = "2", ClampMax = "30", Units = "cm"))
    float HandleLengthCm = 9.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive", meta = (ClampMin = "0.5", ClampMax = "6", Units = "cm"))
    float HandleRadiusCm = 2.2f;
    /** A grip within this gap of the handle or shaft picks the primitive up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive", meta = (ClampMin = "0", ClampMax = "15", Units = "cm"))
    float GrabReachCm = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive")
    FLinearColor ShaftColor = FLinearColor(0.78f, 0.42f, 0.55f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive")
    FLinearColor HandleColor = FLinearColor(0.12f, 0.12f, 0.14f);
    /** Optional skinned look: these bones (base first) are placed on the joints; empty draws basic shapes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive|Mesh")
    TObjectPtr<USkeletalMesh> ChainMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive|Mesh")
    TArray<FName> ChainBones;

    /** Let go inside a channel, the shaft stays on the body (a primitive); false lets it go (the player's hand). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive")
    bool bAnchorWhenReleased = true;
    /** One fixed shape under Label (a hand's fingers, flat hand or fist); the solver re-shapes the joints. */
    void SetShape(FName Label, const GratiaPenetration::FShaft& Shape);
    UFUNCTION(BlueprintCallable, Category = "Primitive")
    void SetSize(int32 Index);
    UFUNCTION(BlueprintCallable, Category = "Primitive")
    void CycleSize();
    UFUNCTION(BlueprintCallable, Category = "Primitive")
    void SetForm(EGratiaShaftForm NewForm);
    UFUNCTION(BlueprintCallable, Category = "Primitive")
    void CycleForm();
    /** Menu name of the form (Russian). */
    UFUNCTION(BlueprintPure, Category = "Primitive")
    FString GetFormLabel() const;
    UFUNCTION(BlueprintPure, Category = "Primitive")
    FString GetSizeLabel() const;
    GratiaPenetration::FShaft GetShaft() const;

    /** Base frame: joint 0, +X along the shaft. The hand or the solver places it. */
    void SetBase(const FTransform& Base);
    FTransform GetBase() const { return FTransform(GetActorQuat(), GetActorLocation()); }
    /** Places the joints (world, base first) and the visuals; a wrong count straightens the shaft. */
    void SetJoints(const TArray<FVector>& World);
    const TArray<FVector>& GetJoints() const { return Joints; }
    /** Gap from Point to the handle and the shaft's first half (negative inside). */
    double GrabGap(const FVector& Point) const;

    void SetHeld(int32 Hand) { HeldHand = Hand; }
    int32 GetHeldHand() const { return HeldHand; }
    bool IsHeld() const { return HeldHand != INDEX_NONE; }
    /** Vibration for the holding hand, written by the solver each frame. */
    float HapticAmplitude = 0.0f;
    float HapticFrequency = 0.0f;

protected:
    virtual void BeginPlay() override;

private:
    void BuildVisuals();
    void UpdateVisuals();

    UPROPERTY(VisibleAnywhere, Category = "Primitive")
    TObjectPtr<USceneComponent> Root;
    /** The tube: Rings rings of Sides vertices from the base to the tip, then the base cap. */
    UPROPERTY(Transient)
    TObjectPtr<UProceduralMeshComponent> Tube;
    bool bTubeBuilt = false;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> Handle;
    UPROPERTY(Transient)
    TObjectPtr<UPoseableMeshComponent> Poseable;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> ShaftMaterial;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> HandleMaterial;
    UPROPERTY()
    TObjectPtr<UStaticMesh> CylinderMesh;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> BasicMaterial;
    TArray<FVector> Joints;
    int32 HeldHand = INDEX_NONE;
};
