using UnrealBuildTool;
public class PersonaTrialEditorTarget : TargetRules
{
    public PersonaTrialEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        WindowsPlatform.WindowsSdkVersion = "10.0.22621.0";
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("PersonaTrial");
    }
}
