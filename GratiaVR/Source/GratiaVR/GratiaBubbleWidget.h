#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GratiaBubbleWidget.generated.h"

class UBorder;
class UFont;
class UTextBlock;

/** Speech bubble of a reaction line: a rounded light bubble with an accent outline, sized to its text. */
UCLASS()
class GRATIAVR_API UGratiaBubbleWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    /** Set before the widget is first shown; empty uses the engine's bold font. */
    UPROPERTY(Transient) TObjectPtr<UFont> Font;
    FLinearColor Accent = FLinearColor(1.0f, 0.06f, 0.42f);
    void SetLine(const FText& Text);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    void BuildTree();
    UPROPERTY(Transient) TObjectPtr<UBorder> Bubble;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
    FText Line;
};
