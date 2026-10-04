#include "GratiaStage1GameMode.h"

#include "GameFramework/Pawn.h"
#include "GratiaStage1HUD.h"
#include "UObject/ConstructorHelpers.h"

AGratiaStage1GameMode::AGratiaStage1GameMode()
{
    static ConstructorHelpers::FClassFinder<APawn> PawnClass(TEXT("/Game/XRFramework/Blueprints/BP_XRPawn"));
    if (PawnClass.Succeeded()) DefaultPawnClass = PawnClass.Class;
    HUDClass = AGratiaStage1HUD::StaticClass();
}
