// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkPCGEditor : ModuleRules
{
	public NexusLinkPCGEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_PCG", true);
		if (PrivateDependencyModuleNames.Contains("PCGEditor"))
		{
			if (!NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 5))
				PrivateDependencyModuleNames.Add("StructUtils");
			if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 7))
				PrivateDependencyModuleNames.Add("AssetDefinition");
		}
	}
}
