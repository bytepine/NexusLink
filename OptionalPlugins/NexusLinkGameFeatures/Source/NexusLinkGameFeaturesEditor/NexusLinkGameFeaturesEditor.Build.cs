// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkGameFeaturesEditor : ModuleRules
{
	public NexusLinkGameFeaturesEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_GAME_FEATURES", true);
	}
}
