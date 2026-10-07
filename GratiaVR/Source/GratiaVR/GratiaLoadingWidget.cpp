#include "GratiaLoadingWidget.h"
#include "GratiaSceneLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Styling/CoreStyle.h"

namespace
{
const FLinearColor GratiaInk(1.0f, 0.93f, 0.98f);
const FLinearColor GratiaPink(1.0f, 0.06f, 0.42f);
const FLinearColor GratiaLilac(0.55f, 0.40f, 1.0f);
constexpr float GratiaBarWidth = 720.0f;

FSlateBrush GratiaLoadingRounded(const FLinearColor& Fill, float Radius)
{
    FSlateBrush Brush;
    Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
    Brush.TintColor = FSlateColor(Fill);
    Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius), FSlateColor(FLinearColor::Transparent), 0.0f);
    Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
    return Brush;
}

const TCHAR* GratiaTips[] = {
    TEXT("Нажми на стик — встать в центр сцены"),
    TEXT("Y или B — меню, луч из правой руки и триггер — выбор"),
    TEXT("«  » в меню — переключить трек, переход мягкий, по биту"),
    TEXT("Триггер рядом с волосами — взять прядь"),
    TEXT("Ляг и нажми стик в сцене с партнёром — его глазами"),
};
}

TSharedRef<SWidget> UGratiaLoadingWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget) BuildTree();
    return Super::RebuildWidget();
}

UTextBlock* UGratiaLoadingWidget::MakeText(int32 Size, const TCHAR* Typeface, const FLinearColor& Color)
{
    UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
    const UGratiaSceneLibrary* Look = Library.Get();
    Block->SetFont(Look && Look->Font ? FSlateFontInfo(Look->Font, Size, FName(Typeface))
        : FCoreStyle::GetDefaultFontStyle(FCString::Strcmp(Typeface, TEXT("Regular")) ? "Bold" : "Regular", Size));
    Block->SetColorAndOpacity(FSlateColor(Color));
    Block->SetJustification(ETextJustify::Center);
    return Block;
}

void UGratiaLoadingWidget::BuildTree()
{
    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
    WidgetTree->RootWidget = Root;
    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    UOverlaySlot* ColumnSlot = Root->AddChildToOverlay(Column);
    ColumnSlot->SetHorizontalAlignment(HAlign_Center);
    ColumnSlot->SetVerticalAlignment(VAlign_Center);
    SceneBox = WidgetTree->ConstructWidget<USizeBox>();
    SceneBox->SetWidthOverride(460.0f);
    SceneBox->SetHeightOverride(400.0f);
    Picture = WidgetTree->ConstructWidget<UImage>();
    if (const UGratiaSceneLibrary* Look = Library.Get(); Look && Look->HexMaterial)
    {
        PictureMaterial = UMaterialInstanceDynamic::Create(Look->HexMaterial, this);
        FSlateBrush Brush;
        Brush.SetResourceObject(PictureMaterial);
        Brush.ImageSize = FVector2D(460, 400);
        Picture->SetBrush(Brush);
    }
    SceneBox->AddChild(Picture);
    Column->AddChildToVerticalBox(SceneBox)->SetHorizontalAlignment(HAlign_Center);
    Title = MakeText(58, TEXT("Black"), GratiaInk);
    FSlateFontInfo TitleFont = Title->GetFont();
    TitleFont.OutlineSettings.OutlineSize = 3;
    TitleFont.OutlineSettings.OutlineColor = GratiaPink;
    TitleFont.LetterSpacing = 60;
    Title->SetFont(TitleFont);
    Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 18, 0, 0));
    Subtitle = MakeText(30, TEXT("Bold"), GratiaLilac);
    Subtitle->SetAutoWrapText(true);
    Column->AddChildToVerticalBox(Subtitle)->SetPadding(FMargin(40, 2, 40, 26));
    // Progress: dark track with a pink-to-violet fill.
    Track = WidgetTree->ConstructWidget<USizeBox>();
    Track->SetWidthOverride(GratiaBarWidth);
    Track->SetHeightOverride(18.0f);
    UOverlay* Bar = WidgetTree->ConstructWidget<UOverlay>();
    Track->AddChild(Bar);
    UBorder* Back = WidgetTree->ConstructWidget<UBorder>();
    Back->SetBrush(GratiaLoadingRounded(FLinearColor(0.06f, 0.02f, 0.12f, 0.9f), 7.0f));
    UOverlaySlot* BackSlot = Bar->AddChildToOverlay(Back);
    BackSlot->SetHorizontalAlignment(HAlign_Fill);
    BackSlot->SetVerticalAlignment(VAlign_Fill);
    Fill = WidgetTree->ConstructWidget<USizeBox>();
    Fill->SetWidthOverride(0.0f);
    UBorder* FillBox = WidgetTree->ConstructWidget<UBorder>();
    FillBox->SetBrush(GratiaLoadingRounded(GratiaPink, 7.0f));
    Fill->AddChild(FillBox);
    UOverlaySlot* FillSlot = Bar->AddChildToOverlay(Fill);
    FillSlot->SetHorizontalAlignment(HAlign_Left);
    FillSlot->SetVerticalAlignment(VAlign_Fill);
    Column->AddChildToVerticalBox(Track)->SetHorizontalAlignment(HAlign_Center);
    Percent = MakeText(24, TEXT("ExtraBold"), GratiaPink);
    FSlateFontInfo PercentFont = Percent->GetFont();
    PercentFont.LetterSpacing = 300;
    Percent->SetFont(PercentFont);
    Column->AddChildToVerticalBox(Percent)->SetPadding(FMargin(0, 10, 0, 18));
    Tip = MakeText(28, TEXT("Bold"), GratiaInk * 0.85f);
    Column->AddChildToVerticalBox(Tip);
    Notice = MakeText(26, TEXT("ExtraBold"), GratiaPink);
    Notice->SetAutoWrapText(true);
    Notice->SetVisibility(ESlateVisibility::Collapsed);
    Column->AddChildToVerticalBox(Notice)->SetPadding(FMargin(60, 0));
}

