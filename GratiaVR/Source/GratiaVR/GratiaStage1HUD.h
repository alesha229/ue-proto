#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GratiaStage1HUD.generated.h"

class AGratiaStage1Runtime;

UCLASS()
class GRATIAVR_API AGratiaStage1HUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    TWeakObjectPtr<AGratiaStage1Runtime> Runtime;
};
