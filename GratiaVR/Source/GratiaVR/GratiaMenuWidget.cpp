#include "GratiaMenuWidget.h"
#include "GratiaMenu.h"
#include "GratiaSceneDirector.h"
#include "GratiaSceneLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"

namespace
{
const FLinearColor GratiaInk(0.95f, 0.94f, 0.98f);
const FLinearColor GratiaDim(0.62f, 0.60f, 0.70f);
const FLinearColor GratiaAccent(0.95f, 0.35f, 0.65f);
const FLinearColor GratiaViolet(0.48f, 0.38f, 0.98f);
const FLinearColor GratiaPanel(0.035f, 0.03f, 0.055f, 0.94f);
const FLinearColor GratiaCard(0.085f, 0.075f, 0.12f, 0.96f);

FSlateBrush GratiaRounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float Width = 0.0f)
{
    FSlateBrush Brush;
    Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
    Brush.TintColor = FSlateColor(Fill);
    Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius), FSlateColor(Outline), Width);
    Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
    return Brush;
}

const TCHAR* GratiaPageNames[] = {TEXT("Сцены"), TEXT("Воспроизведение"), TEXT("Персонаж"), TEXT("Физика"), TEXT("Настройки")};
}

void UGratiaMenuClick::OnClicked()
{
    if (UGratiaMenuWidget* Owner = Widget.Get()) Owner->Click(Action, Param);
}

TSharedRef<SWidget> UGratiaMenuWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget) BuildTree();
    return Super::RebuildWidget();
}

UTextBlock* UGratiaMenuWidget::MakeText(const FString& Text, int32 Size, const FLinearColor& Color, bool bBold)
{
    UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
    Block->SetText(FText::FromString(Text));
    Block->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
    Block->SetColorAndOpacity(FSlateColor(Color));
    return Block;
}

void UGratiaMenuWidget::StyleButton(UButton* Button, bool bActive, bool bFocused)
{
    if (!Button) return;
    const FLinearColor Base = bActive ? GratiaAccent * FLinearColor(0.75f, 0.75f, 0.75f, 1.0f) : FLinearColor(0.13f, 0.12f, 0.18f, 0.95f);
    const FLinearColor Edge = bFocused ? GratiaInk : bActive ? GratiaAccent : FLinearColor(0.25f, 0.22f, 0.34f, 1.0f);
    FButtonStyle Style;
    Style.SetNormal(GratiaRounded(Base, 14.0f, Edge, bFocused ? 3.0f : 1.0f));
    Style.SetHovered(GratiaRounded(FLinearColor::LerpUsingHSV(Base, GratiaAccent, 0.55f), 14.0f, GratiaInk, 2.0f));
    Style.SetPressed(GratiaRounded(GratiaAccent, 14.0f, GratiaInk, 2.0f));
    Style.SetNormalPadding(FMargin(18, 12));
    Style.SetPressedPadding(FMargin(18, 13, 18, 11));
    Button->SetStyle(Style);
}

UButton* UGratiaMenuWidget::AddButton(UPanelWidget* Parent, const FString& Text, EGratiaMenuAction Action, int32 Param, int32 Page, bool bDynamic, float Width)
{
    UButton* Button = WidgetTree->ConstructWidget<UButton>();
    UTextBlock* Label = MakeText(Text, 22, GratiaInk);
    Label->SetJustification(ETextJustify::Center);
    Button->AddChild(Label);
    StyleButton(Button, false, false);
    UGratiaMenuClick* ClickTarget = NewObject<UGratiaMenuClick>(this);
    ClickTarget->Widget = this; ClickTarget->Action = Action; ClickTarget->Param = Param;
    Button->OnClicked.AddDynamic(ClickTarget, &UGratiaMenuClick::OnClicked);
    Clicks.Add(ClickTarget);
    UWidget* Placed = Button;
    if (Width > 0.0f)
    {
        USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
        Size->SetWidthOverride(Width);
        Size->AddChild(Button);
        Placed = Size;
    }
    UPanelSlot* ItemSlot = Parent->AddChild(Placed);
    if (UVerticalBoxSlot* Vertical = Cast<UVerticalBoxSlot>(ItemSlot)) Vertical->SetPadding(FMargin(0, 6));
    if (UHorizontalBoxSlot* Horizontal = Cast<UHorizontalBoxSlot>(ItemSlot)) { Horizontal->SetPadding(FMargin(6, 0)); Horizontal->SetVerticalAlignment(VAlign_Center); }
    Items.Add({Button, Label, Action, Param, Page, bDynamic, false});
    return Button;
}

