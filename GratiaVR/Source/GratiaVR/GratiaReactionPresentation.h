#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaReactionPresentation.generated.h"

class AGratiaPreviewCharacter;
class UGratiaInteraction;
class UGratiaCharacterProfile;
class UTextRenderComponent;
class UAudioComponent;
class USoundBase;

/** Optional local caption/audio presenter. Contact detection only emits events. */
UCLASS(ClassGroup = (Interaction), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaReactionPresentation : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaReactionPresentation();
    virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UFUNCTION(BlueprintPure, Category = "Interaction|Presentation")
    FString GetCaptionText() const;
    UFUNCTION(BlueprintPure, Category = "Interaction|Presentation")
    bool IsCaptionVisible() const;
    UFUNCTION(BlueprintCallable, Category = "Interaction|Presentation")
    void ResetPresentation();

    /** Disable this presenter when an external UI/audio subscriber handles the same event. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation")
    bool bPresentCaptions = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation")
    bool bPresentSound = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation", meta = (ClampMin = "0.1", Units = "cm"))
    float CaptionWorldSize = 3.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation")
    FColor CaptionColor = FColor(230, 240, 255);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SoundVolume = 0.3f;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleContactReaction(FName ZoneName, int32 HandIndex, float HandSpeed, int32 Mood);
    UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> Caption;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ActiveAudio;
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<UGratiaInteraction> Source;
    TWeakObjectPtr<UGratiaCharacterProfile> PresentedProfile;
    float CaptionSeconds = 0.0f;
    double LastChimeTime = -10.0;
    USoundBase* SelectSound(FName ZoneName, const UGratiaCharacterProfile& Profile) const;
    void StopSound();
};
