// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkCommonUIEditor : ModuleRules
{
	public NexusLinkCommonUIEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_COMMON_UI", true);
		var Diagnostics = NexusLinkOptionalPlugins.Opt(new[] { "CommonUI.uplugin" },
			"WITH_NEXUS_COMMON_UI_DEBUGGING", "NEXUS_COMMON_UI_DEBUGGING", 5, 9, rt: new[] { "CommonUIDebugging" });
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, Diagnostics, false);
	}
}
