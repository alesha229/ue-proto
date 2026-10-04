using UnrealBuildTool;

public class GratiaVRTarget : TargetRules
{
    public GratiaVRTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("GratiaVR");
    }
}
