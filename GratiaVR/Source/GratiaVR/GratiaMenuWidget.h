#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GratiaMenuWidget.generated.h"

class UBorder;
class UButton;
class UGratiaMenu;
class UGratiaMenuWidget;
class UPanelWidget;
class USizeBox;
class UScrollBox;
class UTextBlock;
class UWidgetSwitcher;

/** Everything a menu button can do; UGratiaMenu::Execute applies it. */
UENUM()
enum class EGratiaMenuAction : uint8
{
    Tab, StartScene, Lobby, Close, Quit,
    Pause, Restart, PrevPart, NextPart, SpeedDown, SpeedUp, PartnerView,
    Pose, Mood, Demo, Reset,
    Hair, Cloth, Body, Ears, Physics, Springs,
    Quality, Sound, MusicDown, MusicUp, HapticsDown, HapticsUp, HeightDown, HeightUp, Recenter
};

/** Click target of one button (dynamic delegates need a UFUNCTION). */
UCLASS()
class GRATIAVR_API UGratiaMenuClick : public UObject
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<UGratiaMenuWidget> Widget;
    EGratiaMenuAction Action = EGratiaMenuAction::Close;
    int32 Param = 0;
    UFUNCTION() void OnClicked();
};

/**
 * The VR menu panel, built in C++ (no widget blueprint): a sidebar of tabs and pages for the
 * scene library (cards with thumbnails), playback, character, physics and settings, with an
 * equalizer that moves with the music. Pointer (laser) clicks and stick/keyboard focus both work.
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
        /** Page the button lives on; INDEX_NONE = sidebar (always visible). */
        int32 Page = INDEX_NONE;
        bool bDynamicLabel = false;
        bool bCard = false;
        FLinearColor Accent = FLinearColor::White;
    };
    void BuildTree();
    UPanelWidget* BuildScenesPage();
    UPanelWidget* BuildPlaybackPage();
    UPanelWidget* BuildCharacterPage();
    UPanelWidget* BuildPhysicsPage();
    UPanelWidget* BuildSettingsPage();
    UButton* AddButton(UPanelWidget* Parent, const FString& Text, EGratiaMenuAction Action, int32 Param, int32 Page, bool bDynamic = false, float Width = 0.0f);
    UTextBlock* MakeText(const FString& Text, int32 Size, const FLinearColor& Color, bool bBold = false);
    void StyleButton(UButton* Button, bool bActive, bool bFocused);
    TArray<int32> VisibleItems() const;

    UPROPERTY(Transient) TArray<TObjectPtr<UGratiaMenuClick>> Clicks;
    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages;
    UPROPERTY(Transient) TArray<TObjectPtr<USizeBox>> Bars;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PlaybackText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PlaybackTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Header;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    FString LastStatus;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> SceneScroll;
    TArray<FItem> Items;
    int32 CurrentPage = Scenes;
    int32 Focus = INDEX_NONE;
    float Time = 0.0f;
};
