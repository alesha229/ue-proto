#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GratiaMotionProbe.generated.h"

class USceneComponent;
class USkeletalMeshComponent;

/**
 * Motion probe (test infrastructure, -GratiaMotionProbe). Every frame after animation it samples the target character's
 * bones (component space) and the mesh's position, its final morph target weights and the player's visible hands (a
 * point 10 cm ahead of each, so turns count too), and measures how far each sample leaves the straight line between
 * the frames around it. Smooth motion at 90 FPS stays far below a millimetre there; a pop shows as one large step, a
 * jitter as steps that keep changing direction. The worst pops and the most jittery tracks, with the time and what the
 * runtime was doing (SetContext), go to Saved/MotionProbe/report.txt every five seconds and at the end.
 */
UCLASS()
class GRATIAVR_API UGratiaMotionProbe : public UActorComponent
{
    GENERATED_BODY()
public:
    UGratiaMotionProbe();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
    /** What is happening now (a shot, a case); written with each event. */
    void SetContext(const FString& InContext) { Context = InContext; bExternalContext = true; }
    /** Intended jumps (a scene reset, a new case) are not measured: the next Count frames start the tracks afresh. */
    void Skip(int32 Count = 3) { SkipFrames = FMath::Max(SkipFrames, Count); }
    /** Writes the report now. */
    void Write() const;
    /** A step this far off the line counts as a pop: bones and hands in cm, morph weights in weight. */
    float PopCm = 0.25f;
    float PopWeight = 0.05f;
    /** Steps above this that reverse direction from the previous step count as jitter. */
    float JitterCm = 0.04f;
    float JitterWeight = 0.01f;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    struct FTrack
    {
        FString Name;
        bool bWeight = false;
        FVector Sample[3] = {FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector};
        int32 Count = 0;
        FVector LastStep = FVector::ZeroVector;
        double MaxStep = 0.0, SumStep = 0.0, MaxTime = 0.0;
        FString MaxContext;
        int32 Pops = 0, Reversals = 0, Steps = 0;
    };
    struct FEvent { double Time = 0.0; double Size = 0.0; FString Track, Context; };
    void Sample(FTrack& Track, const FVector& Value, double Time);
    void Bind();
    TArray<FTrack> Tracks;
    TArray<FEvent> Events;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
    TWeakObjectPtr<USceneComponent> Hands[2];
    double Times[3] = {0.0, 0.0, 0.0};
    int32 Frames = 0;
    int32 SkipFrames = 0;
    double NextWrite = 5.0;
    double Elapsed = 0.0;
    FString Context;
    bool bExternalContext = false;
    /** Loading and the scene's start are not measured. */
    double WarmupSeconds = 8.0;
    FVector LastMesh = FVector::ZeroVector;
    bool bHaveLastMesh = false;
    int32 Teleports = 0;
    /** -GratiaMotionProbeSeconds=N: write the report and quit after N seconds (unset: -1). */
    double QuitSeconds = -1.0;
};
