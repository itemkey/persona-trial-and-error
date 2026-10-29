using UnrealBuildTool;
public class PersonaTrialTarget : TargetRules
{
    public PersonaTrialTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("PersonaTrial");
    }
}
