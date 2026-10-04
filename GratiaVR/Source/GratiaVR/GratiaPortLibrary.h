#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GratiaPortLibrary.generated.h"

class USkeletalMesh;
class UPhysicsAsset;

/** Editor authoring helpers; no asset creation or animation runs in constructors. */
UCLASS()
class GRATIAVR_API UGratiaPortLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Creates/updates a persistent constrained asset at a /Game/Gratia path.
     * Uses imported rest frames and source secondary-role metadata. Caller saves
     * the returned asset and mesh. Packaged builds return nullptr.
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Gratia|Editor")
    static UPhysicsAsset* BuildPhysicsAsset(USkeletalMesh* SkeletalMesh, const FString& AssetPackagePath);
};
