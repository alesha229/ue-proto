#include "GratiaMenuWidget.h"
#include "GratiaMenu.h"
#include "GratiaSceneDirector.h"
#include "GratiaSceneLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
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
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Styling/CoreStyle.h"

namespace
{
// ViRo palette (linear): white ink, neon pink, magenta, violet, cyan, deep night.
const FLinearColor GratiaInk(1.0f, 0.93f, 0.98f);
const FLinearColor GratiaDim(0.55f, 0.47f, 0.70f);
const FLinearColor GratiaPink(1.0f, 0.06f, 0.42f);
const FLinearColor GratiaMagenta(0.62f, 0.02f, 1.0f);
const FLinearColor GratiaViolet(0.18f, 0.05f, 1.0f);
const FLinearColor GratiaCyan(0.05f, 0.62f, 1.0f);
const FLinearColor GratiaNight(0.012f, 0.004f, 0.03f, 0.95f);

FSlateBrush GratiaRounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float Width = 0.0f)
{
    FSlateBrush Brush;
    Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
    Brush.TintColor = FSlateColor(Fill);
    Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius), FSlateColor(Outline), Width);
    Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
    return Brush;
}

FSlateBrush GratiaMaterialBrush(UMaterialInstanceDynamic* Material, const FVector2D& Size)
{
    FSlateBrush Brush;
    Brush.SetResourceObject(Material);
    Brush.ImageSize = Size;
    Brush.DrawAs = ESlateBrushDrawType::Image;
    return Brush;
}

const TCHAR* GratiaPageNames[] = {TEXT("Сцены"), TEXT("Сцена"), TEXT("Персонаж"), TEXT("Физика"), TEXT("Настройки")};

const UGratiaSceneLibrary* GratiaLibraryOf(const TWeakObjectPtr<UGratiaMenu>& Menu)
{
    const UGratiaSceneDirector* Director = Menu.IsValid() ? Menu->GetDirector() : nullptr;
    return Director ? Director->Library.Get() : nullptr;
}
}

void UGratiaMenuClick::OnClicked() { if (UGratiaMenuWidget* Owner = Widget.Get()) Owner->Click(Action, Param); }
void UGratiaMenuClick::OnHovered() { if (UGratiaMenuWidget* Owner = Widget.Get()) Owner->Hover(Item, true); }
void UGratiaMenuClick::OnUnhovered() { if (UGratiaMenuWidget* Owner = Widget.Get()) Owner->Hover(Item, false); }

TSharedRef<SWidget> UGratiaMenuWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget) BuildTree();
    return Super::RebuildWidget();
}

FSlateFontInfo UGratiaMenuWidget::Font(int32 Size, const TCHAR* Typeface) const
{
    const UGratiaSceneLibrary* Library = GratiaLibraryOf(Menu);
    if (Library && Library->Font) return FSlateFontInfo(Library->Font, Size, FName(Typeface));
    const bool bHeavy = FCString::Strcmp(Typeface, TEXT("Regular")) != 0;
    return FCoreStyle::GetDefaultFontStyle(bHeavy ? "Bold" : "Regular", Size);
}

UTextBlock* UGratiaMenuWidget::MakeText(const FString& Text, int32 Size, const FLinearColor& Color, const TCHAR* Typeface)
{
    UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
    Block->SetText(FText::FromString(Text));
    Block->SetFont(Font(Size, Typeface));
    Block->SetColorAndOpacity(FSlateColor(Color));
    return Block;
}

UMaterialInstanceDynamic* UGratiaMenuWidget::MakeMaterial(UMaterialInterface* Parent)
{
    if (!Parent) return nullptr;
    UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Parent, this);
    Materials.Add(Instance);
    return Instance;
}

void UGratiaMenuWidget::BindClick(UButton* Button, EGratiaMenuAction Action, int32 Param, int32 Item)
{
    UGratiaMenuClick* Target = NewObject<UGratiaMenuClick>(this);
    Target->Widget = this; Target->Action = Action; Target->Param = Param; Target->Item = Item;
    Button->OnClicked.AddDynamic(Target, &UGratiaMenuClick::OnClicked);
    Button->OnHovered.AddDynamic(Target, &UGratiaMenuClick::OnHovered);
    Button->OnUnhovered.AddDynamic(Target, &UGratiaMenuClick::OnUnhovered);
    Clicks.Add(Target);
}

