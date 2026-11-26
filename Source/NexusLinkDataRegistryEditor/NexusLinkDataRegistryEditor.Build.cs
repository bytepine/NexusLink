// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkDataRegistryEditor : ModuleRules
{
	public NexusLinkDataRegistryEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_DATA_REGISTRY", true);
	}
}