void UGratiaMenuWidget::BuildTree()
{
    UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
    Background->SetBrush(GratiaRounded(GratiaPanel, 32.0f, GratiaViolet * FLinearColor(1, 1, 1, 0.6f), 2.0f));
    Background->SetPadding(FMargin(28));
    WidgetTree->RootWidget = Background;
    UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>();
    Background->SetContent(Columns);

    // Sidebar: title, tabs, equalizer, lobby/close.
    USizeBox* SideSize = WidgetTree->ConstructWidget<USizeBox>();
    SideSize->SetWidthOverride(290.0f);
    UVerticalBox* Side = WidgetTree->ConstructWidget<UVerticalBox>();
    SideSize->AddChild(Side);
    Columns->AddChildToHorizontalBox(SideSize)->SetPadding(FMargin(0, 0, 24, 0));
    UTextBlock* Logo = MakeText(TEXT("GRATIA"), 54, GratiaInk, true);
    Side->AddChildToVerticalBox(Logo);
    UTextBlock* Tagline = MakeText(TEXT("VR  PLAYSPACE"), 18, GratiaAccent, true);
    Side->AddChildToVerticalBox(Tagline)->SetPadding(FMargin(4, 0, 0, 22));
    for (int32 Page = 0; Page < PageCount; ++Page) AddButton(Side, GratiaPageNames[Page], EGratiaMenuAction::Tab, Page, INDEX_NONE);
    USpacer* Gap = WidgetTree->ConstructWidget<USpacer>();
    Side->AddChildToVerticalBox(Gap)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    // Equalizer: bars follow the music the environment reacts to.
    UHorizontalBox* Equalizer = WidgetTree->ConstructWidget<UHorizontalBox>();
    for (int32 I = 0; I < 18; ++I)
    {
        USizeBox* Bar = WidgetTree->ConstructWidget<USizeBox>();
        Bar->SetWidthOverride(9.0f);
        Bar->SetHeightOverride(8.0f);
        UBorder* Fill = WidgetTree->ConstructWidget<UBorder>();
        Fill->SetBrush(GratiaRounded(FLinearColor::LerpUsingHSV(GratiaViolet, GratiaAccent, I / 17.0f), 4.0f));
        Bar->AddChild(Fill);
        UHorizontalBoxSlot* ItemSlot = Equalizer->AddChildToHorizontalBox(Bar);
        ItemSlot->SetPadding(FMargin(2, 0));
        ItemSlot->SetVerticalAlignment(VAlign_Bottom);
        Bars.Add(Bar);
    }
    USizeBox* EqualizerSize = WidgetTree->ConstructWidget<USizeBox>();
    EqualizerSize->SetHeightOverride(70.0f);
    EqualizerSize->AddChild(Equalizer);
    Side->AddChildToVerticalBox(EqualizerSize)->SetPadding(FMargin(4, 10));
    AddButton(Side, TEXT("В лобби"), EGratiaMenuAction::Lobby, 0, INDEX_NONE);
    AddButton(Side, TEXT("Закрыть"), EGratiaMenuAction::Close, 0, INDEX_NONE);

    // Content: header and pages.
    UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
    Columns->AddChildToHorizontalBox(Content)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Header = MakeText(TEXT("Сцены"), 36, GratiaInk, true);
    Content->AddChildToVerticalBox(Header)->SetPadding(FMargin(4, 4, 0, 16));
    StatusText = MakeText(TEXT(""), 18, GratiaAccent);
    StatusText->SetAutoWrapText(true);
    StatusText->SetVisibility(ESlateVisibility::Collapsed);
    Content->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(4, 0, 4, 12));
    Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>();
    Content->AddChildToVerticalBox(Pages)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Pages->AddChild(BuildScenesPage());
    Pages->AddChild(BuildPlaybackPage());
    Pages->AddChild(BuildCharacterPage());
    Pages->AddChild(BuildPhysicsPage());
    Pages->AddChild(BuildSettingsPage());
    ShowPage(Scenes);
}

