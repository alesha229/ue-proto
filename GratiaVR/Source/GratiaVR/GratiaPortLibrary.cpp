#include "GratiaPortLibrary.h"
#include "GratiaSecondaryBones.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

namespace
{
const TCHAR* CoreBones[] =
{
    TEXT("root"), TEXT("DEF-spine"), TEXT("DEF-spine_001"), TEXT("DEF-spine_002"),
    TEXT("DEF-spine_003"), TEXT("DEF-spine_004"), TEXT("DEF-spine_005"), TEXT("DEF-spine_006"),
    TEXT("DEF-shoulder_L"), TEXT("DEF-shoulder_R"),
    TEXT("DEF-upper_arm_L"), TEXT("DEF-upper_arm_L_001"), TEXT("DEF-upper_arm_R"), TEXT("DEF-upper_arm_R_001"),
    TEXT("DEF-forearm_L"), TEXT("DEF-forearm_L_001"), TEXT("DEF-forearm_R"), TEXT("DEF-forearm_R_001"),
    TEXT("DEF-hand_L"), TEXT("DEF-hand_R"),
    TEXT("DEF-thigh_L"), TEXT("DEF-thigh_L_001"), TEXT("DEF-thigh_R"), TEXT("DEF-thigh_R_001"),
    TEXT("DEF-shin_L"), TEXT("DEF-shin_L_001"), TEXT("DEF-shin_R"), TEXT("DEF-shin_R_001"),
    TEXT("DEF-foot_L"), TEXT("DEF-foot_R")
};

void ConfigureBody(USkeletalBodySetup* Body, const FGratiaSecondaryBoneDef* Definition)
{
    const bool bSecondary = Definition && Definition->bSafeDefaultSimulation;
    Body->PhysicsType = bSecondary ? PhysType_Simulated : PhysType_Kinematic;
    Body->bConsiderForBounds = !bSecondary;
    Body->bSkipScaleFromAnimation = true;
    Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
    FBodyInstance& Instance = Body->DefaultInstance;
    Instance.SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
    Instance.SetObjectType(ECC_PhysicsBody);
    Instance.SetResponseToAllChannels(ECR_Ignore);
    Instance.SetEnableGravity(false);
    Instance.LinearDamping = bSecondary ? 3.0f : 0.0f;
    Instance.AngularDamping = bSecondary ? 6.0f : 0.0f;
    Instance.SetPositionSolverIterationCount(8);
    Instance.SetVelocitySolverIterationCount(2);
    const uint8 Group = Definition ? Definition->Group : 0;
    Instance.SetMassOverride(Group == 3 ? 0.25f : Group == 4 ? 0.035f : Group == 2 ? 0.025f : 0.018f);

    if (bSecondary)
    {
        // Keep the PhAT orientation fitted in imported bone space, but bound
        // size and inertia. Blender's metre-space bone axes are not UE axes.
        const float SourceLength = FMath::Clamp(Definition->SourceRestLengthCm, 0.5f, 30.0f);
        if (Body->AggGeom.SphylElems.Num() > 0)
        {
            FKSphylElem Capsule = Body->AggGeom.SphylElems[0];
            Capsule.Radius = FMath::Clamp(Capsule.Radius, 0.5f, Group == 3 ? 3.0f : 2.0f);
            Capsule.Length = FMath::Clamp(Capsule.Length, 0.1f, SourceLength);
            Capsule.Center = Capsule.Center.GetClampedToMaxSize(SourceLength);
            Body->AggGeom.EmptyElements();
            Body->AggGeom.SphylElems.Add(Capsule);
        }
        else
        {
            // A leaf without dominant vertices still needs a controlled body.
            // A sphere avoids inventing a converted local longitudinal axis.
            Body->AggGeom.EmptyElements();
            Body->AggGeom.SphereElems.Add(FKSphereElem(FMath::Clamp(SourceLength * 0.12f, 0.5f, 2.0f)));
        }

        Body->AddPhysicalAnimationProfile(TEXT("GratiaStable"));
        if (FPhysicalAnimationProfile* Profile = Body->FindPhysicalAnimationProfile(TEXT("GratiaStable")))
        {
            FPhysicalAnimationData& Drive = Profile->PhysicalAnimationData;
            Drive.bIsLocalSimulation = true;
            Drive.OrientationStrength = Group == 3 ? 650.0f : Group == 4 ? 450.0f : Group == 2 ? 260.0f : 180.0f;
            Drive.AngularVelocityStrength = Group == 3 ? 75.0f : 40.0f;
            Drive.PositionStrength = 0.0f;
            Drive.VelocityStrength = 0.0f;
            Drive.MaxLinearForce = 100.0f;
            Drive.MaxAngularForce = Group == 3 ? 120.0f : 35.0f;
        }
    }
    Body->InvalidatePhysicsData();
    Body->CreatePhysicsMeshes();
}
}
#endif

