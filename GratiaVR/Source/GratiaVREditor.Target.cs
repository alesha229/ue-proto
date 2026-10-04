using UnrealBuildTool;

public class GratiaVREditorTarget : TargetRules
{
    public GratiaVREditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("GratiaVR");
    }
}