void UGratiaMenuWidget::StyleButton(FItem& Item, bool bActive, bool bFocused)
{
    UButton* Button = Item.Button.Get();
    if (!Button) return;
    if (Item.Normal.IsValid())
    {
        for (UMaterialInstanceDynamic* State : {Item.Normal.Get(), Item.Hovered.Get(), Item.Pressed.Get()})
        {
            if (!State) continue;
            State->SetScalarParameterValue(TEXT("Active"), bActive ? 1.0f : 0.0f);
            State->SetScalarParameterValue(TEXT("Focus"), bFocused ? 1.0f : 0.0f);
        }
        if (Item.bCard) Item.Normal->SetScalarParameterValue(TEXT("Hover"), FMath::Max(Item.HoverAmount, bFocused ? 1.0f : 0.0f));
        return;
    }
    // Fallback look without the UI materials.
    const FLinearColor Base = bActive ? GratiaPink * 0.7f : FLinearColor(0.09f, 0.04f, 0.17f, 0.95f);
    const FLinearColor Edge = bFocused ? GratiaInk : bActive ? GratiaPink : GratiaViolet * 0.8f;
    FButtonStyle Style;
    Style.SetNormal(GratiaRounded(Base, 30.0f, Edge, bFocused ? 3.0f : 1.5f));
    Style.SetHovered(GratiaRounded(FLinearColor::LerpUsingHSV(Base, GratiaPink, 0.5f), 30.0f, GratiaInk, 2.0f));
    Style.SetPressed(GratiaRounded(GratiaPink, 30.0f, GratiaInk, 2.0f));
    Style.SetNormalPadding(FMargin(20, 10));
    Style.SetPressedPadding(FMargin(20, 11, 20, 9));
    Button->SetStyle(Style);
}

UButton* UGratiaMenuWidget::AddButton(UPanelWidget* Parent, const FString& Text, EGratiaMenuAction Action, int32 Param, int32 Page, bool bDynamic,
    float Width, float Height, int32 FontSize)
{
    UButton* Button = WidgetTree->ConstructWidget<UButton>();
    UTextBlock* Label = MakeText(Text, FontSize, GratiaInk, TEXT("ExtraBold"));
    Label->SetJustification(ETextJustify::Center);
    Button->AddChild(Label);
    const int32 Index = Items.Num();
    FItem& Item = Items.AddDefaulted_GetRef();
    Item.Button = Button; Item.Label = Label; Item.Action = Action; Item.Param = Param; Item.Page = Page; Item.bDynamicLabel = bDynamic;
    Item.Size = FVector2D(Width > 0.0f ? Width : 420.0f, Height);
    // Gradient pill (UI material) in three states; its shape follows the pill's aspect.
    const UGratiaSceneLibrary* Library = GratiaLibraryOf(Menu);
    if (Library && Library->PillMaterial)
    {
        UMaterialInstanceDynamic* Normal = MakeMaterial(Library->PillMaterial);
        UMaterialInstanceDynamic* Hovered = MakeMaterial(Library->PillMaterial);
        UMaterialInstanceDynamic* Pressed = MakeMaterial(Library->PillMaterial);
        for (UMaterialInstanceDynamic* State : {Normal, Hovered, Pressed}) State->SetScalarParameterValue(TEXT("Aspect"), Item.Size.X / FMath::Max(1.0f, float(Item.Size.Y)));
        Hovered->SetScalarParameterValue(TEXT("Hover"), 1.0f);
        Pressed->SetScalarParameterValue(TEXT("Hover"), 1.0f);
        Pressed->SetScalarParameterValue(TEXT("Pressed"), 1.0f);
        FButtonStyle Style;
        Style.SetNormal(GratiaMaterialBrush(Normal, Item.Size));
        Style.SetHovered(GratiaMaterialBrush(Hovered, Item.Size));
        Style.SetPressed(GratiaMaterialBrush(Pressed, Item.Size));
        Style.SetDisabled(GratiaMaterialBrush(Normal, Item.Size));
        Style.SetNormalPadding(FMargin(18, 6));
        Style.SetPressedPadding(FMargin(18, 7, 18, 5));
        Button->SetStyle(Style);
        Item.Normal = Normal; Item.Hovered = Hovered; Item.Pressed = Pressed;
    }
    StyleButton(Item, false, false);
    BindClick(Button, Action, Param, Index);
    USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(Item.Size.X);
    Size->SetHeightOverride(Item.Size.Y);
    Size->AddChild(Button);
    UPanelSlot* ItemSlot = Parent->AddChild(Size);
    if (UVerticalBoxSlot* Vertical = Cast<UVerticalBoxSlot>(ItemSlot)) Vertical->SetPadding(FMargin(0, 7));
    if (UHorizontalBoxSlot* Horizontal = Cast<UHorizontalBoxSlot>(ItemSlot)) { Horizontal->SetPadding(FMargin(7, 0)); Horizontal->SetVerticalAlignment(VAlign_Center); }
    return Button;
}

