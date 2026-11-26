// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkNiagaraEditor : ModuleRules
{
	public NexusLinkNiagaraEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);
		NexusLinkEditor.AddStandardDependencies(this, Target);
		PrivateDependencyModuleNames.Add("NexusLinkEditor");
		NexusLinkOptionalPlugins.ConfigureIntegration(this, Target, "WITH_NIAGARA", true);
	}
}
