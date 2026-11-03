// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusControlEditorOperationCapability.h"
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"

void FNexusControlEditorOperationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ControlEditorOperation");
	Out.Description = TEXT("Request cancellation of an owned task. Only tasks exposing a stop callback can be cancelled.");
	Out.InputSchema = FNexusSchema::Object().Required(TEXT("OperationId"), FNexusSchema::Str(TEXT("Owned running operation with bCanCancel=true"))).Build();
	FNexusEditorToolSchema::Configure(Out, true);
	Out.RelatedCapabilities = {TEXT("GetEditorOperation")};
}

FCapabilityResult FNexusControlEditorOperationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FString Id = FNexusArgs(Arguments).Str(TEXT("OperationId"));
	const auto Operation = FNexusEditorOperations::Find(Id);
	if (!Operation) return FCapabilityResult::MakeArgInvalid(TEXT("Unknown operation in this MCP session"));
	if (!FNexusEditorOperations::Cancel(Id)) return FCapabilityResult::MakeArgInvalid(TEXT("Task is terminal or does not support cancellation"));
	return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusControlEditorOperationCapability)
