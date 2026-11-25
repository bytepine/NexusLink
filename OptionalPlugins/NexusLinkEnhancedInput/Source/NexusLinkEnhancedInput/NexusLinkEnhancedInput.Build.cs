// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkEnhancedInput : ModuleRules
{
	public NexusLinkEnhancedInput(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		PrivateDependencyModuleNames.AddRange(new[] { "NexusLink", "Core", "CoreUObject", "Engine", "Json", "InputCore", "GameplayTags" });
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_ENHANCED_INPUT", false);
	}
}
