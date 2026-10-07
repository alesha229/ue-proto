#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GratiaLoadingWidget.generated.h"

class UGratiaSceneLibrary;
class UImage;
class UMaterialInstanceDynamic;
class USizeBox;
class UTextBlock;
class UTexture2D;

/**
 * Loading card floating in the loading space (ViRo look): the scene picture in a glowing hexagon,
 * the title with a neon outline, a gradient progress bar with percent and a rotating tip. In the
 * lobby it shows only a failure notice when there is one.
 */
UCLASS()
class GRATIAVR_API UGratiaLoadingWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<const UGratiaSceneLibrary> Library;
    void ShowScene(const FText& Title, const FText& Subtitle, const FLinearColor& Accent, UTexture2D* Picture);
    void ShowNotice(const FText& Message);
    void SetProgress(float Value);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

private:
    void BuildTree();
    UTextBlock* MakeText(int32 Size, const TCHAR* Typeface, const FLinearColor& Color);

    UPROPERTY(Transient) TObjectPtr<UImage> Picture;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PictureMaterial;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Subtitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Percent;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Tip;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Notice;
    UPROPERTY(Transient) TObjectPtr<USizeBox> Fill;
    UPROPERTY(Transient) TObjectPtr<USizeBox> SceneBox;
    UPROPERTY(Transient) TObjectPtr<USizeBox> Track;
    float Progress = 0.0f, Shown = 0.0f, Time = 0.0f;
    int32 TipIndex = 0;
};
