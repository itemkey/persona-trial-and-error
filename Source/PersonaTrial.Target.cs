using UnrealBuildTool;
public class PersonaTrialTarget : TargetRules
{
    public PersonaTrialTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        WindowsPlatform.WindowsSdkVersion = "10.0.22621.0";
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("PersonaTrial");
    }
}
