#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GratiaMenuWidget.generated.h"

class UBorder;
class UButton;
class UGratiaMenu;
class UGratiaMenuWidget;
class UHorizontalBox;
class UPanelWidget;
class USizeBox;
class UTextBlock;
class UVerticalBox;
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
    Primitive, PrimitiveSize,
    Captions, VoiceDown, VoiceUp, TurnMode, WalkSpeed, ResetSettings,
    HandPenetration, Forearms
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
 * The VR menu panel in the ViRo Playspace spirit, laid out and styled entirely in code (rounded Slate
 * boxes, no widget blueprint or UI materials): a dark violet glass panel with a neon edge, a top bar with
 * the logo, the now-playing strip and close, a navigation rail on the left and the page on the right.
 * Pages are built from sections (titled cards) and rows: toggles with a switch, segmented choices,
 * steppers with a value, cycle buttons and scene cards with pictures. Every page explains itself with a
 * hint and shows an empty state where its controls do not apply. Only the font and the scene pictures
 * come from the scene library. Pointer clicks and stick/keyboard focus both work.
 */
UCLASS()
class GRATIAVR_API UGratiaMenuWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    enum EPage : int32 { Scenes = 0, Playback, Character, Physics, Settings, Player, PageCount };
    /** Panel size in widget pixels (the world size is this times the component scale). */
    static constexpr float PanelWidth = 1600.0f;
    static constexpr float PanelHeight = 900.0f;

    TWeakObjectPtr<UGratiaMenu> Menu;
    void ShowPage(int32 Page);
    int32 GetPage() const { return CurrentPage; }
    /** Updates the labels, values, switches and highlights from the current state (after every action). */
    void RefreshState();
    /** Moves the stick/keyboard focus over the visible buttons; Activate clicks it. */
    void Navigate(int32 Direction);
    void Activate();
    void Click(EGratiaMenuAction Action, int32 Param);
    void Hover(int32 Item, bool bHovered);
    /** Buttons of the page (and the always visible top bar and rail) that can be clicked now. */
    int32 CountVisibleItems() const { return VisibleItems().Num(); }
    /** Empty-state message shown on the current page instead of its controls ("" when the controls apply). */
    FString GetEmptyState() const;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

private:
    enum class EKind : uint8 { Button, Nav, Card, Toggle, Segment, Step, Cycle, Round };
    struct FItem
    {
        TWeakObjectPtr<UButton> Button;
        TWeakObjectPtr<UTextBlock> Label;
        /** Right-hand value of a cycle button. */
        TWeakObjectPtr<UTextBlock> Value;
        /** Switch of a toggle row: track and knob (the knob slides to the right when on). */
        TWeakObjectPtr<UBorder> Track, Knob;
        /** Picture frame of a scene card and its "now" badge. */
        TWeakObjectPtr<UBorder> Frame;
        TWeakObjectPtr<UWidget> Badge;
        EGratiaMenuAction Action = EGratiaMenuAction::Close;
        int32 Param = 0;
        /** Page the button lives on; INDEX_NONE = top bar/rail (always visible). */
        int32 Page = INDEX_NONE;
        EKind Kind = EKind::Button;
        bool bDynamicLabel = false;
        FLinearColor Accent = FLinearColor::White;
        float Radius = 30.0f;
        float HoverAmount = 0.0f;
        bool bHovered = false;
    };
    /** A value text next to a stepper, refreshed from UGratiaMenu::ValueFor. */
    struct FValue
    {
        TWeakObjectPtr<UTextBlock> Text;
        EGratiaMenuAction Action = EGratiaMenuAction::Close;
    };
    struct FEmpty
    {
        TWeakObjectPtr<UWidget> Controls;
        TWeakObjectPtr<UWidget> Message;
        int32 Page = INDEX_NONE;
    };

    void BuildTree();
    UPanelWidget* BuildScenesPage();
    UPanelWidget* BuildPlaybackPage();
    UPanelWidget* BuildCharacterPage();
    UPanelWidget* BuildPhysicsPage();
    UPanelWidget* BuildSettingsPage();
    UPanelWidget* BuildPlayerPage();

    // Building blocks.
    UTextBlock* MakeText(const FString& Text, int32 Size, const FLinearColor& Color, const TCHAR* Typeface = TEXT("Bold"));
    FSlateFontInfo Font(int32 Size, const TCHAR* Typeface) const;
    UButton* MakeButton(int32& OutItem, EKind Kind, EGratiaMenuAction Action, int32 Param, int32 Page, float Radius);
    UWidget* Sized(UWidget* Content, float Width, float Height);
    /** A titled card; returns its body. */
    UVerticalBox* AddSection(UPanelWidget* Column, const FString& Title, bool bFill = false);
    /** Two equal columns of the page. */
    void MakeColumns(UPanelWidget* Page, UVerticalBox*& Left, UVerticalBox*& Right);
    /** Label (and an optional hint under it) at the left of a row. */
    UWidget* MakeLabel(const FString& Label, const FString& Hint);
    UHorizontalBox* AddRow(UVerticalBox* Section, const FString& Label, const FString& Hint);
    void AddTextButton(UPanelWidget* Parent, const FString& Text, EGratiaMenuAction Action, int32 Param, int32 Page, float Width,
        float Height = 64.0f, int32 FontSize = 22, bool bDynamic = false);
    void AddToggle(UVerticalBox* Section, const FString& Label, const FString& Hint, EGratiaMenuAction Action, int32 Page);
    void AddSegments(UPanelWidget* Parent, const TArray<FString>& Labels, EGratiaMenuAction Action, int32 Page, float Width);
    void AddStepper(UVerticalBox* Section, const FString& Label, const FString& Hint, EGratiaMenuAction Down, EGratiaMenuAction Up, int32 Page);
    void AddCycle(UVerticalBox* Section, const FString& Label, const FString& Hint, EGratiaMenuAction Action, int32 Page);
    UWidget* MakeEmptyState(const FString& Title, const FString& Text);
    void BindClick(UButton* Button, EGratiaMenuAction Action, int32 Param, int32 Item);
    void StyleItem(FItem& Item, bool bEnabled, bool bActive, bool bFocused);
    TArray<int32> VisibleItems() const;

    UPROPERTY(Transient) TArray<TObjectPtr<UGratiaMenuClick>> Clicks;
    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages;
    UPROPERTY(Transient) TArray<TObjectPtr<USizeBox>> Bars;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PlaybackText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PlaybackTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Header;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HeaderHint;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TrackText;
    UPROPERTY(Transient) TObjectPtr<UBorder> PanelEdge;
    FString LastStatus, LastTrack;
    TArray<FItem> Items;
    TArray<FValue> Values;
    TArray<FEmpty> Empties;
    int32 CurrentPage = Scenes;
    int32 Focus = INDEX_NONE;
    float Time = 0.0f;
};
