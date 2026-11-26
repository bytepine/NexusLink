// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkGeometryCollectionEditor : ModuleRules
{
	public NexusLinkGeometryCollectionEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_GEOMETRY_COLLECTION", true);
	}
}
