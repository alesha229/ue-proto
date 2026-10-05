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
    /**
     * Fits one capsule per bone to the visible surface it skins (vertices whose dominant
     * weight >= 0.5, only sections with IncludeSlots materials when given): axis along the
     * bone (or the skin's principal axis), centre line through the skin, radius at the
     * RadiusPercentile of distances. Bones with fewer than MinVertices are skipped.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static bool MeasureBodySurface(USkeletalMesh* SkeletalMesh, const TArray<FName>& IncludeSlots, const TArray<FName>& ExcludeBones,
        int32 MinVertices, float RadiusPercentile, TArray<FGratiaSurfaceCapsule>& Capsules);

    /**
     * One sphere per bone fitted (least squares) to the surface it skins, e.g. a breast dome
     * or the head; radius at RadiusPercentile of the vertex distances (outer layer).
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static bool MeasureSphereSurface(USkeletalMesh* SkeletalMesh, const TArray<FName>& IncludeSlots, const TArray<FName>& Bones,
        float RadiusPercentile, TArray<FGratiaSurfaceCapsule>& Capsules);

    /**
     * Torso as horizontal stadium slices (every StepCm of height) from all vertices owned by
     * VertexBones: belly, back, waist and chest form closed rings even where one bone only
     * skins the front. Each slice is attached to the nearest AttachBone and wraps around up.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static bool MeasureTorsoSlices(USkeletalMesh* SkeletalMesh, const TArray<FName>& IncludeSlots, const TArray<FName>& VertexBones,
        const TArray<FName>& AttachBones, float StepCm, TArray<FGratiaSurfaceCapsule>& Capsules);

    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static bool BuildBodyColliders(USkeletalMesh* SkeletalMesh, const FString& JSONPath, const TArray<FName>& SoftBones,
        float RangeCm, TArray<FGratiaBodyColliderSphere>& Colliders);
};
