// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkGameplayAbilitiesEditor : ModuleRules
{
	public NexusLinkGameplayAbilitiesEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		PrivateDependencyModuleNames.Add("NexusLinkGameplayAbilities");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_GAS", true);
	}
}
