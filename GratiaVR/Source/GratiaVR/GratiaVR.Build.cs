using UnrealBuildTool;

public class GratiaVR : ModuleRules
{
    public GratiaVR(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Profile types are also authored by the editor-only setup module.
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay" });
        PrivateDependencyModuleNames.AddRange(new[] { "XRBase", "RenderCore", "RHI", "PhysicsCore", "EnhancedInput",
            "KawaiiPhysics", "AnimGraphRuntime" });
    }
}
