using UnrealBuildTool;

public class GratiaVR : ModuleRules
{
    public GratiaVR(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // GratiaSourceClothingAsset is also authored by the editor-only port module.
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay",
            "ClothingSystemRuntimeInterface", "ClothingSystemRuntimeCommon" });
        PrivateDependencyModuleNames.AddRange(new[] { "XRBase", "RenderCore", "RHI", "PhysicsCore", "EnhancedInput",
            "ChaosCloth", "Chaos" });
    }
}
