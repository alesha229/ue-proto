#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaReactionPresentation.generated.h"

class AGratiaPreviewCharacter;
class UGratiaInteraction;
class UGratiaCharacterProfile;
class UGratiaBubbleWidget;
class UWidgetComponent;
class UAudioComponent;
class UFont;
class USoundBase;
class USoundAttenuation;
struct FGratiaReactionLine;

/**
 * Answers contact reactions with the profile's ReactionLines: a speech bubble by the head that faces the player
 * and a voice from the touched zone. Contact detection only emits events.
 */
UCLASS(ClassGroup = (Interaction), meta = (BlueprintSpawnableComponent))
class GRATIAVR_API UGratiaReactionPresentation : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaReactionPresentation();
    virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Text of the bubble on screen (empty when none). */
    UFUNCTION(BlueprintPure, Category = "Interaction|Presentation")
    FString GetCaptionText() const;
    UFUNCTION(BlueprintPure, Category = "Interaction|Presentation")
    bool IsCaptionVisible() const;
    /** Zone or channel of the last reaction answered (None after a reset). */
    UFUNCTION(BlueprintPure, Category = "Interaction|Presentation")
    FName GetPresentedZone() const { return PresentedZone; }
    UFUNCTION(BlueprintCallable, Category = "Interaction|Presentation")
    void ResetPresentation();

    /** Disable this presenter when an external UI/audio subscriber handles the same event. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation")
    bool bPresentCaptions = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation")
    bool bPresentSound = true;
    /** Bubble font (ExtraBold typeface); empty uses the engine's bold font. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation")
    TObjectPtr<UFont> BubbleFont;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation")
    FLinearColor BubbleAccent = FLinearColor(1.0f, 0.06f, 0.42f);
    /** Centimetres per bubble pixel: text is ~2 cm tall, readable at arm's length and from 2 m. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation", meta = (ClampMin = "0.005", Units = "cm"))
    float BubbleScale = 0.04f;
    /** From the head bone: up, to the player's right and towards the player, like a manga speech bubble beside
     *  the head (never over the face or tall ears). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation", meta = (Units = "cm"))
    float BubbleAboveHead = 16.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation", meta = (Units = "cm"))
    float BubbleBeside = 26.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation", meta = (Units = "cm"))
    float BubbleTowardsPlayer = 8.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SoundVolume = 0.6f;
    /** Player's voice volume (menu "Голос"), multiplies SoundVolume. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float VoiceVolume = 1.0f;
    /** Reaction sounds come from the touched zone: distance falloff and direction relative to the
     *  listener's head. Empty: BeginPlay creates a spatialized sphere (30 cm inner, 12 m falloff). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation")
    TObjectPtr<USoundAttenuation> ReactionAttenuation;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleContactReaction(FName ZoneName, int32 HandIndex, float HandSpeed, int32 Mood);
    UPROPERTY(Transient) TObjectPtr<UWidgetComponent> Bubble;
    UPROPERTY(Transient) TObjectPtr<UGratiaBubbleWidget> BubbleWidget;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ActiveAudio;
    TWeakObjectPtr<AGratiaPreviewCharacter> Character;
    TWeakObjectPtr<UGratiaInteraction> Source;
    TWeakObjectPtr<UGratiaCharacterProfile> PresentedProfile;
    FText CaptionText;
    FName PresentedZone;
    float CaptionSeconds = 0.0f, CaptionAge = 0.0f;
    double LastVoiceTime = -10.0;
    int32 LastLineIndex = INDEX_NONE;
    /** The take that played last (a line with several takes does not repeat it). */
    TWeakObjectPtr<USoundBase> LastVoice;
    const FGratiaReactionLine* SelectLine(FName ZoneName, int32 Mood, bool bStrong, const UGratiaCharacterProfile& Profile);
    USoundBase* SelectSound(FName ZoneName, const UGratiaCharacterProfile& Profile) const;
    bool EnsureBubble();
    void PlaceBubble();
    void StopSound();
};
