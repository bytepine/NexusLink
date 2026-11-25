// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkGameplayTagsEditorEditor : ModuleRules
{
	public NexusLinkGameplayTagsEditorEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_NEXUS_TAGS_EDITOR", true);
	}
}
