#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GratiaClothPortLibrary.generated.h"

class USkeletalMesh;

/** Authors native Chaos cloth from exported source cages and SurfaceDeform masks. */
UCLASS()
class GRATIAVREDITORTOOLS_API UGratiaClothPortLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Edits mesh-owned cloth only. Caller saves the mesh after success.
     *  Render vertices whose dominant skin bone is (under) an excluded root stay skinned:
     *  a merged mesh cannot tell, e.g., hair over the chest from a shared-material consumer. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Gratia|Editor")
    static bool BuildSourceClothCages(USkeletalMesh* SkeletalMesh, const FString& JSONPath, const TArray<FName>& ExcludedBoneRoots,
        float StiffnessHalfPoint, float PressureScale);
};