UPanelWidget* UGratiaMenuWidget::BuildScenesPage()
{
    UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    SceneScroll = Scroll;
    UWrapBox* Cards = WidgetTree->ConstructWidget<UWrapBox>();
    Cards->SetInnerSlotPadding(FVector2D(18, 18));
    Scroll->AddChild(Cards);
    const UGratiaSceneDirector* Director = Menu.IsValid() ? Menu->GetDirector() : nullptr;
    const UGratiaSceneLibrary* Library = Director ? Director->Library.Get() : nullptr;
    if (!Library) { Cards->AddChild(MakeText(TEXT("Библиотека сцен недоступна"), 24, GratiaDim)); return Scroll; }
    for (int32 Index = 0; Index < Library->Scenes.Num(); ++Index)
    {
        const FGratiaSceneEntry& Entry = Library->Scenes[Index];
        UButton* Card = WidgetTree->ConstructWidget<UButton>();
        FButtonStyle Style;
        Style.SetNormal(GratiaRounded(GratiaCard, 22.0f, Entry.Accent * FLinearColor(1, 1, 1, 0.55f), 2.0f));
        Style.SetHovered(GratiaRounded(GratiaCard * 1.6f, 22.0f, Entry.Accent, 4.0f));
        Style.SetPressed(GratiaRounded(Entry.Accent * 0.5f, 22.0f, GratiaInk, 4.0f));
        Style.SetNormalPadding(FMargin(14));
        Style.SetPressedPadding(FMargin(14, 15, 14, 13));
        Card->SetStyle(Style);
        UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>();
        Card->AddChild(Body);
        // Thumbnail 16:9; without one, an accent gradient panel with the title's initial.
        USizeBox* Picture = WidgetTree->ConstructWidget<USizeBox>();
        Picture->SetWidthOverride(400.0f);
        Picture->SetHeightOverride(225.0f);
        if (UTexture2D* Thumbnail = Entry.Thumbnail.LoadSynchronous())
        {
            UImage* Image = WidgetTree->ConstructWidget<UImage>();
            FSlateBrush Brush = GratiaRounded(FLinearColor::White, 16.0f);
            Brush.SetResourceObject(Thumbnail);
            Image->SetBrush(Brush);
            Picture->AddChild(Image);
        }
        else
        {
            UBorder* Plate = WidgetTree->ConstructWidget<UBorder>();
            Plate->SetBrush(GratiaRounded(Entry.Accent * FLinearColor(0.35f, 0.35f, 0.35f, 1.0f), 16.0f));
            Plate->SetHorizontalAlignment(HAlign_Center);
            Plate->SetVerticalAlignment(VAlign_Center);
            Plate->SetContent(MakeText(Entry.Title.ToString().Left(1), 96, GratiaInk, true));
            Picture->AddChild(Plate);
        }
        Body->AddChildToVerticalBox(Picture);
        UTextBlock* Title = MakeText(Entry.Title.ToString(), 28, GratiaInk, true);
        Body->AddChildToVerticalBox(Title)->SetPadding(FMargin(4, 12, 4, 2));
        UTextBlock* Description = MakeText(Entry.Description.ToString(), 17, GratiaDim);
        Description->SetAutoWrapText(true);
        USizeBox* DescriptionSize = WidgetTree->ConstructWidget<USizeBox>();
        DescriptionSize->SetWidthOverride(400.0f);
        DescriptionSize->SetHeightOverride(52.0f);
        DescriptionSize->AddChild(Description);
        Body->AddChildToVerticalBox(DescriptionSize)->SetPadding(FMargin(4, 0));
        UGratiaMenuClick* ClickTarget = NewObject<UGratiaMenuClick>(this);
        ClickTarget->Widget = this; ClickTarget->Action = EGratiaMenuAction::StartScene; ClickTarget->Param = Index;
        Card->OnClicked.AddDynamic(ClickTarget, &UGratiaMenuClick::OnClicked);
        Clicks.Add(ClickTarget);
        Cards->AddChild(Card);
        Items.Add({Card, nullptr, EGratiaMenuAction::StartScene, Index, Scenes, false, true});
        Items.Last().Accent = Entry.Accent;
    }
    return Scroll;
}

UPanelWidget* UGratiaMenuWidget::BuildPlaybackPage()
{
    UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();
    PlaybackTitle = MakeText(TEXT(""), 30, GratiaInk, true);
    Page->AddChildToVerticalBox(PlaybackTitle)->SetPadding(FMargin(4, 0, 0, 6));
    PlaybackText = MakeText(TEXT(""), 24, GratiaAccent);
    Page->AddChildToVerticalBox(PlaybackText)->SetPadding(FMargin(4, 0, 0, 22));
    UHorizontalBox* Transport = WidgetTree->ConstructWidget<UHorizontalBox>();
    Page->AddChildToVerticalBox(Transport)->SetPadding(FMargin(0, 6));
    AddButton(Transport, TEXT("Пред. часть"), EGratiaMenuAction::PrevPart, 0, Playback, false, 170.0f);
    AddButton(Transport, TEXT("Пауза"), EGratiaMenuAction::Pause, 0, Playback, true, 190.0f);
    AddButton(Transport, TEXT("След. часть"), EGratiaMenuAction::NextPart, 0, Playback, false, 170.0f);
    AddButton(Transport, TEXT("Сначала"), EGratiaMenuAction::Restart, 0, Playback, false, 170.0f);
    UHorizontalBox* Speed = WidgetTree->ConstructWidget<UHorizontalBox>();
    Page->AddChildToVerticalBox(Speed)->SetPadding(FMargin(0, 10));
    AddButton(Speed, TEXT("Медленнее"), EGratiaMenuAction::SpeedDown, 0, Playback, false, 190.0f);
    AddButton(Speed, TEXT("Быстрее"), EGratiaMenuAction::SpeedUp, 0, Playback, false, 170.0f);
    AddButton(Page, TEXT("Вид"), EGratiaMenuAction::PartnerView, 0, Playback, true);
    return Page;
}

