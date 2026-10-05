#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GratiaCharacterProfile.h"
#include "GratiaSoftBodySetupLibrary.generated.h"

class USkeletalMesh;

/** Editor-only measurements that author a profile's KawaiiPhysics soft body data. */
UCLASS()
class GRATIAVREDITORTOOLS_API UGratiaSoftBodySetupLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Measures a soft root bone from the skin it drives: forward axis (head to the
     * skin centroid), length to the farthest skin point, and a contact sphere.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static bool MeasureSoftBone(USkeletalMesh* SkeletalMesh, FName RootBone, EGratiaBoneAxis& ForwardAxis,
        float& LengthCm, float& ContactRadiusCm, float& ContactCenterAlongBone);

    /**
     * Converts exported source collision objects (schema 2 "colliders") into spheres
     * inscribed in per-bone convex hulls, keeping only those within RangeCm of a soft bone.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static bool BuildBodyColliders(USkeletalMesh* SkeletalMesh, const FString& JSONPath, const TArray<FName>& SoftBones,
        float RangeCm, TArray<FGratiaBodyColliderSphere>& Colliders);
};
