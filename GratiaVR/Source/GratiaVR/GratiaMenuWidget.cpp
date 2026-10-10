#include "GratiaMenuWidget.h"
#include "GratiaMenu.h"
#include "GratiaSceneDirector.h"
#include "GratiaSceneLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"

namespace
{
// Palette, linear (sRGB in comments): night glass, raised surfaces, controls, neon pink/cyan, white ink.
const FLinearColor MenuGlass(0.0080f, 0.0037f, 0.0210f, 0.96f);     // 22 12 40
const FLinearColor MenuSurface(0.0210f, 0.0080f, 0.0610f, 0.92f);   // 40 22 70
const FLinearColor MenuSurfaceHover(0.0420f, 0.0140f, 0.1220f, 0.96f);
const FLinearColor MenuWell(0.0050f, 0.0024f, 0.0140f, 0.90f);      // segmented control background
const FLinearColor MenuControl(0.0480f, 0.0180f, 0.1500f, 1.0f);    // 62 36 108
const FLinearColor MenuControlHover(0.1070f, 0.0340f, 0.3050f, 1.0f);
const FLinearColor MenuRowHover(0.0610f, 0.0210f, 0.1880f, 0.70f);
const FLinearColor MenuLine(0.1550f, 0.0610f, 0.4020f, 0.60f);      // 110 70 170
const FLinearColor MenuEdge(0.4500f, 0.0500f, 0.5500f, 1.0f);
const FLinearColor MenuInk(1.0f, 0.92f, 0.97f);
const FLinearColor MenuSoft(0.644f, 0.552f, 0.838f);                // 210 196 236
const FLinearColor MenuDim(0.305f, 0.231f, 0.479f);                 // 150 132 184
const FLinearColor MenuPink(1.0f, 0.027f, 0.254f);                  // 255 46 138
const FLinearColor MenuPinkHover(1.0f, 0.127f, 0.402f);
const FLinearColor MenuCyan(0.045f, 0.578f, 1.0f);
const FLinearColor MenuOff(0.0610f, 0.0340f, 0.1270f);
const FLinearColor MenuClear(0.0f, 0.0f, 0.0f, 0.0f);

FSlateBrush MenuRounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline = MenuClear, float Width = 0.0f)
{
    FSlateBrush Brush;
    Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
    Brush.TintColor = FSlateColor(Fill);
    Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius), FSlateColor(Outline), Width);
    Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
    return Brush;
}

FSlateBrush MenuPicture(UTexture2D* Texture, const FVector2D& Size, float Radius)
{
    FSlateBrush Brush = MenuRounded(FLinearColor::White, Radius);
    Brush.SetResourceObject(Texture);
    Brush.ImageSize = Size;
    return Brush;
}

struct FGratiaPageInfo { const TCHAR* Name; const TCHAR* Hint; };
const FGratiaPageInfo GratiaPages[] = {
    {TEXT("Сцены"), TEXT("Выберите место или шоу — сцена откроется с плавным переходом")},
    {TEXT("Шоу"), TEXT("Пауза, части, скорость и вид глазами партнёра")},
    {TEXT("Персонаж"), TEXT("Настроение, поза, демонстрация реакций и примитив")},
    {TEXT("Физика"), TEXT("Движение волос, одежды и тела — выключайте, если не хватает кадров")},
    {TEXT("Настройки"), TEXT("Графика, звук и вибрация сохраняются между запусками")},
    {TEXT("Игрок"), TEXT("Поворот, ходьба, рост и подсказка по управлению")},
};
static_assert(UE_ARRAY_COUNT(GratiaPages) == UGratiaMenuWidget::PageCount, "Every page needs a name and a hint");
/** Order of the pages in the navigation rail. */
const int32 GratiaRailOrder[] = {UGratiaMenuWidget::Scenes, UGratiaMenuWidget::Playback, UGratiaMenuWidget::Character,
    UGratiaMenuWidget::Physics, UGratiaMenuWidget::Player, UGratiaMenuWidget::Settings};

const UGratiaSceneLibrary* GratiaLibraryOf(const TWeakObjectPtr<UGratiaMenu>& Menu)
{
    const UGratiaSceneDirector* Director = Menu.IsValid() ? Menu->GetDirector() : nullptr;
    return Director ? Director->Library.Get() : nullptr;
}

void GratiaButtonContent(UButton* Button, UWidget* Content, const FMargin& Padding, EHorizontalAlignment Horizontal = HAlign_Fill,
    EVerticalAlignment Vertical = VAlign_Center)
{
    if (UButtonSlot* Slot = Cast<UButtonSlot>(Button->AddChild(Content)))
    {
        Slot->SetPadding(Padding);
        Slot->SetHorizontalAlignment(Horizontal);
        Slot->SetVerticalAlignment(Vertical);
    }
}

