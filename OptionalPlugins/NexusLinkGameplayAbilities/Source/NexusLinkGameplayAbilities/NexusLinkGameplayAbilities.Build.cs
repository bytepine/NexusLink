// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkGameplayAbilities : ModuleRules
{
	public NexusLinkGameplayAbilities(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		PrivateDependencyModuleNames.AddRange(new[] { "NexusLink", "Core", "CoreUObject", "Engine", "Json", "InputCore", "GameplayTags" });
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_GAS", false);
	}
}
