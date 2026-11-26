// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkControlRigEditor : ModuleRules
{
	public NexusLinkControlRigEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_CONTROL_RIG", true);
	}
}