UPanelWidget* UGratiaMenuWidget::BuildCharacterPage()
{
    UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();
    AddButton(Page, TEXT("Поза"), EGratiaMenuAction::Pose, 0, Character, true);
    UHorizontalBox* Moods = WidgetTree->ConstructWidget<UHorizontalBox>();
    Page->AddChildToVerticalBox(Moods)->SetPadding(FMargin(0, 6));
    AddButton(Moods, TEXT("Спокойная"), EGratiaMenuAction::Mood, 0, Character, false, 220.0f);
    AddButton(Moods, TEXT("Радостная"), EGratiaMenuAction::Mood, 1, Character, false, 200.0f);
    AddButton(Moods, TEXT("Сдержанная"), EGratiaMenuAction::Mood, 2, Character, false, 220.0f);
    AddButton(Page, TEXT("Демо реакций"), EGratiaMenuAction::Demo, 0, Character, true);
    AddButton(Page, TEXT("Сбросить позу, контакты и высоту"), EGratiaMenuAction::Reset, 0, Character);
    return Page;
}

UPanelWidget* UGratiaMenuWidget::BuildPhysicsPage()
{
    UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();
    AddButton(Page, TEXT("Волосы"), EGratiaMenuAction::Hair, 0, Physics, true);
    AddButton(Page, TEXT("Одежда"), EGratiaMenuAction::Cloth, 0, Physics, true);
    AddButton(Page, TEXT("Тело"), EGratiaMenuAction::Body, 0, Physics, true);
    AddButton(Page, TEXT("Уши / хвост"), EGratiaMenuAction::Ears, 0, Physics, true);
    AddButton(Page, TEXT("Вторичная физика"), EGratiaMenuAction::Physics, 0, Physics, true);
    AddButton(Page, TEXT("Локальные пружины"), EGratiaMenuAction::Springs, 0, Physics, true);
    return Page;
}

UPanelWidget* UGratiaMenuWidget::BuildSettingsPage()
{
    UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();
    UHorizontalBox* Quality = WidgetTree->ConstructWidget<UHorizontalBox>();
    Page->AddChildToVerticalBox(Quality)->SetPadding(FMargin(0, 6));
    AddButton(Quality, TEXT("Низкое"), EGratiaMenuAction::Quality, 0, Settings, false, 170.0f);
    AddButton(Quality, TEXT("Среднее"), EGratiaMenuAction::Quality, 1, Settings, false, 170.0f);
    AddButton(Quality, TEXT("Высокое"), EGratiaMenuAction::Quality, 2, Settings, false, 170.0f);
    AddButton(Page, TEXT("Звук"), EGratiaMenuAction::Sound, 0, Settings, true);
    auto Stepper = [this, Page](EGratiaMenuAction Down, EGratiaMenuAction Up)
    {
        UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        Page->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 6));
        AddButton(Row, TEXT("-"), Down, 0, Settings, false, 90.0f);
        AddButton(Row, TEXT(""), Up, 0, Settings, true, 380.0f);
    };
    Stepper(EGratiaMenuAction::MusicDown, EGratiaMenuAction::MusicUp);
    Stepper(EGratiaMenuAction::HapticsDown, EGratiaMenuAction::HapticsUp);
    Stepper(EGratiaMenuAction::HeightDown, EGratiaMenuAction::HeightUp);
    AddButton(Page, TEXT("Центрировать взгляд"), EGratiaMenuAction::Recenter, 0, Settings);
    AddButton(Page, TEXT("Выход из игры"), EGratiaMenuAction::Quit, 0, Settings);
    return Page;
}

void UGratiaMenuWidget::ShowPage(int32 Page)
{
    CurrentPage = FMath::Clamp(Page, 0, PageCount - 1);
    if (Pages) Pages->SetActiveWidgetIndex(CurrentPage);
    if (Header) Header->SetText(FText::FromString(GratiaPageNames[CurrentPage]));
    Focus = INDEX_NONE;
    RefreshState();
}