void UGratiaLoadingWidget::ShowScene(const FText& InTitle, const FText& InSubtitle, const FLinearColor& Accent, UTexture2D* Image)
{
    if (!WidgetTree || !WidgetTree->RootWidget) TakeWidget();
    const bool bLobby = InTitle.IsEmpty();
    for (UWidget* Part : {Cast<UWidget>(SceneBox.Get()), Cast<UWidget>(Title.Get()), Cast<UWidget>(Subtitle.Get()), Cast<UWidget>(Track.Get()), Cast<UWidget>(Percent.Get()), Cast<UWidget>(Tip.Get())})
        if (Part) Part->SetVisibility(bLobby ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    Notice->SetVisibility(ESlateVisibility::Collapsed);
    Title->SetText(InTitle);
    Subtitle->SetText(InSubtitle);
    if (PictureMaterial)
    {
        PictureMaterial->SetVectorParameterValue(TEXT("Accent"), Accent);
        PictureMaterial->SetScalarParameterValue(TEXT("HasImage"), Image ? 1.0f : 0.0f);
        PictureMaterial->SetScalarParameterValue(TEXT("Hover"), 0.6f);
        if (Image) PictureMaterial->SetTextureParameterValue(TEXT("Image"), Image);
    }
    TipIndex = (TipIndex + 1) % UE_ARRAY_COUNT(GratiaTips);
    Tip->SetText(FText::FromString(GratiaTips[TipIndex]));
    Progress = Shown = 0.0f;
    SetProgress(0.0f);
}

void UGratiaLoadingWidget::ShowNotice(const FText& Message)
{
    if (!WidgetTree || !WidgetTree->RootWidget) TakeWidget();
    Notice->SetText(Message);
    Notice->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UGratiaLoadingWidget::SetProgress(float Value)
{
    Progress = FMath::Clamp(FMath::IsFinite(Value) ? Value : 0.0f, 0.0f, 1.0f);
}

void UGratiaLoadingWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    Time += DeltaSeconds;
    Shown = FMath::FInterpTo(Shown, Progress, DeltaSeconds, 6.0f);
    if (Fill) Fill->SetWidthOverride(GratiaBarWidth * Shown);
    if (Percent) Percent->SetText(FText::FromString(FString::Printf(TEXT("ЗАГРУЗКА  %d%%"), FMath::RoundToInt(Shown * 100.0f))));
    // The picture breathes slowly while the scene streams in.
    if (PictureMaterial) PictureMaterial->SetScalarParameterValue(TEXT("Hover"), 0.45f + 0.25f * FMath::Sin(Time * 2.4f));
}