void UGratiaMenuWidget::BuildTree()
{
    const UGratiaSceneLibrary* Library = GratiaLibraryOf(Menu);
    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
    WidgetTree->RootWidget = Root;
    // Panel: violet glass with a neon edge, halftone and sparkles (UI material), or a plain box.
    if (Library && Library->PanelMaterial)
    {
        UImage* Backdrop = WidgetTree->ConstructWidget<UImage>();
        PanelInstance = MakeMaterial(Library->PanelMaterial);
        PanelInstance->SetScalarParameterValue(TEXT("Aspect"), 1600.0f / 900.0f);
        Backdrop->SetBrush(GratiaMaterialBrush(PanelInstance, FVector2D(1600, 900)));
        UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop);
        BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
        BackdropSlot->SetVerticalAlignment(VAlign_Fill);
    }
    else
    {
        UBorder* Plain = WidgetTree->ConstructWidget<UBorder>();
        Plain->SetBrush(GratiaRounded(GratiaNight, 40.0f, GratiaPink, 3.0f));
        UOverlaySlot* PlainSlot = Root->AddChildToOverlay(Plain);
        PlainSlot->SetHorizontalAlignment(HAlign_Fill);
        PlainSlot->SetVerticalAlignment(VAlign_Fill);
    }
    UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
    UOverlaySlot* ContentSlot = Root->AddChildToOverlay(Content);
    ContentSlot->SetPadding(FMargin(56, 40, 56, 34));
    ContentSlot->SetHorizontalAlignment(HAlign_Fill);
    ContentSlot->SetVerticalAlignment(VAlign_Fill);

    // Header: logo (white, pink outline; "playspace" in pink) and the now-playing strip.
    UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    Content->AddChildToVerticalBox(HeaderRow);
    UVerticalBox* Logo = WidgetTree->ConstructWidget<UVerticalBox>();
    UTextBlock* Name = MakeText(TEXT("GRATIA"), 66, GratiaInk, TEXT("Black"));
    FSlateFontInfo NameFont = Name->GetFont();
    NameFont.OutlineSettings.OutlineSize = 3;
    NameFont.OutlineSettings.OutlineColor = GratiaPink;
    NameFont.LetterSpacing = 120;
    Name->SetFont(NameFont);
    Logo->AddChildToVerticalBox(Name);
    UTextBlock* Space = MakeText(TEXT("playspace"), 30, GratiaPink, TEXT("ExtraBold"));
    Logo->AddChildToVerticalBox(Space)->SetPadding(FMargin(6, -10, 0, 0));
    HeaderRow->AddChildToHorizontalBox(Logo)->SetVerticalAlignment(VAlign_Center);
    HeaderRow->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    // Now playing: equalizer, track, previous/next.
    UBorder* Strip = WidgetTree->ConstructWidget<UBorder>();
    Strip->SetBrush(GratiaRounded(FLinearColor(0.05f, 0.015f, 0.10f, 0.85f), 44.0f, GratiaViolet * 0.9f, 2.0f));
    Strip->SetPadding(FMargin(22, 10, 12, 10));
    UHorizontalBox* StripRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    Strip->SetContent(StripRow);
    UHorizontalBox* Equalizer = WidgetTree->ConstructWidget<UHorizontalBox>();
    for (int32 I = 0; I < 14; ++I)
    {
        USizeBox* Bar = WidgetTree->ConstructWidget<USizeBox>();
        Bar->SetWidthOverride(6.0f);
        Bar->SetHeightOverride(6.0f);
        UBorder* Fill = WidgetTree->ConstructWidget<UBorder>();
        Fill->SetBrush(GratiaRounded(FLinearColor::LerpUsingHSV(GratiaPink, GratiaCyan, I / 13.0f), 3.0f));
        Bar->AddChild(Fill);
        UHorizontalBoxSlot* BarSlot = Equalizer->AddChildToHorizontalBox(Bar);
        BarSlot->SetPadding(FMargin(1.5f, 0));
        BarSlot->SetVerticalAlignment(VAlign_Bottom);
        Bars.Add(Bar);
    }
    USizeBox* EqualizerSize = WidgetTree->ConstructWidget<USizeBox>();
    EqualizerSize->SetHeightOverride(52.0f);
    EqualizerSize->AddChild(Equalizer);
    StripRow->AddChildToHorizontalBox(EqualizerSize)->SetVerticalAlignment(VAlign_Center);
    UVerticalBox* TrackBox = WidgetTree->ConstructWidget<UVerticalBox>();
    TrackBox->AddChildToVerticalBox(MakeText(TEXT("СЕЙЧАС ИГРАЕТ"), 13, GratiaDim, TEXT("ExtraBold")));
    TrackText = MakeText(TEXT("—"), 21, GratiaInk, TEXT("Bold"));
    TrackText->SetClipping(EWidgetClipping::ClipToBounds);
    USizeBox* TrackSize = WidgetTree->ConstructWidget<USizeBox>();
    TrackSize->SetWidthOverride(330.0f);
    TrackSize->AddChild(TrackText);
    TrackBox->AddChildToVerticalBox(TrackSize);
    StripRow->AddChildToHorizontalBox(TrackBox)->SetPadding(FMargin(18, 0, 10, 0));
    AddButton(StripRow, TEXT("«"), EGratiaMenuAction::TrackPrev, 0, INDEX_NONE, false, 64.0f, 64.0f, 30);
    AddButton(StripRow, TEXT("»"), EGratiaMenuAction::TrackNext, 0, INDEX_NONE, false, 64.0f, 64.0f, 30);
    HeaderRow->AddChildToHorizontalBox(Strip)->SetVerticalAlignment(VAlign_Center);

    // Page title and status.
    UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    Content->AddChildToVerticalBox(TitleRow)->SetPadding(FMargin(4, 14, 0, 10));
    Header = MakeText(GratiaPageNames[Scenes], 38, GratiaInk, TEXT("ExtraBold"));
    TitleRow->AddChildToHorizontalBox(Header)->SetVerticalAlignment(VAlign_Bottom);
    StatusText = MakeText(TEXT(""), 18, GratiaPink, TEXT("Bold"));
    StatusText->SetVisibility(ESlateVisibility::Collapsed);
    TitleRow->AddChildToHorizontalBox(StatusText)->SetPadding(FMargin(24, 0, 0, 8));

    Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>();
    Content->AddChildToVerticalBox(Pages)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Pages->AddChild(BuildScenesPage());
    Pages->AddChild(BuildPlaybackPage());
    Pages->AddChild(BuildCharacterPage());
    Pages->AddChild(BuildPhysicsPage());
    Pages->AddChild(BuildSettingsPage());

    // Dock: tabs, lobby, close.
    UHorizontalBox* Dock = WidgetTree->ConstructWidget<UHorizontalBox>();
    UVerticalBoxSlot* DockSlot = Content->AddChildToVerticalBox(Dock);
    DockSlot->SetHorizontalAlignment(HAlign_Center);
    DockSlot->SetPadding(FMargin(0, 14, 0, 0));
    for (int32 Page = 0; Page < PageCount; ++Page) AddButton(Dock, GratiaPageNames[Page], EGratiaMenuAction::Tab, Page, INDEX_NONE, false, 200.0f, 62.0f, 22);
    Dock->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())->SetPadding(FMargin(18, 0));
    AddButton(Dock, TEXT("В лобби"), EGratiaMenuAction::Lobby, 0, INDEX_NONE, false, 180.0f, 62.0f, 22);
    AddButton(Dock, TEXT("×"), EGratiaMenuAction::Close, 0, INDEX_NONE, false, 62.0f, 62.0f, 32);
    ShowPage(Scenes);
}

