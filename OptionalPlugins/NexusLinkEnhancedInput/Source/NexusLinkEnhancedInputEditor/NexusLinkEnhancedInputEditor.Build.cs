// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkEnhancedInputEditor : ModuleRules
{
	public NexusLinkEnhancedInputEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_ENHANCED_INPUT", true);
	}
}
