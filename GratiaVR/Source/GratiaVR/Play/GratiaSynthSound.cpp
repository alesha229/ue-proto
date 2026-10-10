#include "GratiaSynthSound.h"

UGratiaSynthSound::UGratiaSynthSound(const FObjectInitializer& Initializer) : Super(Initializer)
{
    SetSampleRate(Rate);
    NumChannels = 1;
    SampleByteSize = 2;
    bLooping = false;
    bProcedural = true;
}

void UGratiaSynthSound::Configure(uint8 InShape, float InLength, float InLow, float InHigh, float InGain)
{
    Shape = InShape;
    Length = InLength * FMath::FRandRange(0.85f, 1.15f);
    Low = InLow;
    High = InHigh * FMath::FRandRange(0.9f, 1.1f);
    Gain = InGain;
    bLoop = InShape == 4;
    Seed = uint32(FMath::Rand()) | 1u;
    Duration = bLoop ? INDEFINITELY_LOOPING_DURATION : Length;
}

UGratiaSynthSound* UGratiaSynthSound::MakeVocal(UObject* Outer, EGratiaVocalBank Bank)
{
    UGratiaSynthSound* S = NewObject<UGratiaSynthSound>(Outer ? Outer : GetTransientPackage());
    switch (Bank)
    {
    case EGratiaVocalBank::InhaleSoft:  S->Configure(0, 0.9f, 500.f, 2600.f, 0.25f); break;
    case EGratiaVocalBank::InhaleDeep:  S->Configure(0, 1.3f, 400.f, 3200.f, 0.35f); break;
    case EGratiaVocalBank::Gasp:        S->Configure(0, 0.28f, 700.f, 4200.f, 0.45f); break;
    case EGratiaVocalBank::ExhaleSoft:  S->Configure(1, 1.1f, 250.f, 1800.f, 0.25f); break;
    case EGratiaVocalBank::HeldRelease: S->Configure(1, 0.8f, 250.f, 2400.f, 0.4f); break;
    case EGratiaVocalBank::Broken:      S->Configure(1, 0.9f, 300.f, 2200.f, 0.3f); S->Clicks = 0.5f; S->ClickRate = 7.0f; break;
    case EGratiaVocalBank::Sigh:        S->Configure(2, 1.6f, 200.f, 1600.f, 0.3f); S->Hum = 0.25f; S->HumHz = 200.f; break;
    case EGratiaVocalBank::NonVerbalSoft:   S->Configure(2, 0.7f, 200.f, 900.f, 0.12f); S->Hum = 0.6f; S->HumHz = 230.f; break;
    case EGratiaVocalBank::NonVerbalStrong: S->Configure(2, 0.5f, 300.f, 1400.f, 0.18f); S->Hum = 0.7f; S->HumHz = 290.f; break;
    case EGratiaVocalBank::EarWhisper:  S->Configure(1, 1.2f, 1500.f, 6000.f, 0.18f); break;
    default:                            S->Configure(1, 1.0f, 300.f, 2000.f, 0.25f); break;
    }
    S->HumHz *= FMath::FRandRange(0.95f, 1.05f);
    return S;
}