UPanelWidget* UGratiaMenuWidget::BuildScenesPage()
{
    // A carousel of hexagon pictures (ViRo store art), scrolled sideways.
    UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    Scroll->SetOrientation(Orient_Horizontal);
    Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
    SceneScroll = Scroll;
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
    Scroll->AddChild(Row);
    const UGratiaSceneLibrary* Library = GratiaLibraryOf(Menu);
    if (!Library) { Row->AddChildToHorizontalBox(MakeText(TEXT("Библиотека сцен недоступна"), 24, GratiaDim)); return Scroll; }
    for (int32 Index = 0; Index < Library->Scenes.Num(); ++Index)
    {
        const FGratiaSceneEntry& Entry = Library->Scenes[Index];
        UVerticalBox* Card = WidgetTree->ConstructWidget<UVerticalBox>();
        UButton* Picture = WidgetTree->ConstructWidget<UButton>();
        const FVector2D PictureSize(300.0f, 260.0f);
        const int32 ItemIndex = Items.Num();
        FItem& Item = Items.AddDefaulted_GetRef();
        Item.Button = Picture; Item.Action = EGratiaMenuAction::StartScene; Item.Param = Index; Item.Page = Scenes; Item.bCard = true;
        Item.Accent = Entry.Accent; Item.Size = PictureSize;
        UTexture2D* Thumbnail = Entry.Thumbnail.LoadSynchronous();
        if (Library->HexMaterial)
        {
            UMaterialInstanceDynamic* Hex = MakeMaterial(Library->HexMaterial);
            Hex->SetVectorParameterValue(TEXT("Accent"), Entry.Accent);
            Hex->SetScalarParameterValue(TEXT("HasImage"), Thumbnail ? 1.0f : 0.0f);
            if (Thumbnail) Hex->SetTextureParameterValue(TEXT("Image"), Thumbnail);
            FButtonStyle Style;
            const FSlateBrush Brush = GratiaMaterialBrush(Hex, PictureSize);
            Style.SetNormal(Brush); Style.SetHovered(Brush); Style.SetPressed(Brush); Style.SetDisabled(Brush);
            Style.SetNormalPadding(FMargin(0)); Style.SetPressedPadding(FMargin(0));
            Picture->SetStyle(Style);
            Item.Normal = Hex;
        }
        else
        {
            FButtonStyle Style;
            FSlateBrush Brush = GratiaRounded(FLinearColor::White, 24.0f, Entry.Accent, 3.0f);
            if (Thumbnail) Brush.SetResourceObject(Thumbnail);
            else Brush.TintColor = FSlateColor(Entry.Accent * 0.4f);
            Style.SetNormal(Brush); Style.SetHovered(Brush); Style.SetPressed(Brush);
            Picture->SetStyle(Style);
        }
        BindClick(Picture, EGratiaMenuAction::StartScene, Index, ItemIndex);
        USizeBox* PictureBox = WidgetTree->ConstructWidget<USizeBox>();
        PictureBox->SetWidthOverride(PictureSize.X);
        PictureBox->SetHeightOverride(PictureSize.Y);
        PictureBox->AddChild(Picture);
        Card->AddChildToVerticalBox(PictureBox)->SetHorizontalAlignment(HAlign_Center);
        UTextBlock* Title = MakeText(Entry.Title.ToString(), 24, GratiaInk, TEXT("Black"));
        Title->SetJustification(ETextJustify::Center);
        Title->SetAutoWrapText(true);
        USizeBox* TitleSize = WidgetTree->ConstructWidget<USizeBox>();
        TitleSize->SetWidthOverride(PictureSize.X - 10.0f);
        TitleSize->AddChild(Title);
        Card->AddChildToVerticalBox(TitleSize)->SetPadding(FMargin(0, 10, 0, 0));
        UTextBlock* Description = MakeText(Entry.Description.ToString(), 16, GratiaDim, TEXT("Bold"));
        Description->SetJustification(ETextJustify::Center);
        Description->SetAutoWrapText(true);
        USizeBox* DescriptionSize = WidgetTree->ConstructWidget<USizeBox>();
        DescriptionSize->SetWidthOverride(290.0f);
        DescriptionSize->AddChild(Description);
        Card->AddChildToVerticalBox(DescriptionSize)->SetHorizontalAlignment(HAlign_Center);
        Row->AddChildToHorizontalBox(Card)->SetPadding(FMargin(0, 8, 34, 0));
    }
    return Scroll;
}

