// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkPoseSearchEditor : ModuleRules
{
	public NexusLinkPoseSearchEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_POSE_SEARCH", true);
	}
}