TArray<int32> UGratiaMenuWidget::VisibleItems() const
{
    TArray<int32> Result;
    for (int32 I = 0; I < Items.Num(); ++I)
        if ((Items[I].Page == INDEX_NONE || Items[I].Page == CurrentPage) && Items[I].Button.IsValid() && Items[I].Button->IsVisible() && Items[I].Button->GetIsEnabled()) Result.Add(I);
    return Result;
}

void UGratiaMenuWidget::RefreshState()
{
    if (!Menu.IsValid()) return;
    for (int32 I = 0; I < Items.Num(); ++I)
    {
        FItem& Item = Items[I];
        UButton* Button = Item.Button.Get();
        if (!Button) continue;
        Button->SetIsEnabled(Menu->IsActionAvailable(Item.Action, Item.Param));
        if (Item.bDynamicLabel && Item.Label.IsValid()) Item.Label->SetText(FText::FromString(Menu->LabelFor(Item.Action, Item.Param)));
        // The lobby button only exists inside a scene; playback only inside a performance.
        if (Item.Action == EGratiaMenuAction::Lobby)
            Button->SetVisibility(Menu->IsInScene() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        if (Item.bCard)
        {
            FButtonStyle Style = Button->GetStyle();
            Style.SetNormal(GratiaRounded(GratiaCard, 22.0f, I == Focus ? GratiaInk : Item.Accent * FLinearColor(1, 1, 1, 0.55f), I == Focus ? 4.0f : 2.0f));
            Button->SetStyle(Style);
            continue;
        }
        const bool bActive = Item.Action == EGratiaMenuAction::Tab ? Item.Param == CurrentPage : Menu->IsSelected(Item.Action, Item.Param);
        StyleButton(Button, bActive, I == Focus);
    }
    for (const FItem& Item : Items)
        if (Item.bCard && Item.Button.IsValid()) Item.Button->SetRenderOpacity(Item.Param == Menu->GetCurrentScene() ? 1.0f : 0.92f);
}

void UGratiaMenuWidget::Navigate(int32 Direction)
{
    const TArray<int32> Visible = VisibleItems();
    if (Visible.IsEmpty()) return;
    const int32 Position = Visible.IndexOfByKey(Focus);
    Focus = Visible[Position == INDEX_NONE ? 0 : (Position + Direction + Visible.Num()) % Visible.Num()];
    if (Items[Focus].bCard && SceneScroll) SceneScroll->ScrollWidgetIntoView(Items[Focus].Button.Get(), true);
    RefreshState();
}

void UGratiaMenuWidget::Activate()
{
    if (Items.IsValidIndex(Focus) && VisibleItems().Contains(Focus)) Click(Items[Focus].Action, Items[Focus].Param);
    else Navigate(1);
}

void UGratiaMenuWidget::Click(EGratiaMenuAction Action, int32 Param)
{
    if (Action == EGratiaMenuAction::Tab) { ShowPage(Param); return; }
    if (Menu.IsValid()) Menu->Execute(Action, Param);
    RefreshState();
}

void UGratiaMenuWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    Time += DeltaSeconds;
    const UGratiaSceneDirector* Director = Menu.IsValid() ? Menu->GetDirector() : nullptr;
    const FVector4f Music = Director ? Director->GetMusicFrame() : FVector4f(0, 0, 0, 0);
    const FString Status = Director ? Director->GetLastError() : FString();
    if (StatusText && Status != LastStatus)
    {
        LastStatus = Status;
        StatusText->SetText(FText::FromString(Status));
        StatusText->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    }
    for (int32 I = 0; I < Bars.Num(); ++I)
    {
        // Low bars follow the bass, the middle the mids, the right the highs; a ripple on top.
        const float T = I / float(FMath::Max(1, Bars.Num() - 1));
        const float Band = T < 0.33f ? Music.X : T < 0.66f ? Music.Y : Music.Z;
        const float Ripple = 0.5f + 0.5f * FMath::Sin(Time * 7.0f + I * 1.3f);
        Bars[I]->SetHeightOverride(6.0f + 60.0f * FMath::Clamp(Band * (0.6f + 0.4f * Ripple) + 0.25f * Music.W, 0.0f, 1.0f));
    }
    if (CurrentPage == Playback && Director && PlaybackText)
    {
        const FGratiaSceneEntry* Entry = Director->GetCurrentEntry();
        PlaybackTitle->SetText(Entry ? Entry->Title : FText::FromString(TEXT("Свободная сцена")));
        PlaybackText->SetText(FText::FromString(Director->GetPlaybackText()));
    }
}
