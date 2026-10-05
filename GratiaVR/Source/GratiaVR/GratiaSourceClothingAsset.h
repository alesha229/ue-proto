#pragma once

#include "CoreMinimal.h"
#include "ClothingAsset.h"
#include "GratiaSourceClothingAsset.generated.h"

struct FSkelMeshSection;
struct FMeshToMeshVertData;

/** Editor-only render binding authored from source SurfaceDeform masks. */
USTRUCT()
struct GRATIAVR_API FGratiaStoredClothSectionMapping
{
    GENERATED_BODY()
    UPROPERTY() int32 SectionIndex = INDEX_NONE;
    UPROPERTY() int32 NumVertices = 0;
    /** Quantized render positions; a reimported or reordered section is rejected. */
    UPROPERTY() uint32 PositionHash = 0;
    /** Raw FMeshToMeshVertData array for this section (one entry per render vertex). */
    UPROPERTY() TArray<uint8> MappingBytes;
};

/**
 * Native Chaos clothing asset whose render binding follows authored masks.
 *
 * Unreal regenerates cloth render bindings whenever the skeletal mesh is rebuilt
 * (also during cook). Its generic nearest-triangle binding cannot express source
 * SurfaceDeform masks and binds one asset LOD to only one section. This class
 * reapplies the stored, validated mapping to each recorded section instead.
 * At runtime it is an ordinary UClothingAssetCommon.
 */
UCLASS(hidecategories = Object)
class GRATIAVR_API UGratiaSourceClothingAsset : public UClothingAssetCommon
{
    GENERATED_BODY()

public:
    UGratiaSourceClothingAsset(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
    virtual bool BindToSkeletalMesh(USkeletalMesh* InSkelMesh, const int32 InMeshLodIndex, const int32 InSectionIndex, const int32 InAssetLodIndex) override;
    virtual TArray<TPair<FText, FText>> GetStats() const override;

    static uint32 HashSectionPositions(const FSkelMeshSection& Section);
    void SetStoredMappings(TArray<FGratiaStoredClothSectionMapping>&& Mappings) { StoredMappings = MoveTemp(Mappings); }
    const FGratiaStoredClothSectionMapping* FindStoredMapping(int32 SectionIndex) const;
    /** True when the section is bound to this asset with exactly the stored mapping. */
    bool IsSectionUsingStoredMapping(const USkeletalMesh* Mesh, int32 MeshLodIndex, int32 SectionIndex) const;
#endif

#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TArray<FGratiaStoredClothSectionMapping> StoredMappings;
#endif
};
