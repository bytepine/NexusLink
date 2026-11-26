// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkSemanticSearchEditor : ModuleRules
{
	public NexusLinkSemanticSearchEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		var Search = NexusLinkOptionalPlugins.Opt(new[] { "SemanticSearch.uplugin" },
			"WITH_NEXUS_SEMANTIC_SEARCH", "NEXUS_SEMANTIC_SEARCH", 5, 8, rt: new[] { "SemanticSearch" });
		if (NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, Search, false))
			PrivateDependencyModuleNames.Add("DerivedDataCache");
	}
}
