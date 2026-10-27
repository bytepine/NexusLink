// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusControlEditorAutomationCapability.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"

void FNexusControlEditorAutomationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ControlEditorAutomation");
	Out.Description = TEXT("Discover or run local tests. Explicit stop uses owned operation id; no rollback.");
	Out.InputSchema = FNexusSchema::Object().Required(TEXT("Action"),FNexusSchema::Enum(TEXT("Controller action"),{TEXT("Discover"),TEXT("Run"),TEXT("Stop")})).Prop(TEXT("TestNames"),FNexusSchema::ArrayOf(TEXT("Full discovered names"),FNexusSchema::Str(TEXT("Full test name")))).Prop(TEXT("OperationId"),FNexusSchema::Str(TEXT("Owned run to stop"))).Build();
	FNexusEditorToolSchema::Configure(Out, true);
	Out.RelatedCapabilities = { TEXT("GetEditorOperation") };
}

FCapabilityResult FNexusControlEditorAutomationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString Action; Arguments->TryGetStringField(TEXT("Action"), Action);
	return FNexusEditorToolSchema::Result(FNexusEditorToolsets::Automation(Action, Arguments));
}
REGISTER_MCP_CAPABILITY(FNexusControlEditorAutomationCapability)
