// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkPaper2DEditor : ModuleRules
{
	public NexusLinkPaper2DEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_PAPER2D", true);
	}
}
