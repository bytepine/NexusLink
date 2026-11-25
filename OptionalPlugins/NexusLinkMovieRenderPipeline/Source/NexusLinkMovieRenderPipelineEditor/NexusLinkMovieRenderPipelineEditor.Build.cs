// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkMovieRenderPipelineEditor : ModuleRules
{
	public NexusLinkMovieRenderPipelineEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_MOVIE_RENDER_PIPELINE", true);
	}
}
