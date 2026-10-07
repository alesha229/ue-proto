#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GratiaMenuWidget.generated.h"

class UBorder;
class UButton;
class UGratiaMenu;
class UGratiaMenuWidget;
class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPanelWidget;
class USizeBox;
class UScrollBox;
class UTextBlock;
class UWidgetSwitcher;
struct FSlateFontInfo;

/** Everything a menu button can do; UGratiaMenu::Execute applies it. */
UENUM()
enum class EGratiaMenuAction : uint8
{
    Tab, StartScene, Lobby, Close, Quit,
    Pause, Restart, PrevPart, NextPart, SpeedDown, SpeedUp, PartnerView,
    Pose, Mood, Demo, Reset,
    Hair, Cloth, Body, Ears, Physics, Springs,
    Quality, Sound, MusicDown, MusicUp, HapticsDown, HapticsUp, HeightDown, HeightUp, Recenter,
    TrackPrev, TrackNext,
    Primitive, PrimitiveSize
};

/** Click/hover target of one button (dynamic delegates need a UFUNCTION). */
UCLASS()
class GRATIAVR_API UGratiaMenuClick : public UObject
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<UGratiaMenuWidget> Widget;
    EGratiaMenuAction Action = EGratiaMenuAction::Close;
    int32 Param = 0;
    int32 Item = INDEX_NONE;
    UFUNCTION() void OnClicked();
    UFUNCTION() void OnHovered();
    UFUNCTION() void OnUnhovered();
};

/**
 * The VR menu panel (ViRo Playspace look), built in C++ without a widget blueprint: a dark violet
 * glass panel with a neon edge, the GRATIA playspace logo, scene pictures in hexagon frames,
 * gradient pill buttons, a now-playing strip with track switching and an equalizer, and a dock
 * of tabs at the bottom. The look comes from the scene library (font and UI materials); without
 * them it falls back to plain rounded boxes. Pointer clicks and stick/keyboard focus both work.
 */
UCLASS()
class GRATIAVR_API UGratiaMenuWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    enum EPage : int32 { Scenes = 0, Playback, Character, Physics, Settings, PageCount };

    TWeakObjectPtr<UGratiaMenu> Menu;
    void ShowPage(int32 Page);
    int32 GetPage() const { return CurrentPage; }
    /** Updates the labels/highlights from the current state (after every action). */
    void RefreshState();
    /** Moves the stick/keyboard focus over the visible buttons; Activate clicks it. */
    void Navigate(int32 Direction);
    void Activate();
    void Click(EGratiaMenuAction Action, int32 Param);
    void Hover(int32 Item, bool bHovered);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

private:
    struct FItem
    {
        TWeakObjectPtr<UButton> Button;
        TWeakObjectPtr<UTextBlock> Label;
        EGratiaMenuAction Action = EGratiaMenuAction::Close;
        int32 Param = 0;
        /** Page the button lives on; INDEX_NONE = header/dock (always visible). */
        int32 Page = INDEX_NONE;
        bool bDynamicLabel = false;
        bool bCard = false;
        FLinearColor Accent = FLinearColor::White;
        /** Material states of the button (normal, hovered, pressed) or the card picture. */
        TWeakObjectPtr<UMaterialInstanceDynamic> Normal, Hovered, Pressed;
        float HoverAmount = 0.0f;
        bool bHovered = false;
        FVector2D Size = FVector2D::ZeroVector;
    };
    void BuildTree();
    UPanelWidget* BuildScenesPage();
    UPanelWidget* BuildPlaybackPage();
    UPanelWidget* BuildCharacterPage();
    UPanelWidget* BuildPhysicsPage();
    UPanelWidget* BuildSettingsPage();
    UButton* AddButton(UPanelWidget* Parent, const FString& Text, EGratiaMenuAction Action, int32 Param, int32 Page, bool bDynamic = false,
        float Width = 0.0f, float Height = 64.0f, int32 FontSize = 22);
    UTextBlock* MakeText(const FString& Text, int32 Size, const FLinearColor& Color, const TCHAR* Typeface = TEXT("Bold"));
    FSlateFontInfo Font(int32 Size, const TCHAR* Typeface) const;
    UMaterialInstanceDynamic* MakeMaterial(UMaterialInterface* Parent);
    void StyleButton(FItem& Item, bool bActive, bool bFocused);
    void BindClick(UButton* Button, EGratiaMenuAction Action, int32 Param, int32 Item);
    TArray<int32> VisibleItems() const;

    UPROPERTY(Transient) TArray<TObjectPtr<UGratiaMenuClick>> Clicks;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages;
    UPROPERTY(Transient) TArray<TObjectPtr<USizeBox>> Bars;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PlaybackText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PlaybackTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Header;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TrackText;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PanelInstance;
    FString LastStatus, LastTrack;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> SceneScroll;
    TArray<FItem> Items;
    int32 CurrentPage = Scenes;
    int32 Focus = INDEX_NONE;
    float Time = 0.0f;
};
