// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkMetasoundEditor : ModuleRules
{
	public NexusLinkMetasoundEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_METASOUND", true);
	}
}
