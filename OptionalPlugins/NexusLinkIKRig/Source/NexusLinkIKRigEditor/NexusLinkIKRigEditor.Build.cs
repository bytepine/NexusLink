// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkIKRigEditor : ModuleRules
{
	public NexusLinkIKRigEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_IK_RIG", true);
	}
}