void GratiaPlace(UPanelWidget* Parent, UWidget* Child, const FMargin& Padding, bool bFill = false)
{
    UPanelSlot* Slot = Parent->AddChild(Child);
    if (UHorizontalBoxSlot* Horizontal = Cast<UHorizontalBoxSlot>(Slot))
    {
        Horizontal->SetPadding(Padding);
        Horizontal->SetVerticalAlignment(VAlign_Center);
        if (bFill) Horizontal->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
    else if (UVerticalBoxSlot* Vertical = Cast<UVerticalBoxSlot>(Slot))
    {
        Vertical->SetPadding(Padding);
        if (bFill) Vertical->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
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

UWidget* UGratiaMenuWidget::Sized(UWidget* Content, float Width, float Height)
{
    USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
    if (Width > 0.0f) Box->SetWidthOverride(Width);
    if (Height > 0.0f) Box->SetHeightOverride(Height);
    Box->AddChild(Content);
    return Box;
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

UButton* UGratiaMenuWidget::MakeButton(int32& OutItem, EKind Kind, EGratiaMenuAction Action, int32 Param, int32 Page, float Radius)
{
    UButton* Button = WidgetTree->ConstructWidget<UButton>();
    Button->SetClickMethod(EButtonClickMethod::MouseDown);
    OutItem = Items.Num();
    FItem& Item = Items.AddDefaulted_GetRef();
    Item.Button = Button; Item.Kind = Kind; Item.Action = Action; Item.Param = Param; Item.Page = Page; Item.Radius = Radius;
    BindClick(Button, Action, Param, OutItem);
    StyleItem(Item, true, false, false);
    return Button;
}

UVerticalBox* UGratiaMenuWidget::AddSection(UPanelWidget* Column, const FString& Title, bool bFill)
{
    UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
    Card->SetBrush(MenuRounded(MenuSurface, 26.0f, MenuLine, 1.5f));
    Card->SetPadding(FMargin(24, 16, 24, 16));
    UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>();
    Card->SetContent(Body);
    if (!Title.IsEmpty())
    {
        UTextBlock* Caption = MakeText(Title, 17, MenuPink, TEXT("Black"));
        FSlateFontInfo CaptionFont = Caption->GetFont();
        CaptionFont.LetterSpacing = 160;
        Caption->SetFont(CaptionFont);
        Body->AddChildToVerticalBox(Caption)->SetPadding(FMargin(2, 0, 0, 6));
    }
    GratiaPlace(Column, Card, FMargin(0, 0, 0, 16), bFill);
    return Body;
}

void UGratiaMenuWidget::MakeColumns(UPanelWidget* Page, UVerticalBox*& Left, UVerticalBox*& Right)
{
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
    GratiaPlace(Page, Row, FMargin(0), true);
    Left = WidgetTree->ConstructWidget<UVerticalBox>();
    Right = WidgetTree->ConstructWidget<UVerticalBox>();
    for (UVerticalBox* Column : {Left, Right})
    {
        UHorizontalBoxSlot* ColumnSlot = Row->AddChildToHorizontalBox(Column);
        ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ColumnSlot->SetVerticalAlignment(VAlign_Fill);
        ColumnSlot->SetPadding(FMargin(Column == Left ? 0 : 11, 0, Column == Left ? 11 : 0, 0));
    }
}

UWidget* UGratiaMenuWidget::MakeLabel(const FString& Label, const FString& Hint)
{
    UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
    Box->AddChildToVerticalBox(MakeText(Label, 24, MenuInk, TEXT("Bold")));
    if (!Hint.IsEmpty())
    {
        UTextBlock* Small = MakeText(Hint, 17, MenuSoft, TEXT("Regular"));
        Small->SetAutoWrapText(true);
        Box->AddChildToVerticalBox(Small)->SetPadding(FMargin(0, 1, 0, 0));
    }
    return Box;
}

UHorizontalBox* UGratiaMenuWidget::AddRow(UVerticalBox* Section, const FString& Label, const FString& Hint)
{
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
    GratiaPlace(Row, MakeLabel(Label, Hint), FMargin(0, 0, 16, 0), true);
    // At least a control's height; a wrapped hint makes the row taller instead of running into the next one.
    USizeBox* Height = WidgetTree->ConstructWidget<USizeBox>();
    Height->SetMinDesiredHeight(64.0f);
    Height->AddChild(Row);
    GratiaPlace(Section, Height, FMargin(0, 4));
    return Row;
}

void UGratiaMenuWidget::AddTextButton(UPanelWidget* Parent, const FString& Text, EGratiaMenuAction Action, int32 Param, int32 Page, float Width,
    float Height, int32 FontSize, bool bDynamic)
{
    int32 Index = INDEX_NONE;
    UButton* Button = MakeButton(Index, EKind::Button, Action, Param, Page, Height * 0.5f);
    UTextBlock* Label = MakeText(Text, FontSize, MenuInk, TEXT("ExtraBold"));
    Label->SetJustification(ETextJustify::Center);
    GratiaButtonContent(Button, Label, FMargin(16, 0), HAlign_Center);
    Items[Index].Label = Label;
    Items[Index].bDynamicLabel = bDynamic;
    const bool bHorizontal = Cast<UHorizontalBox>(Parent) != nullptr;
    GratiaPlace(Parent, Sized(Button, Width, Height), bHorizontal ? FMargin(0, 0, 14, 0) : FMargin(0, 4), Width <= 0.0f && bHorizontal);
}

void UGratiaMenuWidget::AddToggle(UVerticalBox* Section, const FString& Label, const FString& Hint, EGratiaMenuAction Action, int32 Page)
{
    int32 Index = INDEX_NONE;
    UButton* Button = MakeButton(Index, EKind::Toggle, Action, 0, Page, 22.0f);
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
    UVerticalBox* Text = WidgetTree->ConstructWidget<UVerticalBox>();
    UTextBlock* Name = MakeText(Label, 24, MenuInk, TEXT("Bold"));
    Text->AddChildToVerticalBox(Name);
    if (!Hint.IsEmpty())
    {
        UTextBlock* Small = MakeText(Hint, 17, MenuSoft, TEXT("Regular"));
        Small->SetAutoWrapText(true);
        Text->AddChildToVerticalBox(Small)->SetPadding(FMargin(0, 1, 0, 0));
    }
    GratiaPlace(Row, Text, FMargin(0, 0, 16, 0), true);
    // Switch: a pill track with a round knob that slides right when on.
    UBorder* Track = WidgetTree->ConstructWidget<UBorder>();
    Track->SetPadding(FMargin(5));
    Track->SetVerticalAlignment(VAlign_Center);
    UBorder* Knob = WidgetTree->ConstructWidget<UBorder>();
    Knob->SetPadding(FMargin(0));
    Knob->SetContent(Sized(WidgetTree->ConstructWidget<USpacer>(), 30.0f, 30.0f));
    Track->SetContent(Knob);
    GratiaPlace(Row, Sized(Track, 80.0f, 40.0f), FMargin(0));
    GratiaButtonContent(Button, Row, FMargin(14, 8, 12, 8));
    FItem& Item = Items[Index];
    Item.Label = Name; Item.Track = Track; Item.Knob = Knob;
    StyleItem(Item, true, false, false);
    // The row is the button; it reaches into the section padding so its text lines up with the other rows.
    USizeBox* Height = WidgetTree->ConstructWidget<USizeBox>();
    Height->SetMinDesiredHeight(68.0f);
    Height->AddChild(Button);
    GratiaPlace(Section, Height, FMargin(-14, 2, -12, 2));
}

void UGratiaMenuWidget::AddSegments(UPanelWidget* Parent, const TArray<FString>& Labels, EGratiaMenuAction Action, int32 Page, float Width)
{
    UBorder* Well = WidgetTree->ConstructWidget<UBorder>();
    Well->SetBrush(MenuRounded(MenuWell, 30.0f, MenuLine, 1.5f));
    Well->SetPadding(FMargin(4));
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
    Well->SetContent(Row);
    for (int32 Option = 0; Option < Labels.Num(); ++Option)
    {
        int32 Index = INDEX_NONE;
        UButton* Button = MakeButton(Index, EKind::Segment, Action, Option, Page, 26.0f);
        UTextBlock* Label = MakeText(Labels[Option], 21, MenuInk, TEXT("ExtraBold"));
        Label->SetJustification(ETextJustify::Center);
        GratiaButtonContent(Button, Label, FMargin(8, 0), HAlign_Center);
        Items[Index].Label = Label;
        GratiaPlace(Row, Sized(Button, -1.0f, 52.0f), FMargin(Option ? 2 : 0, 0, 0, 0), true);
    }
    const bool bHorizontal = Cast<UHorizontalBox>(Parent) != nullptr;
    GratiaPlace(Parent, Sized(Well, Width, 60.0f), bHorizontal ? FMargin(0) : FMargin(0, 4, 0, 8));
}

void UGratiaMenuWidget::AddStepper(UVerticalBox* Section, const FString& Label, const FString& Hint, EGratiaMenuAction Down, EGratiaMenuAction Up, int32 Page)
{
    UHorizontalBox* Row = AddRow(Section, Label, Hint);
    auto Round = [this, Row, Page](const TCHAR* Glyph, EGratiaMenuAction Action)
    {
        int32 Index = INDEX_NONE;
        UButton* Button = MakeButton(Index, EKind::Round, Action, 0, Page, 28.0f);
        UTextBlock* Text = MakeText(Glyph, 32, MenuInk, TEXT("Black"));
        Text->SetJustification(ETextJustify::Center);
        GratiaButtonContent(Button, Text, FMargin(0, 0, 0, 3), HAlign_Center);
        Items[Index].Label = Text;
        GratiaPlace(Row, Sized(Button, 56.0f, 56.0f), FMargin(0));
    };
    Round(TEXT("−"), Down);
    UTextBlock* Value = MakeText(TEXT("—"), 24, MenuInk, TEXT("ExtraBold"));
    Value->SetJustification(ETextJustify::Center);
    GratiaPlace(Row, Sized(Value, 128.0f, -1.0f), FMargin(4, 0));
    Values.Add({Value, Up});
    Round(TEXT("+"), Up);
}

void UGratiaMenuWidget::AddCycle(UVerticalBox* Section, const FString& Label, const FString& Hint, EGratiaMenuAction Action, int32 Page)
{
    UHorizontalBox* Row = AddRow(Section, Label, Hint);
    int32 Index = INDEX_NONE;
    UButton* Button = MakeButton(Index, EKind::Cycle, Action, 0, Page, 28.0f);
    UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>();
    UTextBlock* Value = MakeText(TEXT("—"), 21, MenuInk, TEXT("ExtraBold"));
    Value->SetClipping(EWidgetClipping::ClipToBounds);
    GratiaPlace(Content, Value, FMargin(0), true);
    GratiaPlace(Content, MakeText(TEXT("›"), 30, MenuPink, TEXT("Black")), FMargin(10, 0, 0, 3));
    GratiaButtonContent(Button, Content, FMargin(22, 0, 18, 0));
    Items[Index].Value = Value;
    GratiaPlace(Row, Sized(Button, 280.0f, 56.0f), FMargin(0));
}

UWidget* UGratiaMenuWidget::MakeEmptyState(const FString& Title, const FString& Text)
{
    UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
    // A soft ring as the empty-state mark.
    UBorder* Ring = WidgetTree->ConstructWidget<UBorder>();
    Ring->SetBrush(MenuRounded(MenuSurface, 44.0f, MenuPink, 3.0f));
    Ring->SetHorizontalAlignment(HAlign_Center);
    Ring->SetVerticalAlignment(VAlign_Center);
    Ring->SetContent(MakeText(TEXT("i"), 40, MenuPink, TEXT("Black")));
    Box->AddChildToVerticalBox(Sized(Ring, 88.0f, 88.0f))->SetHorizontalAlignment(HAlign_Center);
    UTextBlock* Heading = MakeText(Title, 32, MenuInk, TEXT("Black"));
    Heading->SetJustification(ETextJustify::Center);
    Box->AddChildToVerticalBox(Heading)->SetPadding(FMargin(0, 22, 0, 8));
    UTextBlock* Body = MakeText(Text, 21, MenuSoft, TEXT("Regular"));
    Body->SetJustification(ETextJustify::Center);
    Body->SetAutoWrapText(true);
    UVerticalBoxSlot* BodySlot = Box->AddChildToVerticalBox(Sized(Body, 760.0f, -1.0f));
    BodySlot->SetHorizontalAlignment(HAlign_Center);
    return Box;
}

void UGratiaMenuWidget::BuildTree()
{
    Items.Reset(); Values.Reset(); Empties.Reset(); Bars.Reset(); Clicks.Reset();
    USizeBox* RootSize = WidgetTree->ConstructWidget<USizeBox>();
    RootSize->SetWidthOverride(PanelWidth);
    RootSize->SetHeightOverride(PanelHeight);
    WidgetTree->RootWidget = RootSize;
    // Panel: dark violet glass with a neon edge (it glows on the beat) and a faint inner rim.
    PanelEdge = WidgetTree->ConstructWidget<UBorder>();
    PanelEdge->SetBrush(MenuRounded(MenuGlass, 46.0f, MenuEdge, 3.0f));
    PanelEdge->SetPadding(FMargin(5));
    RootSize->AddChild(PanelEdge);
    UBorder* Rim = WidgetTree->ConstructWidget<UBorder>();
    Rim->SetBrush(MenuRounded(MenuClear, 41.0f, FLinearColor(1.0f, 0.8f, 1.0f, 0.07f), 1.5f));
    Rim->SetPadding(FMargin(38, 26, 38, 30));
    PanelEdge->SetContent(Rim);
    UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
    Rim->SetContent(Content);

    // Top bar: logo, now playing, close.
    UHorizontalBox* TopBar = WidgetTree->ConstructWidget<UHorizontalBox>();
    GratiaPlace(Content, Sized(TopBar, -1.0f, 78.0f), FMargin(0, 0, 0, 18));
    UHorizontalBox* Logo = WidgetTree->ConstructWidget<UHorizontalBox>();
    UTextBlock* Name = MakeText(TEXT("GRATIA"), 50, MenuInk, TEXT("Black"));
    FSlateFontInfo NameFont = Name->GetFont();
    NameFont.OutlineSettings.OutlineSize = 2;
    NameFont.OutlineSettings.OutlineColor = MenuPink;
    NameFont.LetterSpacing = 110;
    Name->SetFont(NameFont);
    GratiaPlace(Logo, Name, FMargin(4, 0, 12, 0));
    UTextBlock* Space = MakeText(TEXT("playspace"), 26, MenuPink, TEXT("ExtraBold"));
    Logo->AddChildToHorizontalBox(Space)->SetVerticalAlignment(VAlign_Bottom);
    Cast<UHorizontalBoxSlot>(Space->Slot)->SetPadding(FMargin(0, 0, 0, 14));
    GratiaPlace(TopBar, Logo, FMargin(0), true);
    // Now playing: equalizer, track, previous/next.
    UBorder* Strip = WidgetTree->ConstructWidget<UBorder>();
    Strip->SetBrush(MenuRounded(MenuSurface, 36.0f, MenuLine, 1.5f));
    Strip->SetPadding(FMargin(22, 6, 8, 6));
    UHorizontalBox* StripRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    Strip->SetContent(StripRow);
    UHorizontalBox* Equalizer = WidgetTree->ConstructWidget<UHorizontalBox>();
    for (int32 I = 0; I < 14; ++I)
    {
        USizeBox* Bar = WidgetTree->ConstructWidget<USizeBox>();
        Bar->SetWidthOverride(6.0f);
        Bar->SetHeightOverride(6.0f);
        UBorder* Fill = WidgetTree->ConstructWidget<UBorder>();
        Fill->SetBrush(MenuRounded(FLinearColor::LerpUsingHSV(MenuPink, MenuCyan, I / 13.0f), 3.0f));
        Bar->AddChild(Fill);
        UHorizontalBoxSlot* BarSlot = Equalizer->AddChildToHorizontalBox(Bar);
        BarSlot->SetPadding(FMargin(1.5f, 0));
        BarSlot->SetVerticalAlignment(VAlign_Bottom);
        Bars.Add(Bar);
    }
    GratiaPlace(StripRow, Sized(Equalizer, -1.0f, 44.0f), FMargin(0));
    UVerticalBox* TrackBox = WidgetTree->ConstructWidget<UVerticalBox>();
    UTextBlock* NowLabel = MakeText(TEXT("СЕЙЧАС ИГРАЕТ"), 14, MenuSoft, TEXT("Black"));
    FSlateFontInfo NowFont = NowLabel->GetFont();
    NowFont.LetterSpacing = 140;
    NowLabel->SetFont(NowFont);
    TrackBox->AddChildToVerticalBox(NowLabel);
    TrackText = MakeText(TEXT("—"), 21, MenuInk, TEXT("Bold"));
    TrackText->SetClipping(EWidgetClipping::ClipToBounds);
    TrackBox->AddChildToVerticalBox(Sized(TrackText, 340.0f, -1.0f));
    GratiaPlace(StripRow, TrackBox, FMargin(18, 0, 12, 0));
    auto StripButton = [this, StripRow](const TCHAR* Glyph, EGratiaMenuAction Action)
    {
        int32 Index = INDEX_NONE;
        UButton* Button = MakeButton(Index, EKind::Round, Action, 0, INDEX_NONE, 26.0f);
        UTextBlock* Text = MakeText(Glyph, 26, MenuInk, TEXT("Black"));
        Text->SetJustification(ETextJustify::Center);
        GratiaButtonContent(Button, Text, FMargin(0, 0, 0, 3), HAlign_Center);
        Items[Index].Label = Text;
        GratiaPlace(StripRow, Sized(Button, 52.0f, 52.0f), FMargin(4, 0, 0, 0));
    };
    StripButton(TEXT("«"), EGratiaMenuAction::TrackPrev);
    StripButton(TEXT("»"), EGratiaMenuAction::TrackNext);
    GratiaPlace(TopBar, Strip, FMargin(0, 0, 18, 0));
    {
        int32 Index = INDEX_NONE;
        UButton* Close = MakeButton(Index, EKind::Round, EGratiaMenuAction::Close, 0, INDEX_NONE, 32.0f);
        UTextBlock* Cross = MakeText(TEXT("×"), 38, MenuInk, TEXT("Black"));
        Cross->SetJustification(ETextJustify::Center);
        GratiaButtonContent(Close, Cross, FMargin(0, 0, 0, 4), HAlign_Center);
        Items[Index].Label = Cross;
        GratiaPlace(TopBar, Sized(Close, 64.0f, 64.0f), FMargin(0));
    }

    // Body: navigation rail and the page.
    UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>();
    GratiaPlace(Content, Body, FMargin(0), true);
    UVerticalBox* Rail = WidgetTree->ConstructWidget<UVerticalBox>();
    UHorizontalBoxSlot* RailSlot = Body->AddChildToHorizontalBox(Sized(Rail, 252.0f, -1.0f));
    RailSlot->SetVerticalAlignment(VAlign_Fill);
    RailSlot->SetPadding(FMargin(0, 0, 26, 0));
    for (const int32 Page : GratiaRailOrder)
    {
        int32 Index = INDEX_NONE;
        UButton* Tab = MakeButton(Index, EKind::Nav, EGratiaMenuAction::Tab, Page, INDEX_NONE, 31.0f);
        UTextBlock* Label = MakeText(GratiaPages[Page].Name, 24, MenuSoft, TEXT("ExtraBold"));
        GratiaButtonContent(Tab, Label, FMargin(28, 0, 12, 0), HAlign_Left);
        Items[Index].Label = Label;
        GratiaPlace(Rail, Sized(Tab, -1.0f, 62.0f), FMargin(0, 0, 0, 8));
    }
    GratiaPlace(Rail, WidgetTree->ConstructWidget<USpacer>(), FMargin(0), true);
    AddTextButton(Rail, TEXT("В лобби"), EGratiaMenuAction::Lobby, 0, INDEX_NONE, -1.0f, 62.0f, 22);

    UVerticalBox* PageArea = WidgetTree->ConstructWidget<UVerticalBox>();
    UHorizontalBoxSlot* PageSlot = Body->AddChildToHorizontalBox(PageArea);
    PageSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    PageSlot->SetVerticalAlignment(VAlign_Fill);
    Header = MakeText(GratiaPages[Scenes].Name, 34, MenuInk, TEXT("Black"));
    PageArea->AddChildToVerticalBox(Header)->SetPadding(FMargin(2, -6, 0, 0));
    HeaderHint = MakeText(GratiaPages[Scenes].Hint, 19, MenuSoft, TEXT("Regular"));
    PageArea->AddChildToVerticalBox(HeaderHint)->SetPadding(FMargin(2, 0, 0, 0));
    StatusText = MakeText(TEXT(""), 19, MenuPink, TEXT("Bold"));
    StatusText->SetAutoWrapText(true);
    StatusText->SetVisibility(ESlateVisibility::Collapsed);
    PageArea->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(2, 4, 0, 0));
    Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>();
    GratiaPlace(PageArea, Pages, FMargin(0, 16, 0, 0), true);
    Pages->AddChild(BuildScenesPage());
    Pages->AddChild(BuildPlaybackPage());
    Pages->AddChild(BuildCharacterPage());
    Pages->AddChild(BuildPhysicsPage());
    Pages->AddChild(BuildSettingsPage());
    Pages->AddChild(BuildPlayerPage());
    check(Pages->GetChildrenCount() == PageCount);
    ShowPage(Scenes);
}

UPanelWidget* UGratiaMenuWidget::BuildScenesPage()
{
    UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();
    const UGratiaSceneLibrary* Library = GratiaLibraryOf(Menu);
    if (!Library || Library->Scenes.IsEmpty())
    {
        Page->AddChildToVerticalBox(MakeEmptyState(TEXT("Сцен нет"), TEXT("Библиотека сцен не загружена. Перезапустите игру.")))->SetHorizontalAlignment(HAlign_Center);
        return Page;
    }
    // Free play first, then the shows; three cards per row.
    TArray<int32> Order;
    for (int32 Index = 0; Index < Library->Scenes.Num(); ++Index) if (Library->Scenes[Index].Performance.IsNone()) Order.Add(Index);
    for (int32 Index = 0; Index < Library->Scenes.Num(); ++Index) if (!Library->Scenes[Index].Performance.IsNone()) Order.Add(Index);
    // Two rows of cards fit the page height; the pictures keep the 16:9 of the captures.
    const FVector2D PictureSize(320.0f, 180.0f);
    UHorizontalBox* Row = nullptr;
    for (int32 N = 0; N < Order.Num(); ++N)
    {
        if (N % 3 == 0)
        {
            Row = WidgetTree->ConstructWidget<UHorizontalBox>();
            GratiaPlace(Page, Row, FMargin(0, N ? 16 : 0, 0, 0));
        }
        const int32 SceneIndex = Order[N];
        const FGratiaSceneEntry& Entry = Library->Scenes[SceneIndex];
        const bool bShow = !Entry.Performance.IsNone();
        int32 Index = INDEX_NONE;
        UButton* Card = MakeButton(Index, EKind::Card, EGratiaMenuAction::StartScene, SceneIndex, Scenes, 26.0f);
        UVerticalBox* CardBody = WidgetTree->ConstructWidget<UVerticalBox>();
        UOverlay* Picture = WidgetTree->ConstructWidget<UOverlay>();
        UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
        UTexture2D* Thumbnail = Entry.Thumbnail.LoadSynchronous();
        Frame->SetBrush(Thumbnail ? MenuPicture(Thumbnail, PictureSize, 18.0f) : MenuRounded(Entry.Accent * 0.35f, 18.0f));
        Picture->AddChildToOverlay(Sized(Frame, PictureSize.X, PictureSize.Y));
        auto Badge = [this, Picture](const TCHAR* Text, const FLinearColor& Fill, EHorizontalAlignment Side)
        {
            UBorder* Pill = WidgetTree->ConstructWidget<UBorder>();
            Pill->SetBrush(MenuRounded(Fill, 14.0f));
            Pill->SetPadding(FMargin(12, 3, 12, 4));
            UTextBlock* Label = MakeText(Text, 15, MenuInk, TEXT("Black"));
            FSlateFontInfo LabelFont = Label->GetFont();
            LabelFont.LetterSpacing = 100;
            Label->SetFont(LabelFont);
            Pill->SetContent(Label);
            UOverlaySlot* BadgeSlot = Picture->AddChildToOverlay(Pill);
            BadgeSlot->SetHorizontalAlignment(Side);
            BadgeSlot->SetVerticalAlignment(VAlign_Top);
            BadgeSlot->SetPadding(FMargin(10));
            return Pill;
        };
        Badge(bShow ? TEXT("ШОУ") : TEXT("СВОБОДНАЯ ИГРА"), bShow ? MenuPink : FLinearColor(0.004f, 0.002f, 0.012f, 0.80f), HAlign_Left);
        UBorder* Now = Badge(TEXT("СЕЙЧАС"), MenuCyan * 0.85f, HAlign_Right);
        Now->SetVisibility(ESlateVisibility::Collapsed);
        CardBody->AddChildToVerticalBox(Picture);
        UTextBlock* Title = MakeText(Entry.Title.ToString(), 26, MenuInk, TEXT("Black"));
        Title->SetClipping(EWidgetClipping::ClipToBounds);
        CardBody->AddChildToVerticalBox(Title)->SetPadding(FMargin(4, 8, 4, 0));
        // Two lines for the description, the same height on every card.
        UTextBlock* Description = MakeText(Entry.Description.ToString(), 17, MenuSoft, TEXT("Regular"));
        Description->SetAutoWrapText(true);
        Description->SetClipping(EWidgetClipping::ClipToBounds);
        CardBody->AddChildToVerticalBox(Sized(Description, -1.0f, 56.0f))->SetPadding(FMargin(4, 0, 4, 0));
        GratiaButtonContent(Card, CardBody, FMargin(12, 12, 12, 8), HAlign_Fill, VAlign_Top);
        FItem& Item = Items[Index];
        Item.Accent = Entry.Accent; Item.Frame = Frame; Item.Badge = Now; Item.Label = Title;
        GratiaPlace(Row, Sized(Card, PictureSize.X + 24.0f, -1.0f), FMargin(0, 0, N % 3 == 2 ? 0 : 22, 0));
    }
    return Page;
}

UPanelWidget* UGratiaMenuWidget::BuildPlaybackPage()
{
    UOverlay* Page = WidgetTree->ConstructWidget<UOverlay>();
    UVerticalBox* Controls = WidgetTree->ConstructWidget<UVerticalBox>();
    UOverlaySlot* ControlsSlot = Page->AddChildToOverlay(Controls);
    ControlsSlot->SetHorizontalAlignment(HAlign_Fill);
    ControlsSlot->SetVerticalAlignment(VAlign_Fill);
    UVerticalBox* Now = AddSection(Controls, TEXT("СЕЙЧАС"));
    PlaybackTitle = MakeText(TEXT(""), 32, MenuInk, TEXT("Black"));
    Now->AddChildToVerticalBox(PlaybackTitle);
    PlaybackText = MakeText(TEXT(""), 22, MenuPink, TEXT("Bold"));
    Now->AddChildToVerticalBox(PlaybackText)->SetPadding(FMargin(0, 2, 0, 2));
    UVerticalBox* Transport = AddSection(Controls, TEXT("ВОСПРОИЗВЕДЕНИЕ"));
    UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>();
    GratiaPlace(Transport, Buttons, FMargin(0, 6, 0, 6));
    AddTextButton(Buttons, TEXT("« Часть"), EGratiaMenuAction::PrevPart, 0, Playback, 250.0f);
    AddTextButton(Buttons, TEXT("Пауза"), EGratiaMenuAction::Pause, 0, Playback, 250.0f, 64.0f, 22, true);
    AddTextButton(Buttons, TEXT("Часть »"), EGratiaMenuAction::NextPart, 0, Playback, 250.0f);
    AddTextButton(Buttons, TEXT("Сначала"), EGratiaMenuAction::Restart, 0, Playback, 250.0f);
    AddStepper(Transport, TEXT("Скорость"), TEXT("медленнее или быстрее; музыка шоу следует"), EGratiaMenuAction::SpeedDown, EGratiaMenuAction::SpeedUp, Playback);
    UVerticalBox* View = AddSection(Controls, TEXT("ВИД"));
    AddCycle(View, TEXT("Камера"), TEXT("глазами партнёра: лягте на пол или кровать и нажмите стик"), EGratiaMenuAction::PartnerView, Playback);
    UWidget* Empty = MakeEmptyState(TEXT("Шоу не запущено"),
        TEXT("Откройте шоу во вкладке «Сцены» — здесь появятся пауза, части, скорость и вид глазами партнёра."));
    UOverlaySlot* EmptySlot = Page->AddChildToOverlay(Empty);
    EmptySlot->SetHorizontalAlignment(HAlign_Center);
    EmptySlot->SetVerticalAlignment(VAlign_Center);
    Empties.Add({Controls, Empty, Playback});
    return Page;
}

UPanelWidget* UGratiaMenuWidget::BuildCharacterPage()
{
    UOverlay* Page = WidgetTree->ConstructWidget<UOverlay>();
    UVerticalBox* Controls = WidgetTree->ConstructWidget<UVerticalBox>();
    UOverlaySlot* ControlsSlot = Page->AddChildToOverlay(Controls);
    ControlsSlot->SetHorizontalAlignment(HAlign_Fill);
    ControlsSlot->SetVerticalAlignment(VAlign_Fill);
    UVerticalBox* Left = nullptr; UVerticalBox* Right = nullptr;
    MakeColumns(Controls, Left, Right);
    UVerticalBox* Mood = AddSection(Left, TEXT("НАСТРОЕНИЕ"));
    AddSegments(Mood, {TEXT("Спокойная"), TEXT("Радостная"), TEXT("Сдержанная")}, EGratiaMenuAction::Mood, Character, -1.0f);
    UTextBlock* MoodHint = MakeText(TEXT("Меняет реплики и реакции; выбор запоминается"), 17, MenuSoft, TEXT("Regular"));
    MoodHint->SetAutoWrapText(true);
    Mood->AddChildToVerticalBox(MoodHint)->SetPadding(FMargin(2, 0, 0, 0));
    UVerticalBox* Behaviour = AddSection(Left, TEXT("ПОВЕДЕНИЕ"));
    AddCycle(Behaviour, TEXT("Поза"), TEXT("обычная или игривая стойка"), EGratiaMenuAction::Pose, Character);
    AddToggle(Behaviour, TEXT("Демо реакций"), TEXT("персонаж сам показывает реакции по зонам"), EGratiaMenuAction::Demo, Character);
    UVerticalBox* Primitive = AddSection(Right, TEXT("ПРОНИКНОВЕНИЕ"));
    AddToggle(Primitive, TEXT("Примитив"), TEXT("появится перед вами; grip у рукояти — взять"), EGratiaMenuAction::Primitive, Character);
    AddCycle(Primitive, TEXT("Размер"), TEXT("от S до 4XL"), EGratiaMenuAction::PrimitiveSize, Character);
    AddCycle(Primitive, TEXT("Форма"), TEXT("гладкий, реалистичный, узел, бусины, конус, рёбра, раструб, щупальце"),
        EGratiaMenuAction::PrimitiveForm, Character);
    AddToggle(Primitive, TEXT("Руки в каналы"), TEXT("grip — три пальца, большой на стике — ладонь, grip и курок — кулак"), EGratiaMenuAction::HandPenetration, Character);
    // Reset sits under the behaviour on the left; the right column holds penetration.
    UVerticalBox* Look = AddSection(Right, TEXT("ВИД"));
    AddStepper(Look, TEXT("Влажность"), TEXT("мокрая кожа и одежда: блеск, капли, стекающие струйки"), EGratiaMenuAction::WetnessDown,
        EGratiaMenuAction::WetnessUp, Character);
    UVerticalBox* Reset = AddSection(Left, TEXT("СБРОС"));
    AddTextButton(Reset, TEXT("Сбросить позу и контакты"), EGratiaMenuAction::Reset, 0, Character, -1.0f, 58.0f, 21);
    UWidget* Empty = MakeEmptyState(TEXT("Персонажа нет рядом"),
        TEXT("Откройте сцену во вкладке «Сцены», чтобы менять настроение, позу и примитив."));
    UOverlaySlot* EmptySlot = Page->AddChildToOverlay(Empty);
    EmptySlot->SetHorizontalAlignment(HAlign_Center);
    EmptySlot->SetVerticalAlignment(VAlign_Center);
    Empties.Add({Controls, Empty, Character});
    return Page;
}

UPanelWidget* UGratiaMenuWidget::BuildPhysicsPage()
{
    UOverlay* Page = WidgetTree->ConstructWidget<UOverlay>();
    UVerticalBox* Controls = WidgetTree->ConstructWidget<UVerticalBox>();
    UOverlaySlot* ControlsSlot = Page->AddChildToOverlay(Controls);
    ControlsSlot->SetHorizontalAlignment(HAlign_Fill);
    ControlsSlot->SetVerticalAlignment(VAlign_Fill);
    UVerticalBox* Left = nullptr; UVerticalBox* Right = nullptr;
    MakeColumns(Controls, Left, Right);
    UVerticalBox* Motion = AddSection(Left, TEXT("ДВИЖЕНИЕ"));
    AddToggle(Motion, TEXT("Волосы"), TEXT("пряди качаются и откликаются на руки"), EGratiaMenuAction::Hair, Physics);
    AddToggle(Motion, TEXT("Одежда"), TEXT("юбка и ткань"), EGratiaMenuAction::Cloth, Physics);
    AddToggle(Motion, TEXT("Тело"), TEXT("мягкие части при движении и касании"), EGratiaMenuAction::Body, Physics);
    UVerticalBox* Details = AddSection(Right, TEXT("ДЕТАЛИ"));
    AddToggle(Details, TEXT("Уши и хвост"), TEXT("пружины ушей и хвоста"), EGratiaMenuAction::Ears, Physics);
    AddToggle(Details, TEXT("Вторичная физика"), TEXT("физические тела под руками"), EGratiaMenuAction::Physics, Physics);
    AddToggle(Details, TEXT("Локальные пружины"), TEXT("покачивание мягких частей"), EGratiaMenuAction::Springs, Physics);
    UWidget* Empty = MakeEmptyState(TEXT("Физика недоступна"),
        TEXT("Откройте сцену во вкладке «Сцены»: настройки физики относятся к персонажу рядом с вами."));
    UOverlaySlot* EmptySlot = Page->AddChildToOverlay(Empty);
    EmptySlot->SetHorizontalAlignment(HAlign_Center);
    EmptySlot->SetVerticalAlignment(VAlign_Center);
    Empties.Add({Controls, Empty, Physics});
    return Page;
}

UPanelWidget* UGratiaMenuWidget::BuildSettingsPage()
{
    UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();
    UVerticalBox* Left = nullptr; UVerticalBox* Right = nullptr;
    MakeColumns(Page, Left, Right);
    UVerticalBox* Graphics = AddSection(Left, TEXT("ГРАФИКА"));
    AddSegments(Graphics, {TEXT("Низкое"), TEXT("Среднее"), TEXT("Высокое")}, EGratiaMenuAction::Quality, Settings, -1.0f);
    UTextBlock* GraphicsHint = MakeText(TEXT("Если в шлеме проседают кадры, выберите качество ниже"), 17, MenuSoft, TEXT("Regular"));
    GraphicsHint->SetAutoWrapText(true);
    Graphics->AddChildToVerticalBox(GraphicsHint)->SetPadding(FMargin(2, 0, 0, 0));
    UVerticalBox* Haptics = AddSection(Left, TEXT("ВИБРАЦИЯ"));
    AddStepper(Haptics, TEXT("Сила"), TEXT("отклик контроллеров на касания"), EGratiaMenuAction::HapticsDown, EGratiaMenuAction::HapticsUp, Settings);
    UVerticalBox* Game = AddSection(Left, TEXT("ИГРА"));
    AddTextButton(Game, TEXT("Сбросить все настройки"), EGratiaMenuAction::ResetSettings, 0, Settings, -1.0f, 58.0f, 21);
    AddTextButton(Game, TEXT("Выйти из игры"), EGratiaMenuAction::Quit, 0, Settings, -1.0f, 58.0f, 21);
    UVerticalBox* Sound = AddSection(Right, TEXT("ЗВУК"));
    AddStepper(Sound, TEXT("Музыка"), TEXT(""), EGratiaMenuAction::MusicDown, EGratiaMenuAction::MusicUp, Settings);
    AddStepper(Sound, TEXT("Голос"), TEXT("громкость реплик персонажа"), EGratiaMenuAction::VoiceDown, EGratiaMenuAction::VoiceUp, Settings);
    AddToggle(Sound, TEXT("Голос реакций"), TEXT("звуки персонажа при касаниях"), EGratiaMenuAction::Sound, Settings);
    AddToggle(Sound, TEXT("Реплики в облачке"), TEXT("текст ответа рядом с головой"), EGratiaMenuAction::Captions, Settings);
    return Page;
}

UPanelWidget* UGratiaMenuWidget::BuildPlayerPage()
{
    UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();
    UVerticalBox* Left = nullptr; UVerticalBox* Right = nullptr;
    MakeColumns(Page, Left, Right);
    UVerticalBox* Comfort = AddSection(Left, TEXT("КОМФОРТ"));
    Comfort->AddChildToVerticalBox(MakeLabel(TEXT("Поворот правым стиком"), TEXT("")))->SetPadding(FMargin(0, 2, 0, 0));
    AddSegments(Comfort, {TEXT("30°"), TEXT("45°"), TEXT("Плавно")}, EGratiaMenuAction::TurnMode, Player, -1.0f);
    Comfort->AddChildToVerticalBox(MakeLabel(TEXT("Скорость ходьбы"), TEXT("")))->SetPadding(FMargin(0, 2, 0, 0));
    AddSegments(Comfort, {TEXT("Медленно"), TEXT("Обычно"), TEXT("Быстро")}, EGratiaMenuAction::WalkSpeed, Player, -1.0f);
    UVerticalBox* Position = AddSection(Left, TEXT("ПОЛОЖЕНИЕ"));
    AddStepper(Position, TEXT("Высота глаз"), TEXT("если пол кажется выше или ниже"), EGratiaMenuAction::HeightDown, EGratiaMenuAction::HeightUp, Player);
    AddToggle(Position, TEXT("Предплечья"), TEXT("руки продолжаются до локтя"), EGratiaMenuAction::Forearms, Player);
    AddTextButton(Position, TEXT("Центрировать взгляд"), EGratiaMenuAction::Recenter, 0, Player, -1.0f, 60.0f);
    UVerticalBox* Help = AddSection(Right, TEXT("УПРАВЛЕНИЕ"), true);
    const TCHAR* Lines[][2] = {
        {TEXT("Левый стик"), TEXT("ходьба")},
        {TEXT("Правый стик"), TEXT("поворот")},
        {TEXT("Клик стика"), TEXT("центрировать взгляд")},
        {TEXT("Y или B"), TEXT("открыть или закрыть меню")},
        {TEXT("Луч и курок"), TEXT("выбор в меню")},
        {TEXT("Ладонь"), TEXT("касание: персонаж ответит")},
        {TEXT("Курок у груди"), TEXT("обхватить и сжать")},
        {TEXT("Grip у тела"), TEXT("взять руку, ногу, талию")},
        {TEXT("Grip у рукояти"), TEXT("взять примитив")},
        {TEXT("Grip у входа"), TEXT("три пальца внутрь")},
        {TEXT("Большой на стике"), TEXT("ладонь, пальцы прямо")},
        {TEXT("Grip и курок"), TEXT("кулак")},
    };
    for (const auto& Line : Lines)
    {
        UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        GratiaPlace(Row, Sized(MakeText(Line[0], 20, MenuPink, TEXT("ExtraBold")), 200.0f, -1.0f), FMargin(0));
        UTextBlock* What = MakeText(Line[1], 20, MenuInk, TEXT("Regular"));
        GratiaPlace(Row, What, FMargin(8, 0, 0, 0), true);
        GratiaPlace(Help, Row, FMargin(2, 5));
    }
    return Page;
}

FString UGratiaMenuWidget::GetEmptyState() const
{
    if (!Menu.IsValid()) return FString();
    switch (CurrentPage)
    {
    case Playback: return Menu->IsActionAvailable(EGratiaMenuAction::Pause) ? FString() : FString(TEXT("Шоу не запущено"));
    case Character: return Menu->IsActionAvailable(EGratiaMenuAction::Mood) ? FString() : FString(TEXT("Персонажа нет рядом"));
    case Physics:
        for (const EGratiaMenuAction Action : {EGratiaMenuAction::Hair, EGratiaMenuAction::Cloth, EGratiaMenuAction::Body, EGratiaMenuAction::Ears,
            EGratiaMenuAction::Physics, EGratiaMenuAction::Springs})
            if (Menu->IsActionAvailable(Action)) return FString();
        return TEXT("Физика недоступна");
    default: return FString();
    }
}

void UGratiaMenuWidget::ShowPage(int32 Page)
{
    CurrentPage = FMath::Clamp(Page, 0, PageCount - 1);
    if (Pages) Pages->SetActiveWidgetIndex(CurrentPage);
    if (Header) Header->SetText(FText::FromString(GratiaPages[CurrentPage].Name));
    if (HeaderHint) HeaderHint->SetText(FText::FromString(GratiaPages[CurrentPage].Hint));
    if (Items.IsValidIndex(Focus) && Items[Focus].Page != INDEX_NONE && Items[Focus].Page != CurrentPage) Focus = INDEX_NONE;
    RefreshState();
}

TArray<int32> UGratiaMenuWidget::VisibleItems() const
{
    TArray<int32> Result;
    const FString Empty = GetEmptyState();
    for (int32 I = 0; I < Items.Num(); ++I)
    {
        const FItem& Item = Items[I];
        if (Item.Page != INDEX_NONE && (Item.Page != CurrentPage || !Empty.IsEmpty())) continue;
        if (Item.Button.IsValid() && Item.Button->IsVisible() && Item.Button->GetIsEnabled()) Result.Add(I);
    }
    return Result;
}

void UGratiaMenuWidget::StyleItem(FItem& Item, bool bEnabled, bool bActive, bool bFocused)
{
    UButton* Button = Item.Button.Get();
    if (!Button) return;
    FLinearColor Fill = MenuControl, Hover = MenuControlHover, Press = MenuPink, Edge = MenuLine;
    float EdgeWidth = 1.5f;
    switch (Item.Kind)
    {
    case EKind::Nav:
    case EKind::Segment:
        Fill = bActive ? MenuPink : MenuClear; Hover = bActive ? MenuPinkHover : MenuRowHover; Edge = MenuClear; EdgeWidth = 0.0f;
        break;
    case EKind::Toggle:
        Fill = MenuClear; Hover = MenuRowHover; Press = MenuRowHover; Edge = MenuClear; EdgeWidth = 0.0f;
        break;
    case EKind::Card:
        Fill = MenuSurface; Hover = MenuSurfaceHover; Press = MenuSurfaceHover;
        Edge = bActive ? Item.Accent : MenuLine; EdgeWidth = bActive ? 3.0f : 1.5f;
        break;
    default:
        if (bActive) { Fill = MenuPink; Hover = MenuPinkHover; }
        break;
    }
    FLinearColor HoverEdge = Item.Kind == EKind::Card ? Item.Accent : Edge;
    float HoverWidth = Item.Kind == EKind::Card ? 3.0f : EdgeWidth;
    if (bFocused) { Edge = HoverEdge = MenuInk; EdgeWidth = HoverWidth = 3.0f; }
    FLinearColor Disabled = Fill;
    Disabled.A *= 0.4f;
    FButtonStyle Style;
    Style.SetNormal(MenuRounded(Fill, Item.Radius, Edge, EdgeWidth));
    Style.SetHovered(MenuRounded(Hover, Item.Radius, HoverEdge, HoverWidth));
    Style.SetPressed(MenuRounded(Press, Item.Radius, HoverEdge, HoverWidth));
    Style.SetDisabled(MenuRounded(Disabled, Item.Radius));
    Style.SetNormalPadding(FMargin(0));
    Style.SetPressedPadding(FMargin(0, 1, 0, -1));
    Button->SetStyle(Style);
    const FLinearColor Text = !bEnabled ? MenuDim : Item.Kind == EKind::Nav && !bActive ? MenuSoft : MenuInk;
    if (Item.Label.IsValid()) Item.Label->SetColorAndOpacity(FSlateColor(Text));
    if (Item.Value.IsValid()) Item.Value->SetColorAndOpacity(FSlateColor(bEnabled ? MenuInk : MenuDim));
    if (Item.Track.IsValid())
    {
        Item.Track->SetBrush(MenuRounded(bActive ? (bEnabled ? MenuPink : MenuPink * 0.45f) : MenuOff, 20.0f, bActive ? MenuClear : MenuLine, 1.5f));
        Item.Track->SetHorizontalAlignment(bActive ? HAlign_Right : HAlign_Left);
    }
    if (Item.Knob.IsValid()) Item.Knob->SetBrush(MenuRounded(bEnabled ? MenuInk : MenuSoft * 0.6f, 15.0f));
}

void UGratiaMenuWidget::RefreshState()
{
    if (!Menu.IsValid()) return;
    for (int32 I = 0; I < Items.Num(); ++I)
    {
        FItem& Item = Items[I];
        UButton* Button = Item.Button.Get();
        if (!Button) continue;
        const bool bEnabled = Menu->IsActionAvailable(Item.Action, Item.Param);
        Button->SetIsEnabled(bEnabled);
        if (Item.bDynamicLabel && Item.Label.IsValid()) Item.Label->SetText(FText::FromString(Menu->LabelFor(Item.Action, Item.Param)));
        if (Item.Value.IsValid()) Item.Value->SetText(FText::FromString(Menu->ValueFor(Item.Action)));
        // The lobby button only exists inside a scene.
        if (Item.Action == EGratiaMenuAction::Lobby)
            Button->SetVisibility(Menu->IsInScene() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        const bool bCurrentScene = Item.Kind == EKind::Card && Item.Param == Menu->GetCurrentScene();
        if (Item.Badge.IsValid()) Item.Badge->SetVisibility(bCurrentScene ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        const bool bActive = Item.Kind == EKind::Card ? bCurrentScene
            : Item.Action == EGratiaMenuAction::Tab ? Item.Param == CurrentPage : Menu->IsSelected(Item.Action, Item.Param);
        StyleItem(Item, bEnabled, bActive, I == Focus);
    }
    for (const FValue& Value : Values)
        if (Value.Text.IsValid()) Value.Text->SetText(FText::FromString(Menu->ValueFor(Value.Action)));
    const bool bEmpty = !GetEmptyState().IsEmpty();
    for (const FEmpty& Empty : Empties)
    {
        const bool bShowEmpty = Empty.Page == CurrentPage && bEmpty;
        if (Empty.Controls.IsValid()) Empty.Controls->SetVisibility(bShowEmpty ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
        if (Empty.Message.IsValid()) Empty.Message->SetVisibility(bShowEmpty ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
}

void UGratiaMenuWidget::Hover(int32 Index, bool bHovered)
{
    if (!Items.IsValidIndex(Index)) return;
    const bool bWas = Items[Index].bHovered;
    Items[Index].bHovered = bHovered;
    if (bHovered && !bWas && Menu.IsValid() && Items[Index].Button.IsValid() && Items[Index].Button->GetIsEnabled()) Menu->PointerFeedback(false);
}

void UGratiaMenuWidget::Navigate(int32 Direction)
{
    const TArray<int32> Visible = VisibleItems();
    if (Visible.IsEmpty()) return;
    const int32 Position = Visible.IndexOfByKey(Focus);
    Focus = Visible[Position == INDEX_NONE ? 0 : (Position + Direction + Visible.Num()) % Visible.Num()];
    RefreshState();
    if (Menu.IsValid()) Menu->PointerFeedback(false);
}

void UGratiaMenuWidget::Activate()
{
    if (Items.IsValidIndex(Focus) && VisibleItems().Contains(Focus)) Click(Items[Focus].Action, Items[Focus].Param);
    else Navigate(1);
}

void UGratiaMenuWidget::Click(EGratiaMenuAction Action, int32 Param)
{
    if (Menu.IsValid()) Menu->PointerFeedback(true);
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
    // The neon edge breathes with the beat.
    if (PanelEdge) PanelEdge->SetBrush(MenuRounded(MenuGlass, 46.0f, FLinearColor::LerpUsingHSV(MenuEdge, MenuPinkHover, FMath::Clamp(Music.W, 0.0f, 1.0f)), 3.0f));
    const FString Status = Director ? Director->GetLastError() : FString();
    if (StatusText && Status != LastStatus)
    {
        LastStatus = Status;
        StatusText->SetText(FText::FromString(Status));
        StatusText->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
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
        Bars[I]->SetHeightOverride(5.0f + 38.0f * FMath::Clamp(Band * (0.6f + 0.4f * Ripple) + 0.25f * Music.W, 0.0f, 1.0f));
    }
    // Scene cards lift while pointed at.
    for (int32 I = 0; I < Items.Num(); ++I)
    {
        FItem& Item = Items[I];
        if (Item.Kind != EKind::Card || !Item.Button.IsValid()) continue;
        const float Target = (Item.bHovered || I == Focus) && Item.Button->GetIsEnabled() ? 1.0f : 0.0f;
        Item.HoverAmount = FMath::FInterpTo(Item.HoverAmount, Target, DeltaSeconds, 14.0f);
        Item.Button->SetRenderScale(FVector2D(1.0f + 0.03f * Item.HoverAmount));
    }
    if (CurrentPage == Playback && Director && PlaybackText)
    {
        const FGratiaSceneEntry* Entry = Director->GetCurrentEntry();
        PlaybackTitle->SetText(Entry ? Entry->Title : FText::FromString(TEXT("Нет сцены")));
        PlaybackText->SetText(FText::FromString(Director->GetPlaybackText()));
    }
}
