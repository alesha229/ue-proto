using UnrealBuildTool;

public class GratiaVREditorTools : ModuleRules
{
    public GratiaVREditorTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "GratiaVR", "UnrealEd", "PhysicsUtilities", "PhysicsCore", "AssetRegistry", "ClothingSystemRuntimeCommon", "ClothingSystemRuntimeInterface", "ClothingSystemEditor", "ChaosCloth", "Chaos", "ChaosCore", "Json", "JsonUtilities" });
    }
}
