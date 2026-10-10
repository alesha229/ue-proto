#include "GratiaPlaySettings.h"

namespace
{
    FGratiaLimbGrabDefinition Limb(const TCHAR* Name, const TCHAR* Root, const TCHAR* Mid, const TCHAR* End, const TCHAR* Pull,
        const FVector& Pole, float PullFollow, float PullMax, bool bPlantFeet)
    {
        FGratiaLimbGrabDefinition L;
        L.Name = Name; L.RootSemantic = Root; L.MidSemantic = Mid; L.EndSemantic = End; L.PullSemantic = Pull;
        L.PoleHint = Pole; L.PullFollow = PullFollow; L.PullMaxCm = PullMax; L.bPlantFeet = bPlantFeet;
        return L;
    }

    FGratiaGroundContactDefinition Ground(const TCHAR* Semantic, const TCHAR* Root, const TCHAR* Mid, float Radius)
    {
        FGratiaGroundContactDefinition G;
        G.Semantic = Semantic; G.RootSemantic = Root; G.MidSemantic = Mid; G.RadiusCm = Radius;
        return G;
    }

    FGratiaArousalStageDefinition Stage(const TCHAR* Name, float Threshold, float PaceMin, float PaceMax, float Rough,
        std::initializer_list<FName> Unlocks)
    {
        FGratiaArousalStageDefinition S;
        S.Name = Name; S.Threshold = Threshold; S.PaceMin = PaceMin; S.PaceMax = PaceMax;
        S.PaceSoft = FMath::Max(6.0f, 0.6f * (PaceMax - PaceMin)); S.RoughSpeed = Rough;
        S.Unlocks = TArray<FName>(Unlocks);
        return S;
    }

    FGratiaSoftGroupTuning Soft(float WorldDamping, float Damping, float Stiffness, float Kick)
    {
        FGratiaSoftGroupTuning T;
        T.WorldDamping = WorldDamping; T.Damping = Damping; T.Stiffness = Stiffness; T.InertiaKick = Kick;
        return T;
    }
}

UGratiaPlaySettings::UGratiaPlaySettings()
{
    // Semantic keys present in the profile mapping convention (Left/Right + UpperArm, Forearm, Hand, Thigh, Shin, Foot).
    // Pole hints in profile axes: elbows bend back and down, knees forward.
    LimbGrabs = {
        Limb(TEXT("LeftWrist"), TEXT("LeftUpperArm"), TEXT("LeftForearm"), TEXT("LeftHand"), TEXT("Chest"), FVector(-1, 0, -0.5), 0.45f, 12.0f, true),
        Limb(TEXT("RightWrist"), TEXT("RightUpperArm"), TEXT("RightForearm"), TEXT("RightHand"), TEXT("Chest"), FVector(-1, 0, -0.5), 0.45f, 12.0f, true),
        Limb(TEXT("LeftAnkle"), TEXT("LeftThigh"), TEXT("LeftShin"), TEXT("LeftFoot"), TEXT("Pelvis"), FVector(1, 0, 0), 0.35f, 15.0f, true),
        Limb(TEXT("RightAnkle"), TEXT("RightThigh"), TEXT("RightShin"), TEXT("RightFoot"), TEXT("Pelvis"), FVector(1, 0, 0), 0.35f, 15.0f, true),
    };
    for (FGratiaLimbGrabDefinition& L : LimbGrabs)
        if (L.EndSemantic.ToString().EndsWith(TEXT("Foot"))) { L.GrabRadiusCm = 10.0f; L.FollowHz = 4.5f; }

    GroundContacts = {
        Ground(TEXT("LeftFoot"), TEXT("LeftThigh"), TEXT("LeftShin"), 4.0f),
        Ground(TEXT("RightFoot"), TEXT("RightThigh"), TEXT("RightShin"), 4.0f),
        Ground(TEXT("LeftHand"), TEXT("LeftUpperArm"), TEXT("LeftForearm"), 3.0f),
        Ground(TEXT("RightHand"), TEXT("RightUpperArm"), TEXT("RightForearm"), 3.0f),
        Ground(TEXT("Pelvis"), TEXT(""), TEXT(""), 9.0f),
        Ground(TEXT("Chest"), TEXT(""), TEXT(""), 10.0f),
        Ground(TEXT("Head"), TEXT(""), TEXT(""), 9.0f),
    };

    SoftBody = Soft(0.7f, 0.85f, 0.9f, 0.45f);
    Hair = Soft(0.75f, 0.9f, 1.0f, 0.25f);
    ClothDecor = Soft(0.8f, 0.9f, 1.0f, 0.2f);
    EarsTail = Soft(0.7f, 0.85f, 1.0f, 0.3f);

    // Pace (cm/s): slow and gentle first, faster later; rough above RoughSpeed. Tags are free names other systems check.
    Stages = {
        Stage(TEXT("Shy"), 0.0f, 3.0f, 15.0f, 70.0f, { TEXT("Garment.Light"), TEXT("Prop.Feather") }),
        Stage(TEXT("Warm"), 0.25f, 5.0f, 25.0f, 90.0f, { TEXT("Garment.Light"), TEXT("Garment.Outer"), TEXT("Prop.Feather"), TEXT("Prop.Oil"), TEXT("Pose.Sit") }),
        Stage(TEXT("Excited"), 0.5f, 8.0f, 40.0f, 120.0f, { TEXT("Garment.Light"), TEXT("Garment.Outer"), TEXT("Garment.Inner"), TEXT("Prop.Feather"), TEXT("Prop.Oil"), TEXT("Prop.Toy"), TEXT("Pose.Sit"), TEXT("Pose.Lie") }),
        Stage(TEXT("Peak"), 0.8f, 10.0f, 60.0f, 160.0f, { TEXT("Garment.Light"), TEXT("Garment.Outer"), TEXT("Garment.Inner"), TEXT("Prop.Feather"), TEXT("Prop.Oil"), TEXT("Prop.Toy"), TEXT("Pose.Sit"), TEXT("Pose.Lie"), TEXT("Pose.All") }),
    };
    MoodGain = { 0.7f, 1.25f, 0.85f };

    FGratiaPropDefinition Oil;
    Oil.Name = TEXT("Oil"); Oil.Kind = EGratiaPropKind::Oil; Oil.Scale = FVector(0.06, 0.06, 0.14);
    Oil.Color = FLinearColor(0.95f, 0.75f, 0.25f); Oil.Tip = FVector(0, 0, 8); Oil.Stimulus = 0.3f; Oil.RequiredUnlock = TEXT("Prop.Oil");
    FGratiaPropDefinition Toy;
    Toy.Name = TEXT("Toy"); Toy.Kind = EGratiaPropKind::Toy; Toy.Scale = FVector(0.035, 0.035, 0.14);
    Toy.Color = FLinearColor(1.0f, 0.3f, 0.65f); Toy.Tip = FVector(0, 0, 7); Toy.Stimulus = 1.6f; Toy.RequiredUnlock = TEXT("Prop.Toy");
    FGratiaPropDefinition Feather;
    Feather.Name = TEXT("Feather"); Feather.Kind = EGratiaPropKind::Feather; Feather.Scale = FVector(0.01, 0.04, 0.2);
    Feather.Color = FLinearColor(0.9f, 0.9f, 1.0f); Feather.Tip = FVector(0, 0, 10); Feather.Stimulus = 0.7f; Feather.RequiredUnlock = TEXT("Prop.Feather");
    FGratiaPropDefinition Ribbon;
    Ribbon.Name = TEXT("Ribbon"); Ribbon.Kind = EGratiaPropKind::Accessory; Ribbon.Scale = FVector(0.06, 0.02, 0.04);
    Ribbon.Color = FLinearColor(0.85f, 0.05f, 0.25f); Ribbon.Tip = FVector::ZeroVector; Ribbon.Stimulus = 0.0f;
    Ribbon.AttachSemantic = TEXT("Head"); Ribbon.AttachTransform = FTransform(FVector(0, 0, 12));
    Props = { Oil, Toy, Feather, Ribbon };
}