UPanelWidget* UGratiaMenuWidget::BuildPlaybackPage()
{
    UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();
    PlaybackTitle = MakeText(TEXT(""), 34, GratiaInk, TEXT("Black"));
    Page->AddChildToVerticalBox(PlaybackTitle)->SetPadding(FMargin(4, 0, 0, 4));
    PlaybackText = MakeText(TEXT(""), 24, GratiaPink, TEXT("Bold"));
    Page->AddChildToVerticalBox(PlaybackText)->SetPadding(FMargin(4, 0, 0, 20));
    UHorizontalBox* Transport = WidgetTree->ConstructWidget<UHorizontalBox>();
    Page->AddChildToVerticalBox(Transport);
    AddButton(Transport, TEXT("« Часть"), EGratiaMenuAction::PrevPart, 0, Playback, false, 220.0f);
    AddButton(Transport, TEXT("Пауза"), EGratiaMenuAction::Pause, 0, Playback, true, 260.0f);
    AddButton(Transport, TEXT("Часть »"), EGratiaMenuAction::NextPart, 0, Playback, false, 220.0f);
    AddButton(Transport, TEXT("Сначала"), EGratiaMenuAction::Restart, 0, Playback, false, 220.0f);
    UHorizontalBox* Speed = WidgetTree->ConstructWidget<UHorizontalBox>();
    Page->AddChildToVerticalBox(Speed)->SetPadding(FMargin(0, 6));
    AddButton(Speed, TEXT("Медленнее"), EGratiaMenuAction::SpeedDown, 0, Playback, false, 220.0f);
    AddButton(Speed, TEXT("Быстрее"), EGratiaMenuAction::SpeedUp, 0, Playback, false, 220.0f);
    AddButton(Speed, TEXT("Вид"), EGratiaMenuAction::PartnerView, 0, Playback, true, 480.0f);
    return Page;
}

