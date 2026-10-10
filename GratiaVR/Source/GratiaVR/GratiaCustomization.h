#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaCustomization.generated.h"

class AGratiaPreviewCharacter;
class UGratiaCharacterProfile;
class USceneComponent;

/** Applies CharacterProfile.Customization at runtime (in-scene outfit/hairstyle/accessory changes). */
UCLASS(ClassGroup = (Gratia), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaCustomization : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaCustomization();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    static UGratiaCustomization* Ensure(AGratiaPreviewCharacter* InCharacter);

    UFUNCTION(BlueprintPure, Category = "Customization")
    int32 GetSlotCount() const;
    UFUNCTION(BlueprintPure, Category = "Customization")
    FText GetSlotLabel(int32 Slot) const;
    UFUNCTION(BlueprintPure, Category = "Customization")
    FText GetOptionLabel(int32 Slot) const;
    UFUNCTION(BlueprintCallable, Category = "Customization")
    bool SelectOption(int32 Slot, int32 Option);
    /** Next option of a slot (wraps). */
    UFUNCTION(BlueprintCallable, Category = "Customization")
    bool CycleSlot(int32 Slot);
    UFUNCTION(BlueprintPure, Category = "Customization")
    int32 GetSelectedOption(int32 Slot) const { return Selected.IsValidIndex(Slot) ? Selected[Slot] : 0; }
    UFUNCTION(BlueprintPure, Category = "Customization|Diagnostics")
    FString GetDiagnostics() const;

protected:
    virtual void BeginPlay() override;

private:
    const UGratiaCharacterProfile* GetProfile() const;
    void ApplyAll();
    void Restore();
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<const UGratiaCharacterProfile> AppliedProfile;
    TArray<int32> Selected;
    /** Mesh material slots changed by an option, with the material they had. */
    TMap<int32, TObjectPtr<class UMaterialInterface>> OriginalMaterials;
    TSet<int32> HiddenSlots;
    TArray<FName> HeldMorphs;
    TMap<FName, float> MorphWeights;
    UPROPERTY(Transient)
    TArray<TObjectPtr<USceneComponent>> Attachments;
    FString LastProblem;
};