UPhysicsAsset* UGratiaPortLibrary::BuildPhysicsAsset(USkeletalMesh* SkeletalMesh, const FString& AssetPackagePath)
{
#if WITH_EDITOR
    if (!IsInGameThread() || !IsValid(SkeletalMesh) || !AssetPackagePath.StartsWith(TEXT("/Game/Gratia/"))
        || !FPackageName::IsValidLongPackageName(AssetPackagePath))
    {
        UE_LOG(LogTemp, Error, TEXT("Gratia physical port requires game thread, mesh, and /Game/Gratia package path."));
        return nullptr;
    }
    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPackagePath);
    UPackage* Package = CreatePackage(*AssetPackagePath);
    const FString ObjectPath = AssetPackagePath + TEXT(".") + AssetName;
    UPhysicsAsset* Asset = FindObject<UPhysicsAsset>(Package, *AssetName);
    if (!Asset && FPackageName::DoesPackageExist(AssetPackagePath))
    {
        Asset = LoadObject<UPhysicsAsset>(nullptr, *ObjectPath);
    }
    const bool bNewAsset = !Asset;
    if (!Asset)
    {
        Asset = NewObject<UPhysicsAsset>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    }
    Asset->Modify();
    Asset->SkeletalBodySetups.Empty();
    Asset->ConstraintSetup.Empty();
    Asset->CollisionDisableTable.Empty();
    Asset->UpdateBodySetupIndexMap();
    Asset->UpdateBoundsBodiesArray();
    Asset->SetPreviewMesh(SkeletalMesh);

    FPhysAssetCreateParams Params;
    Params.MinBoneSize = 0.0f;
    Params.MinWeldSize = 0.0f;
    Params.bBodyForAll = true;
    Params.bAlwaysUseVertices = true;
    Params.bIncludeChildBones = false;
    Params.bAutoOrientToBone = true;
    Params.bCreateConstraints = false;
    Params.bDisableCollisionsByDefault = true;
    Params.GeomType = EFG_Sphyl;
    Params.VertWeight = EVW_DominantWeight;
    FText Error;
    if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(Asset, SkeletalMesh, Params, Error, false, false))
    {
        UE_LOG(LogTemp, Error, TEXT("Gratia PhysicsAsset generation failed: %s"), *Error.ToString());
        return nullptr;
    }

    const FReferenceSkeleton& Skeleton = SkeletalMesh->GetRefSkeleton();
    TSet<FName> Wanted;
    for (const TCHAR* Name : CoreBones)
    {
        if (Skeleton.FindBoneIndex(FName(Name)) != INDEX_NONE)
        {
            Wanted.Add(FName(Name));
        }
    }
    for (const FGratiaSecondaryBoneDef& Definition : GratiaSecondaryBones::Definitions)
    {
        if (Skeleton.FindBoneIndex(FName(Definition.Name)) != INDEX_NONE)
        {
            Wanted.Add(FName(Definition.Name));
        }
    }
    for (int32 Index = Asset->SkeletalBodySetups.Num() - 1; Index >= 0; --Index)
    {
        if (!Wanted.Contains(Asset->SkeletalBodySetups[Index]->BoneName))
        {
            FPhysicsAssetUtils::DestroyBody(Asset, Index);
        }
    }
    for (const FName Name : Wanted)
    {
        int32 Index = Asset->FindBodyIndex(Name);
        if (Index == INDEX_NONE)
        {
            Index = FPhysicsAssetUtils::CreateNewBody(Asset, Name, Params);
            Asset->SkeletalBodySetups[Index]->AggGeom.SphereElems.Add(FKSphereElem(1.0f));
        }
        ConfigureBody(Asset->SkeletalBodySetups[Index], GratiaSecondaryBones::Find(Name));
    }
    Asset->UpdateBodySetupIndexMap();
    Asset->UpdateBoundsBodiesArray();

    TArray<FTransform> Rest;
    Rest.SetNum(Skeleton.GetNum());
    for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
    {
        const int32 Parent = Skeleton.GetParentIndex(Index);
        Rest[Index] = Skeleton.GetRefBonePose()[Index];
        if (Parent != INDEX_NONE)
        {
            Rest[Index] = Rest[Index] * Rest[Parent];
        }
    }
    int32 SecondaryCount = 0;
    for (USkeletalBodySetup* Body : Asset->SkeletalBodySetups)
    {
        const FGratiaSecondaryBoneDef* Definition = GratiaSecondaryBones::Find(Body->BoneName);
        if (!Definition || !Definition->bSafeDefaultSimulation)
        {
            continue;
        }
        ++SecondaryCount;
        const int32 ChildIndex = Skeleton.FindBoneIndex(Body->BoneName);
        int32 ParentIndex = Skeleton.GetParentIndex(ChildIndex);
        while (ParentIndex != INDEX_NONE && Asset->FindBodyIndex(Skeleton.GetBoneName(ParentIndex)) == INDEX_NONE)
        {
            ParentIndex = Skeleton.GetParentIndex(ParentIndex);
        }
        if (ParentIndex == INDEX_NONE)
        {
            UE_LOG(LogTemp, Error, TEXT("Gratia secondary body has no anchored ancestor: %s"), *Body->BoneName.ToString());
            Body->PhysicsType = PhysType_Kinematic;
            continue;
        }
        const int32 ConstraintIndex = FPhysicsAssetUtils::CreateNewConstraint(Asset, Body->BoneName);
        if (ConstraintIndex == INDEX_NONE)
        {
            UE_LOG(LogTemp, Error, TEXT("Gratia constraint creation failed: %s"), *Body->BoneName.ToString());
            return nullptr;
        }
        UPhysicsConstraintTemplate* Constraint = Asset->ConstraintSetup[ConstraintIndex];
        FConstraintInstance& Joint = Constraint->DefaultInstance;
        Joint.ConstraintBone1 = Body->BoneName;
        Joint.ConstraintBone2 = Skeleton.GetBoneName(ParentIndex);
        Joint.SetRefFrame(EConstraintFrame::Frame1, FTransform::Identity);
        Joint.SetRefFrame(EConstraintFrame::Frame2, Rest[ChildIndex].GetRelativeTransform(Rest[ParentIndex]));
        Joint.SetLinearLimits(LCM_Locked, LCM_Locked, LCM_Locked, 0.0f);
        const float Swing = Definition->Group == 3 ? 7.0f : Definition->Group == 4 ? 10.0f : 15.0f;
        const float Twist = Definition->Group == 3 ? 4.0f : 6.0f;
        Joint.SetAngularSwing1Limit(ACM_Limited, Swing);
        Joint.SetAngularSwing2Limit(ACM_Limited, Swing);
        Joint.SetAngularTwistLimit(ACM_Limited, Twist);
        Joint.SetDisableCollision(true);
        Joint.SetParentDominates(true);
        Joint.SetProjectionParams(true, 0.2f, 0.2f, 0.5f, 8.0f);
        Joint.SetAngularDriveMode(EAngularDriveMode::SLERP);
        Joint.SetOrientationDriveSLERP(true);
        Joint.SetAngularDriveParams(Definition->Group == 3 ? 200.0f : 80.0f, 12.0f, 60.0f);
        Constraint->SetDefaultProfile(Joint);
    }

    // The source model has many overlapping hair/cloth surfaces. Self/contact
    // filtering is deliberate; native hand/body query proxies own interactions.
    Asset->CollisionDisableTable.Empty();
    for (int32 A = 0; A < Asset->SkeletalBodySetups.Num(); ++A)
    {
        for (int32 B = A + 1; B < Asset->SkeletalBodySetups.Num(); ++B)
        {
            Asset->DisableCollision(A, B);
        }
    }
    Package->GetMetaData().SetValue(Asset, TEXT("Gratia.SourcePhysics"), TEXT("Blender MCP groups; retuned Chaos constraints, not direct cloth/driver transfer"));
    Package->GetMetaData().SetValue(Asset, TEXT("Gratia.SecondaryBodies"), *FString::FromInt(SecondaryCount));
    Package->GetMetaData().SetValue(Asset, TEXT("Gratia.Groups"), TEXT("1=hair;2=cloth;3=body;4=ears/tail;thigh core remains kinematic"));
    SkeletalMesh->Modify();
    SkeletalMesh->SetPhysicsAsset(Asset);
    SkeletalMesh->MarkPackageDirty();
    Asset->MarkPackageDirty();
    if (bNewAsset)
    {
        FAssetRegistryModule::AssetCreated(Asset);
    }
    Asset->PostEditChange();
    UE_LOG(LogTemp, Display, TEXT("GRATIA_PHYSICS_PORT asset=%s bodies=%d secondary=%d constraints=%d collision_pairs_disabled=%d"),
        *Asset->GetPathName(), Asset->SkeletalBodySetups.Num(), SecondaryCount,
        Asset->ConstraintSetup.Num(), Asset->CollisionDisableTable.Num());
    return Asset;
#else
    UE_LOG(LogTemp, Error, TEXT("BuildPhysicsAsset is available only in the editor build."));
    return nullptr;
#endif
}