UGratiaSynthSound* UGratiaSynthSound::MakeFoley(UObject* Outer, EGratiaFoleyBank Bank)
{
    UGratiaSynthSound* S = NewObject<UGratiaSynthSound>(Outer ? Outer : GetTransientPackage());
    switch (Bank)
    {
    case EGratiaFoleyBank::SkinSlide:   S->Configure(4, 1.0f, 150.f, 900.f, 0.3f); break;
    case EGratiaFoleyBank::ClothRustle: S->Configure(4, 1.0f, 800.f, 5000.f, 0.25f); S->Clicks = 0.35f; S->ClickRate = 40.0f; break;
    case EGratiaFoleyBank::WetSlide:    S->Configure(4, 1.0f, 200.f, 1500.f, 0.25f); S->Clicks = 0.6f; S->ClickRate = 12.0f; break;
    case EGratiaFoleyBank::ClothSnap:   S->Configure(1, 0.12f, 600.f, 6000.f, 0.6f); break;
    case EGratiaFoleyBank::ButtonPop:   S->Configure(3, 0.05f, 1500.f, 7000.f, 0.6f); S->Clicks = 1.0f; S->ClickRate = 20.0f; break;
    case EGratiaFoleyBank::ZipperTick:  S->Configure(3, 0.04f, 2500.f, 9000.f, 0.5f); S->Clicks = 1.0f; S->ClickRate = 25.0f; break;
    case EGratiaFoleyBank::StrapSlip:   S->Configure(1, 0.35f, 400.f, 3000.f, 0.4f); break;
    case EGratiaFoleyBank::OilPour:     S->Configure(2, 0.6f, 150.f, 1000.f, 0.3f); S->Clicks = 0.5f; S->ClickRate = 15.0f; break;
    case EGratiaFoleyBank::PropPickup:  S->Configure(1, 0.1f, 300.f, 2500.f, 0.4f); break;
    case EGratiaFoleyBank::PropDrop:    S->Configure(1, 0.25f, 80.f, 1200.f, 0.6f); break;
    default:                            S->Configure(1, 0.2f, 300.f, 2000.f, 0.4f); break;
    }
    return S;
}

float UGratiaSynthSound::Noise()
{
    Seed ^= Seed << 13; Seed ^= Seed >> 17; Seed ^= Seed << 5;
    return float(Seed & 0xFFFF) / 32767.5f - 1.0f;
}

int32 UGratiaSynthSound::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
    OutAudio.SetNumUninitialized(NumSamples * 2);
    int16* Out = reinterpret_cast<int16*>(OutAudio.GetData());
    const float Dt = 1.0f / Rate;
    const float LowCoef = 1.0f - FMath::Exp(-2.0f * PI * High * Dt);   // low-pass at High
    const float HighCoef = FMath::Exp(-2.0f * PI * Low * Dt);          // high-pass at Low
    for (int32 I = 0; I < NumSamples; ++I, ++Sample)
    {
        const float T = Sample * Dt;
        const float U = bLoop ? 0.5f : FMath::Clamp(T / FMath::Max(Length, 0.01f), 0.0f, 1.0f);
        float Env = 1.0f;
        switch (Shape)
        {
        case 0: Env = FMath::Sin(U * PI * 0.5f) * FMath::Min(1.0f, (1.0f - U) * 12.0f); break;
        case 1: Env = FMath::Min(1.0f, U * 25.0f) * FMath::Square(1.0f - U); break;
        case 2: Env = FMath::Sin(U * PI); break;
        case 3: Env = FMath::Exp(-U * 8.0f); break;
        default: Env = 0.75f + 0.25f * FMath::Sin(T * 2.0f * PI * 3.1f); break;
        }
        if (!bLoop && U >= 1.0f) Env = 0.0f;
        // Band-limited noise.
        const float N = Noise();
        LowState += LowCoef * (N - LowState);
        HighState = HighCoef * (HighState + LowState - HighIn);
        HighIn = LowState;
        float Value = HighState * 2.5f;
        // Voiced hum with a slight vibrato.
        if (Hum > 0.0f)
        {
            Phase = FMath::Fmod(Phase + HumHz * (1.0f + 0.01f * FMath::Sin(T * 2.0f * PI * 5.5f)) * Dt, 1.0f);
            const float S = FMath::Sin(Phase * 2.0f * PI);
            Value = Value * (1.0f - Hum) + (S + 0.3f * FMath::Sin(Phase * 4.0f * PI)) * Hum * 0.6f;
        }
        // Clicks: random crackle (cloth, wet) or a decaying click.
        if (Clicks > 0.0f)
        {
            if (Noise() * 0.5f + 0.5f < ClickRate * Dt) Crackle = 1.0f;
            Value += Crackle * Noise() * Clicks;
            Crackle *= 0.995f;
        }
        const float Out01 = FMath::Clamp(Value * Env * Gain, -1.0f, 1.0f);
        Out[I] = int16(Out01 * 32000.0f);
    }
    return NumSamples * 2;
}
