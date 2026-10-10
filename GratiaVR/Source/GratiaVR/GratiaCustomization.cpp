#include "GratiaCustomization.h"
#include "GratiaCharacterProfile.h"
#include "GratiaPreviewCharacter.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshRenderData.h"

DEFINE_LOG_CATEGORY_STATIC(LogGratiaCustomization, Log, All);

UGratiaCustomization::UGratiaCustomization()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

UGratiaCustomization* UGratiaCustomization::Ensure(AGratiaPreviewCharacter* InCharacter)
{
    if (!InCharacter) return nullptr;
    if (UGratiaCustomization* Existing = InCharacter->FindComponentByClass<UGratiaCustomization>()) return Existing;
    UGratiaCustomization* Component = NewObject<UGratiaCustomization>(InCharacter, TEXT("Customization"));
    InCharacter->AddInstanceComponent(Component);
    Component->RegisterComponent();
    return Component;
}

void UGratiaCustomization::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<AGratiaPreviewCharacter>(GetOwner());
}

const UGratiaCharacterProfile* UGratiaCustomization::GetProfile() const
{
    return Character.IsValid() ? Character->CharacterProfile.Get() : nullptr;
}

int32 UGratiaCustomization::GetSlotCount() const
{
    const UGratiaCharacterProfile* Profile = GetProfile();
    return Profile ? Profile->Customization.Num() : 0;
}

FText UGratiaCustomization::GetSlotLabel(int32 Slot) const
{
    const UGratiaCharacterProfile* Profile = GetProfile();
    if (!Profile || !Profile->Customization.IsValidIndex(Slot)) return FText::GetEmpty();
    const FGratiaCustomizationSlot& Entry = Profile->Customization[Slot];
    return Entry.Label.IsEmpty() ? FText::FromName(Entry.Name) : Entry.Label;
}

FText UGratiaCustomization::GetOptionLabel(int32 Slot) const
{
    const UGratiaCharacterProfile* Profile = GetProfile();
    if (!Profile || !Profile->Customization.IsValidIndex(Slot)) return FText::GetEmpty();
    const TArray<FGratiaCustomizationOption>& Options = Profile->Customization[Slot].Options;
    const int32 Option = GetSelectedOption(Slot);
    return Options.IsValidIndex(Option) ? Options[Option].Label : FText::GetEmpty();
}

bool UGratiaCustomization::SelectOption(int32 Slot, int32 Option)
{
    const UGratiaCharacterProfile* Profile = GetProfile();
    if (!Profile || !Profile->Customization.IsValidIndex(Slot) || !Profile->Customization[Slot].Options.IsValidIndex(Option))
    {
        LastProblem = FString::Printf(TEXT("no option %d in slot %d of %s"), Option, Slot, *GetNameSafe(Profile));
        UE_LOG(LogGratiaCustomization, Warning, TEXT("Customization: %s"), *LastProblem);
        return false;
    }
    Selected.SetNumZeroed(Profile->Customization.Num());
    Selected[Slot] = Option;
    ApplyAll();
    UE_LOG(LogGratiaCustomization, Display, TEXT("Customization %s -> %s"), *GetSlotLabel(Slot).ToString(), *GetOptionLabel(Slot).ToString());
    return true;
}

bool UGratiaCustomization::CycleSlot(int32 Slot)
{
    const UGratiaCharacterProfile* Profile = GetProfile();
    if (!Profile || !Profile->Customization.IsValidIndex(Slot) || Profile->Customization[Slot].Options.IsEmpty()) return false;
    return SelectOption(Slot, (GetSelectedOption(Slot) + 1) % Profile->Customization[Slot].Options.Num());
}

void UGratiaCustomization::Restore()
{
    USkeletalMeshComponent* Mesh = Character.IsValid() ? Character->CharacterMesh.Get() : nullptr;
    if (Mesh)
    {
        const FSkeletalMeshRenderData* Render = Mesh->GetSkeletalMeshAsset() ? Mesh->GetSkeletalMeshAsset()->GetResourceForRendering() : nullptr;
        for (const int32 Slot : HiddenSlots)
            for (int32 Lod = 0; Render && Lod < Render->LODRenderData.Num(); ++Lod)
                for (int32 Section = 0; Section < Render->LODRenderData[Lod].RenderSections.Num(); ++Section)
                    if (Render->LODRenderData[Lod].RenderSections[Section].MaterialIndex == Slot) Mesh->ShowMaterialSection(Slot, Section, true, Lod);
        for (const TPair<int32, TObjectPtr<UMaterialInterface>>& Original : OriginalMaterials) Mesh->SetMaterial(Original.Key, Original.Value);
        for (const FName Morph : HeldMorphs) Mesh->SetMorphTarget(Morph, 0.0f);
    }
    HiddenSlots.Reset();
    OriginalMaterials.Reset();
    HeldMorphs.Reset();
    MorphWeights.Reset();
    for (USceneComponent* Attachment : Attachments) if (Attachment) Attachment->DestroyComponent();
    Attachments.Reset();
}

