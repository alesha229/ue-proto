using UnrealBuildTool;

public class GratiaVR : ModuleRules
{
    public GratiaVR(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Profile types are also authored by the editor-only setup module.
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay", "UMG" });
        PrivateDependencyModuleNames.AddRange(new[] { "XRBase", "AudioExtensions", "RenderCore", "RHI", "PhysicsCore", "EnhancedInput",
            "KawaiiPhysics", "AnimGraphRuntime", "Slate", "SlateCore", "ProceduralMeshComponent", "Json" });
        // The channel gym patches the running game with Live Coding (editor and development builds that have it).
        if (Target.bWithLiveCoding) PrivateDependencyModuleNames.Add("LiveCoding");
    }
}
