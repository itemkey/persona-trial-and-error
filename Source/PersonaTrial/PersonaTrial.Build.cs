using UnrealBuildTool;
public class PersonaTrial : ModuleRules
{
    public PersonaTrial(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore",
            "AIModule", "NavigationSystem", "GameplayTasks", "GameplayAbilities", "GameplayTags",
            "OnlineSubsystem", "OnlineSubsystemUtils", "CoreOnline", "NetCore", "AnimGraphRuntime", "AnimationCore", "Json", "RHI", "RenderCore"
        });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "AssetRegistry" });
    }
}