UPanelWidget* UGratiaMenuWidget::BuildCharacterPage()
{
    UWrapBox* Page = WidgetTree->ConstructWidget<UWrapBox>();
    Page->SetInnerSlotPadding(FVector2D(14, 14));
    AddButton(Page, TEXT("Поза"), EGratiaMenuAction::Pose, 0, Character, true, 700.0f);
    AddButton(Page, TEXT("Демо реакций"), EGratiaMenuAction::Demo, 0, Character, true, 700.0f);
    AddButton(Page, TEXT("Спокойная"), EGratiaMenuAction::Mood, 0, Character, false, 460.0f);
    AddButton(Page, TEXT("Радостная"), EGratiaMenuAction::Mood, 1, Character, false, 460.0f);
    AddButton(Page, TEXT("Сдержанная"), EGratiaMenuAction::Mood, 2, Character, false, 460.0f);
    AddButton(Page, TEXT("Сбросить позу, контакты и высоту"), EGratiaMenuAction::Reset, 0, Character, false, 700.0f);
    AddButton(Page, TEXT("Примитив"), EGratiaMenuAction::Primitive, 0, Character, true, 700.0f);
    AddButton(Page, TEXT("Размер"), EGratiaMenuAction::PrimitiveSize, 0, Character, true, 700.0f);
    return Page;
}

UPanelWidget* UGratiaMenuWidget::BuildPhysicsPage()
{
    UWrapBox* Page = WidgetTree->ConstructWidget<UWrapBox>();
    Page->SetInnerSlotPadding(FVector2D(14, 14));
    AddButton(Page, TEXT("Волосы"), EGratiaMenuAction::Hair, 0, Physics, true, 700.0f);
    AddButton(Page, TEXT("Одежда"), EGratiaMenuAction::Cloth, 0, Physics, true, 700.0f);
    AddButton(Page, TEXT("Тело"), EGratiaMenuAction::Body, 0, Physics, true, 700.0f);
    AddButton(Page, TEXT("Уши / хвост"), EGratiaMenuAction::Ears, 0, Physics, true, 700.0f);
    AddButton(Page, TEXT("Вторичная физика"), EGratiaMenuAction::Physics, 0, Physics, true, 700.0f);
    AddButton(Page, TEXT("Локальные пружины"), EGratiaMenuAction::Springs, 0, Physics, true, 700.0f);
    return Page;
}

