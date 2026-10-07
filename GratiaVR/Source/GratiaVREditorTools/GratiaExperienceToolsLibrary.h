#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GratiaExperienceToolsLibrary.generated.h"

class UFont;
class UFontFace;

/** Editor helpers for setup_scene_experience.py that Python cannot reach (private engine fields). */
UCLASS()
class GRATIAVREDITORTOOLS_API UGratiaExperienceToolsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Makes Font a runtime composite font whose default typeface has Names[i] -> Faces[i]. */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static bool BuildCompositeFont(UFont* Font, const TArray<FName>& Names, const TArray<UFontFace*>& Faces);
    /**
     * Font face asset (PackageName, e.g. /Game/UI/Font/MyFace) holding the .ttf/.otf file's data.
     * The stock importer asks Slate (dialog, font cache flush), which a -nullrhi commandlet has not.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static UFontFace* ImportFontFace(const FString& Filename, const FString& PackageName);
};
