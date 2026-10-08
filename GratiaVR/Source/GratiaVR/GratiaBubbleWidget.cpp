#include "GratiaBubbleWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UGratiaBubbleWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget) BuildTree();
    return Super::RebuildWidget();
}

void UGratiaBubbleWidget::BuildTree()
{
    Bubble = WidgetTree->ConstructWidget<UBorder>();
    FSlateBrush Brush;
    Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
    Brush.TintColor = FSlateColor(FLinearColor(1.0f, 0.96f, 0.99f, 0.97f));
    Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(40.0, 40.0, 40.0, 40.0), FSlateColor(Accent), 5.0f);
    Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
    Bubble->SetBrush(Brush);
    Bubble->SetPadding(FMargin(38.0f, 14.0f, 38.0f, 18.0f));
    Bubble->SetHorizontalAlignment(HAlign_Center);
    Bubble->SetVerticalAlignment(VAlign_Center);
    Label = WidgetTree->ConstructWidget<UTextBlock>();
    Label->SetFont(Font ? FSlateFontInfo(Font, 46, FName(TEXT("ExtraBold"))) : FCoreStyle::GetDefaultFontStyle("Bold", 46));
    Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.18f, 0.03f, 0.16f)));
    Label->SetJustification(ETextJustify::Center);
    Label->SetText(Line);
    Bubble->SetContent(Label);
    WidgetTree->RootWidget = Bubble;
}

void UGratiaBubbleWidget::SetLine(const FText& Text)
{
    Line = Text;
    if (Label) Label->SetText(Text);
}
