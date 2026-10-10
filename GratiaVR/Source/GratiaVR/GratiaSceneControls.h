#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GratiaSceneControls.generated.h"

class STextBlock;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UWidgetComponent;

/**
 * Physical buttons beside the player for in-scene changes (touch a ball with a controller):
 * light mood, mirror, pose, outfit slots and character archetype. The panel follows the player
 * at the left hip and hides in the lobby, during loading and scripted performances.
 */
UCLASS()
class GRATIAVR_API AGratiaSceneControls : public AActor
{
    GENERATED_BODY()
public:
    AGratiaSceneControls();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    /** Button ball radius; a controller within this plus the touch margin presses it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (ClampMin = "1", Units = "cm"))
    float ButtonRadiusCm = 3.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (ClampMin = "0", Units = "cm"))
    float TouchMarginCm = 3.0f;

private:
    enum class EAction : uint8 { Light, Mirror, Pose, Archetype, Outfit };
    struct FButton
    {
        EAction Action = EAction::Light;
        int32 Slot = 0;
        TObjectPtr<UStaticMeshComponent> Ball;
        TObjectPtr<UWidgetComponent> Label;
        TSharedPtr<STextBlock> Text;
        TObjectPtr<UMaterialInstanceDynamic> Material;
        FLinearColor Color = FLinearColor::White;
        bool bInside[2] = {false, false};
        float Flash = 0.0f;
    };
    void BuildButtons();
    void AddButton(EAction Action, int32 Slot, const FLinearColor& Color);
    void Press(FButton& Button, bool bLeft);
    FText LabelFor(const FButton& Button) const;
    void Follow(bool bSnap, float DeltaSeconds);
    bool ShouldShow() const;

    UPROPERTY(VisibleAnywhere, Category = "Controls") TObjectPtr<USceneComponent> Root;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Balls;
    UPROPERTY(Transient) TArray<TObjectPtr<UWidgetComponent>> Labels;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    TArray<FButton> Buttons;
    int32 BuiltSlots = INDEX_NONE;
    float Cooldown = 0.0f;
    float LabelRefresh = 0.0f;
    bool bPlaced = false;
};