float UGratiaPlaySettings::GetZoneWeight(int32 Stage, FName Zone) const
{
    if (Stages.IsValidIndex(Stage))
        if (const float* Weight = Stages[Stage].ZoneWeights.Find(Zone)) return FMath::Max(0.0f, *Weight);
    return DefaultZoneWeight;
}

bool UGratiaPlaySettings::IsUnlocked(int32 Stage, FName Tag) const
{
    if (Tag.IsNone()) return true;
    return Stages.IsValidIndex(Stage) && Stages[Stage].Unlocks.Contains(Tag);
}

bool UGratiaPlaySettings::Validate(TArray<FString>& Errors) const
{
    const int32 Before = Errors.Num();
    float Last = -1.0f;
    for (const FGratiaArousalStageDefinition& S : Stages)
    {
        if (!FMath::IsFinite(S.Threshold) || S.Threshold < Last) Errors.Add(FString::Printf(TEXT("Arousal stage %s: thresholds must ascend."), *S.Name.ToString()));
        if (S.PaceMin > S.PaceMax || S.RoughSpeed <= S.PaceMax) Errors.Add(FString::Printf(TEXT("Arousal stage %s: PaceMin <= PaceMax < RoughSpeed."), *S.Name.ToString()));
        Last = S.Threshold;
    }
    TSet<FName> Names;
    for (const FGratiaGarmentPiece& G : Garments)
    {
        bool bDuplicate = false;
        Names.Add(G.Name, &bDuplicate);
        if (G.Name.IsNone() || bDuplicate) Errors.Add(TEXT("Garment pieces need unique names."));
        if (G.DragAxis.IsNearlyZero() || !FMath::IsFinite(G.TravelCm) || G.TravelCm <= 0.0f) Errors.Add(FString::Printf(TEXT("Garment %s: drag axis and travel required."), *G.Name.ToString()));
        if ((G.Action == EGratiaGarmentAction::Unzip || G.Action == EGratiaGarmentAction::Untie) && G.AnchorSemantic.IsNone())
            Errors.Add(FString::Printf(TEXT("Garment %s: two-hand action needs AnchorSemantic."), *G.Name.ToString()));
    }
    for (const FGratiaGarmentPiece& G : Garments)
        for (FName Required : G.Requires)
            if (!Names.Contains(Required)) Errors.Add(FString::Printf(TEXT("Garment %s requires unknown piece %s."), *G.Name.ToString(), *Required.ToString()));
    for (const FGratiaLimbGrabDefinition& L : LimbGrabs)
        if (L.RootSemantic.IsNone() || L.MidSemantic.IsNone() || L.EndSemantic.IsNone())
            Errors.Add(FString::Printf(TEXT("Limb grab %s: root, mid and end semantics required."), *L.Name.ToString()));
    return Errors.Num() == Before;
}
