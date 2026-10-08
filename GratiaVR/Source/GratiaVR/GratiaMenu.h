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
class UCameraComponent;
class USoundBase;
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
    /** Value shown next to a stepper or on a cycle button ("80 %", "+2 см", "игривая"). */
    FString ValueFor(EGratiaMenuAction Action) const;
    /** Pointer feedback: a light haptic tick on hover, a stronger one and a click sound on press (VR only for haptics). */
    void PointerFeedback(bool bClick);
    /**
     * Where the panel opens for this head: PanelDistanceCm ahead along the head's yaw, PanelBelowEyeCm below the eyes,
     * never lower than PanelFloorClearanceCm above the floor or furniture under it (a crouching or seated player
     * would otherwise see it sink into the floor), turned to face the eyes.
     */
    FTransform ComputePanelTransform(const UCameraComponent* Camera) const;
    UWidgetComponent* GetPanel() const { return Panel; }
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu", meta=(Units="cm")) float PanelBelowEyeCm = 10.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu", meta=(ClampMin="0", Units="cm")) float PanelFloorClearanceCm = 12.0f;
    /** The open panel glides back in front when the head turns further away than this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu", meta=(ClampMin="20", ClampMax="180")) float FollowAngleDegrees = 60.0f;
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
    UPROPERTY() TObjectPtr<USoundBase> ClickSound;
    UPROPERTY() TObjectPtr<USoundBase> OpenSound;
    UPROPERTY() TObjectPtr<USoundBase> CloseSound;
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
    float HapticSeconds = 0.0f;
    /** -GratiaMenuShots: captures every page (Saved/Screenshots/MenuShots) and quits; layout review without a headset. */
    int32 ShotStep = INDEX_NONE;
    float ShotSeconds = 0.0f;
    void TickShots(float Delta);
    /** Lazy follow: the panel glides to FollowTarget while bFollowing. */
    bool bFollowing = false;
    FTransform FollowTarget;
    void UpdateFollow(float Delta);
    bool EnsureWidget(APlayerController* Controller);
    bool Open(bool bLobby);
    void SetOpen(bool bValue);
    void UpdatePointer(float Delta, APlayerController* Controller);
    void ReleasePointer();
    void Refresh();
};
