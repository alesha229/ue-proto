#include "GratiaStage1HUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GratiaStage1Runtime.h"

void AGratiaStage1HUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    if (!Runtime.IsValid())
    {
        for (TActorIterator<AGratiaStage1Runtime> It(GetWorld()); It; ++It)
        {
            Runtime = *It;
            break;
        }
    }
    if (!Runtime.IsValid() || !Runtime->bShowDebug) return;
    const float Scale = FMath::Clamp(Canvas->ClipX / 1280.0f, 0.75f, 1.5f);
    DrawRect(FLinearColor(0.015f, 0.025f, 0.035f, 0.8f), 12.0f, 12.0f,
        FMath::Min(Canvas->ClipX - 24.0f, 850.0f * Scale), 160.0f * Scale);
    DrawText(Runtime->GetStatusText(), FLinearColor(0.65f, 0.95f, 0.85f), 22.0f, 20.0f,
        GEngine ? GEngine->GetSmallFont() : nullptr, Scale, false);
}
