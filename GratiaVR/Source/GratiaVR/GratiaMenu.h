#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaMenu.generated.h"
class UInputAction;
class UInputMappingContext;
class AGratiaPreviewCharacter;
class APlayerController;
class UEnhancedInputComponent;
class UWidgetComponent;
class UWidgetInteractionComponent;
class UStaticMeshComponent;
class UGratiaMenuWidget;
class UGratiaSceneDirector;
class UGratiaSceneLibrary;
enum class EGratiaMenuAction : uint8;
UCLASS()
class GRATIAVR_API UGratiaMenu : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaMenu();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
    void Toggle();
    void Close();
    void OpenLobby();
    void Execute(EGratiaMenuAction Action, int32 Param = 0);
    FString LabelFor(EGratiaMenuAction Action, int32 Param = 0) const;
    bool IsSelected(EGratiaMenuAction Action, int32 Param = 0) const;
    bool IsActionAvailable(EGratiaMenuAction Action, int32 Param = 0) const;
    UGratiaSceneDirector* GetDirector() const;
    UGratiaMenuWidget* GetWidget() const { return Widget; }
    FString GetDiagnostics() const;
    bool IsInScene() const;
    int32 GetCurrentScene() const;
    void ApplyQuality(int32 Profile, bool bResetMotion = true);
    void ApplySelected();
    bool RunChecks();
    void SetCharacter(AGratiaPreviewCharacter* Value);
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Menu") bool bOpen = false;
    int32 Selected = 0;
    FString QualityLabel() const;
    const TCHAR* ViewLabel() const;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu", meta=(ClampMin="80", ClampMax="400", Units="cm")) float PanelDistanceCm = 165.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu", meta=(ClampMin="0.04", ClampMax="0.3")) float PanelScale = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu") FVector2D PanelResolution = FVector2D(1600, 900);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu|Pointer", meta=(ClampMin="100", ClampMax="1000", Units="cm")) float PointerDistanceCm = 500.0f;
    /** Aim pose is preferred. This rotation is used only by a grip-pose fallback. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu|Pointer") FRotator GripPointerRotation = FRotator::ZeroRotator;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY() TObjectPtr<UInputAction> ToggleAction;
    UPROPERTY() TObjectPtr<UInputAction> NextAction;
    UPROPERTY() TObjectPtr<UInputAction> ApplyAction;
    UPROPERTY() TObjectPtr<UInputMappingContext> MenuMapping;
    /** Per-instance input additions; the shared mapping asset is never changed in play. */
    UPROPERTY() TObjectPtr<UEnhancedInputComponent> ActionInput;
    UPROPERTY(Transient) TObjectPtr<UWidgetComponent> Panel;
    UPROPERTY(Transient) TObjectPtr<UWidgetInteractionComponent> Pointer;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Laser;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Dot;
    UPROPERTY(Transient) TObjectPtr<UGratiaMenuWidget> Widget;
    TWeakObjectPtr<UGratiaSceneLibrary> BuiltLibrary;
    TWeakObjectPtr<APlayerController> BoundController;
    void BindInput(APlayerController* Controller);
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    bool bToggleArmed = true, bNextArmed = true, bApplyArmed = true;
    bool bPointerDown = false, bMouseCursorBefore = false;
    float RefreshSeconds = 0.0f;
    bool EnsureWidget(APlayerController* Controller);
    bool Open(bool bLobby);
    void SetOpen(bool bValue);
    void UpdatePointer(float Delta, APlayerController* Controller);
    void ReleasePointer();
    void Refresh();
};
