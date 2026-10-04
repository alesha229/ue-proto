using UnrealBuildTool;

public class GratiaVR : ModuleRules
{
    public GratiaVR(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay" });
        PrivateDependencyModuleNames.AddRange(new[] { "XRBase", "RenderCore", "RHI", "PhysicsCore", "EnhancedInput" });
    }
}
