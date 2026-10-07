using UnrealBuildTool;

public class GratiaVREditorTools : ModuleRules
{
    public GratiaVREditorTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "GratiaVR", "UnrealEd", "SlateCore", "PhysicsUtilities", "PhysicsCore", "AssetRegistry", "Chaos", "ChaosCore", "Json", "JsonUtilities" });
    }
}
