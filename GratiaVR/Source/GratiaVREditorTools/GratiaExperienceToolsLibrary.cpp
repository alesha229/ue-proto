#include "GratiaExperienceToolsLibrary.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

bool UGratiaExperienceToolsLibrary::BuildCompositeFont(UFont* Font, const TArray<FName>& Names, const TArray<UFontFace*>& Faces)
{
    if (!Font || Names.IsEmpty() || Names.Num() != Faces.Num()) return false;
    Font->Modify();
    Font->FontCacheType = EFontCacheType::Runtime;
    FTypeface& Typeface = Font->GetMutableInternalCompositeFont().DefaultTypeface;
    Typeface.Fonts.Reset();
    for (int32 Index = 0; Index < Names.Num(); ++Index)
    {
        if (!Faces[Index]) return false;
        FTypefaceEntry& Entry = Typeface.Fonts.Emplace_GetRef(Names[Index]);
        Entry.Font = FFontData(Faces[Index]);
    }
    Font->LegacyFontName = Names[0];
    Font->MarkPackageDirty();
    return true;
}

UFontFace* UGratiaExperienceToolsLibrary::ImportFontFace(const FString& Filename, const FString& PackageName)
{
    TArray<uint8> Bytes;
    if (!FPackageName::IsValidLongPackageName(PackageName) || !FFileHelper::LoadFileToArray(Bytes, *Filename) || Bytes.IsEmpty()) return nullptr;
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    UFontFace* Face = LoadObject<UFontFace>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!Face)
    {
        UPackage* Package = CreatePackage(*PackageName);
        Face = NewObject<UFontFace>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Face);
    }
    Face->Modify();
    Face->SourceFilename = Filename;
    Face->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(Bytes));
    Face->CacheSubFaces();
    Face->MarkPackageDirty();
    return Face;
}