void UGratiaCustomization::ApplyAll()
{
    Restore();
    const UGratiaCharacterProfile* Profile = GetProfile();
    USkeletalMeshComponent* Mesh = Character.IsValid() ? Character->CharacterMesh.Get() : nullptr;
    AppliedProfile = Profile;
    if (!Profile || !Mesh || !Mesh->GetSkeletalMeshAsset()) return;
    Selected.SetNumZeroed(Profile->Customization.Num());
    const FSkeletalMeshRenderData* Render = Mesh->GetSkeletalMeshAsset()->GetResourceForRendering();
    LastProblem.Reset();
    for (int32 SlotIndex = 0; SlotIndex < Profile->Customization.Num(); ++SlotIndex)
    {
        const FGratiaCustomizationSlot& Slot = Profile->Customization[SlotIndex];
        if (!Slot.Options.IsValidIndex(Selected[SlotIndex])) continue;
        const FGratiaCustomizationOption& Option = Slot.Options[Selected[SlotIndex]];
        for (const FName MaterialSlot : Option.HiddenMaterialSlots)
        {
            const int32 Index = Mesh->GetMaterialIndex(MaterialSlot);
            if (Index == INDEX_NONE) { LastProblem += FString::Printf(TEXT("no material slot %s; "), *MaterialSlot.ToString()); continue; }
            HiddenSlots.Add(Index);
            for (int32 Lod = 0; Render && Lod < Render->LODRenderData.Num(); ++Lod)
                for (int32 Section = 0; Section < Render->LODRenderData[Lod].RenderSections.Num(); ++Section)
                    if (Render->LODRenderData[Lod].RenderSections[Section].MaterialIndex == Index) Mesh->ShowMaterialSection(Index, Section, false, Lod);
        }
        for (const TPair<FName, TObjectPtr<UMaterialInterface>>& Replacement : Option.Materials)
        {
            const int32 Index = Mesh->GetMaterialIndex(Replacement.Key);
            if (Index == INDEX_NONE || !Replacement.Value) { LastProblem += FString::Printf(TEXT("material %s not applied; "), *Replacement.Key.ToString()); continue; }
            if (!OriginalMaterials.Contains(Index)) OriginalMaterials.Add(Index, Mesh->GetMaterial(Index));
            Mesh->SetMaterial(Index, Replacement.Value);
        }
        for (const TPair<FName, float>& Morph : Option.Morphs)
        {
            MorphWeights.Add(Morph.Key, Morph.Value);
            HeldMorphs.AddUnique(Morph.Key);
        }
        if (Option.SkinnedAttachment)
        {
            USkeletalMeshComponent* Part = NewObject<USkeletalMeshComponent>(GetOwner());
            Part->SetSkeletalMesh(Option.SkinnedAttachment);
            Part->SetupAttachment(Mesh);
            Part->SetLeaderPoseComponent(Mesh);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->RegisterComponent();
            Attachments.Add(Part);
        }
        if (Option.StaticAttachment)
        {
            UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(GetOwner());
            Part->SetStaticMesh(Option.StaticAttachment);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetupAttachment(Mesh, Option.AttachBone);
            Part->SetRelativeTransform(Option.AttachTransform);
            Part->RegisterComponent();
            Attachments.Add(Part);
        }
    }
    if (!LastProblem.IsEmpty()) UE_LOG(LogGratiaCustomization, Warning, TEXT("Customization of %s: %s"), *Profile->GetName(), *LastProblem);
}

void UGratiaCustomization::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    const UGratiaCharacterProfile* Profile = GetProfile();
    if (AppliedProfile.Get() != Profile)
    {
        // Another character profile: its own slots start at their defaults.
        Selected.Reset();
        ApplyAll();
    }
    // Morph holds are re-applied each frame (animation curves may write the same targets).
    if (USkeletalMeshComponent* Mesh = Character.IsValid() ? Character->CharacterMesh.Get() : nullptr)
        for (const TPair<FName, float>& Morph : MorphWeights) Mesh->SetMorphTarget(Morph.Key, Morph.Value, false);
}

FString UGratiaCustomization::GetDiagnostics() const
{
    FString Result = FString::Printf(TEXT("customization: %d slots"), GetSlotCount());
    for (int32 Slot = 0; Slot < GetSlotCount(); ++Slot) Result += FString::Printf(TEXT(", %s=%s"), *GetSlotLabel(Slot).ToString(), *GetOptionLabel(Slot).ToString());
    if (!LastProblem.IsEmpty()) Result += TEXT(" problems: ") + LastProblem;
    return Result;
}