UPanelWidget* UGratiaMenuWidget::BuildSettingsPage()
{
    UWrapBox* Page = WidgetTree->ConstructWidget<UWrapBox>();
    Page->SetInnerSlotPadding(FVector2D(14, 12));
    AddButton(Page, TEXT("Низкое"), EGratiaMenuAction::Quality, 0, Settings, false, 300.0f);
    AddButton(Page, TEXT("Среднее"), EGratiaMenuAction::Quality, 1, Settings, false, 300.0f);
    AddButton(Page, TEXT("Высокое"), EGratiaMenuAction::Quality, 2, Settings, false, 300.0f);
    AddButton(Page, TEXT("Звук"), EGratiaMenuAction::Sound, 0, Settings, true, 460.0f);
    auto Stepper = [this, Page](EGratiaMenuAction Down, EGratiaMenuAction Up)
    {
        AddButton(Page, TEXT("−"), Down, 0, Settings, false, 64.0f, 64.0f, 30);
        AddButton(Page, TEXT(""), Up, 0, Settings, true, 620.0f);
    };
    Stepper(EGratiaMenuAction::MusicDown, EGratiaMenuAction::MusicUp);
    Stepper(EGratiaMenuAction::HapticsDown, EGratiaMenuAction::HapticsUp);
    Stepper(EGratiaMenuAction::HeightDown, EGratiaMenuAction::HeightUp);
    AddButton(Page, TEXT("Центрировать взгляд (клик стиком)"), EGratiaMenuAction::Recenter, 0, Settings, false, 700.0f);
    AddButton(Page, TEXT("Выход из игры"), EGratiaMenuAction::Quit, 0, Settings, false, 700.0f);
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
        if (Item.Label.IsValid()) Item.Label->SetColorAndOpacity(FSlateColor(Button->GetIsEnabled() ? GratiaInk : GratiaDim * 0.8f));
        // The lobby button only exists inside a scene.
        if (Item.Action == EGratiaMenuAction::Lobby)
            Button->SetVisibility(Menu->IsInScene() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        const bool bActive = Item.bCard ? Item.Param == Menu->GetCurrentScene()
            : Item.Action == EGratiaMenuAction::Tab ? Item.Param == CurrentPage : Menu->IsSelected(Item.Action, Item.Param);
        StyleButton(Item, bActive, I == Focus);
    }
}

void UGratiaMenuWidget::Hover(int32 Index, bool bHovered)
{
    if (Items.IsValidIndex(Index)) Items[Index].bHovered = bHovered;
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
    if (PanelInstance) PanelInstance->SetScalarParameterValue(TEXT("Beat"), Music.W);
    const FString Status = Director ? Director->GetLastError() : FString();
    if (StatusText && Status != LastStatus)
    {
        LastStatus = Status;
        StatusText->SetText(FText::FromString(Status));
        StatusText->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    }
    const FString Track = Director ? Director->GetTrackText() : FString();
    if (TrackText && Track != LastTrack)
    {
        LastTrack = Track;
        TrackText->SetText(FText::FromString(Track.IsEmpty() ? TEXT("тишина") : Track));
    }
    for (int32 I = 0; I < Bars.Num(); ++I)
    {
        // Left bars follow the bass, the middle the mids, the right the highs; a ripple on top.
        const float T = I / float(FMath::Max(1, Bars.Num() - 1));
        const float Band = T < 0.33f ? Music.X : T < 0.66f ? Music.Y : Music.Z;
        const float Ripple = 0.5f + 0.5f * FMath::Sin(Time * 7.0f + I * 1.3f);
        Bars[I]->SetHeightOverride(5.0f + 46.0f * FMath::Clamp(Band * (0.6f + 0.4f * Ripple) + 0.25f * Music.W, 0.0f, 1.0f));
    }
    // Scene pictures swell and glow while pointed at.
    for (int32 I = 0; I < Items.Num(); ++I)
    {
        FItem& Item = Items[I];
        if (!Item.bCard || !Item.Button.IsValid()) continue;
        const float Target = Item.bHovered || I == Focus ? 1.0f : 0.0f;
        Item.HoverAmount = FMath::FInterpTo(Item.HoverAmount, Target, DeltaSeconds, 12.0f);
        if (Item.Normal.IsValid()) Item.Normal->SetScalarParameterValue(TEXT("Hover"), Item.HoverAmount);
        Item.Button->SetRenderScale(FVector2D(1.0f + 0.06f * Item.HoverAmount));
    }
    if (CurrentPage == Playback && Director && PlaybackText)
    {
        const FGratiaSceneEntry* Entry = Director->GetCurrentEntry();
        PlaybackTitle->SetText(Entry ? Entry->Title : FText::FromString(TEXT("Нет сцены")));
        PlaybackText->SetText(FText::FromString(Director->GetPlaybackText()));
    }
}
