// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusGetEditorGraphCapability.h"
#include "Utils/NexusEditorGraphTools.h"
#include "NexusCapabilityRegistry.h"

void FNexusGetEditorGraphCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	FNexusEditorGraphTools::Define(Out, false);
	Out.Name = TEXT("GetEditorGraph");
	Out.Description = TEXT("Inspect native canvases. Graph paths, layout, selection and viewport; no UI input.");
}
FCapabilityResult FNexusGetEditorGraphCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const { return FNexusEditorGraphTools::Execute(Arguments, false); }
REGISTER_MCP_CAPABILITY(FNexusGetEditorGraphCapability)
