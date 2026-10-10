#pragma once
#include "CoreMinimal.h"
#include "GratiaCustomizationTypes.generated.h"

class USkeletalMesh;
class UStaticMesh;
class UMaterialInterface;

/** One choice of a customization slot: parts of the character mesh shown/hidden, morphs, attached props. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaCustomizationOption
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
    FText Label;
    /** Material slots of the character mesh hidden in this option (all their sections, every LOD). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
    TArray<FName> HiddenMaterialSlots;
    /** Material slot -> replacement material (a recolour, another hair tone). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
    TMap<FName, TObjectPtr<UMaterialInterface>> Materials;
    /** Morph target -> weight held while the option is selected. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
    TMap<FName, float> Morphs;
    /** Skinned accessory/hairstyle on the character's skeleton (follows the pose). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
    TObjectPtr<USkeletalMesh> SkinnedAttachment;
    /** Rigid accessory attached to a bone or socket. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
    TObjectPtr<UStaticMesh> StaticAttachment;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
    FName AttachBone;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
    FTransform AttachTransform;
};

/** A thing the player can change inside the scene (outfit layer, hairstyle, accessory). Option 0 is the default. */
USTRUCT(BlueprintType)
struct GRATIAVR_API FGratiaCustomizationSlot
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
    FText Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization", meta = (TitleProperty = "Label"))
    TArray<FGratiaCustomizationOption> Options;
};
