#include "GratiaSceneLibrary.h"
#include "Kismet/GameplayStatics.h"

FVector4f UGratiaMusicAnalysis::Sample(float Seconds) const
{
    if (Frames.IsEmpty() || !FMath::IsFinite(Seconds) || Seconds < 0.0f || !FMath::IsFinite(FramesPerSecond) || FramesPerSecond <= 0.0f) return FVector4f(0, 0, 0, 0);
    const double Position = double(Seconds) * FramesPerSecond;
    if (Position >= Frames.Num()) return FVector4f(0, 0, 0, 0);
    const int32 Index = FMath::FloorToInt(Position);
    if (Index >= Frames.Num()) return FVector4f(0, 0, 0, 0);
    const FVector4f& A = Frames[Index];
    const FVector4f& B = Frames[FMath::Min(Index + 1, Frames.Num() - 1)];
    const float T = float(Position - Index);
    FVector4f Result = A + (B - A) * T;
    for (int32 Band = 0; Band < 4; ++Band)
        Result[Band] = FMath::IsFinite(Result[Band]) ? FMath::Clamp(Result[Band], 0.0f, 1.0f) : 0.0f;
    return Result;
}

UGratiaUserSettings* UGratiaUserSettings::Load()
{
    if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
        if (auto* Loaded = Cast<UGratiaUserSettings>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
        {
            if (Loaded->Version >= 1 && Loaded->Version <= CurrentVersion)
            {
                if (Loaded->Version < 2) Loaded->bSprings = true;
                Loaded->Sanitize();
                return Loaded;
            }
            UE_LOG(LogTemp, Warning, TEXT("SCENE_SETTINGS unsupported schema %d; using defaults"), Loaded->Version);
        }
    return Cast<UGratiaUserSettings>(UGameplayStatics::CreateSaveGameObject(UGratiaUserSettings::StaticClass()));
}

void UGratiaUserSettings::Sanitize()
{
    Version = CurrentVersion;
    Quality = FMath::Clamp(Quality, 0, 2);
    MusicVolume = FMath::Clamp(FMath::IsFinite(MusicVolume) ? MusicVolume : 1.0f, 0.0f, 2.0f);
    HapticsScale = FMath::Clamp(FMath::IsFinite(HapticsScale) ? HapticsScale : 1.0f, 0.0f, 1.0f);
    HeightOffsetCm = FMath::Clamp(FMath::IsFinite(HeightOffsetCm) ? HeightOffsetCm : 0.0f, -100.0f, 100.0f);
}

bool UGratiaUserSettings::Save()
{
    Sanitize();
    return UGameplayStatics::SaveGameToSlot(this, SlotName, 0);
}
