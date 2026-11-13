// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusControlEditorGraphCapability.h"
#include "Utils/NexusEditorGraphTools.h"
#include "NexusCapabilityRegistry.h"

void FNexusControlEditorGraphCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	FNexusEditorGraphTools::Define(Out, true);
	Out.Name = TEXT("ControlEditorGraph");
	Out.Description = TEXT("Navigate native canvases. Open, select, focus and pan/zoom; no structural edits.");
}
FCapabilityResult FNexusControlEditorGraphCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const { return FNexusEditorGraphTools::Execute(Arguments, true); }
REGISTER_MCP_CAPABILITY(FNexusControlEditorGraphCapability)
