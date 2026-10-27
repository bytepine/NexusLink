// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusGetEditorOperationCapability.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"

void FNexusGetEditorOperationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetEditorOperation");
	Out.Description = TEXT("Read owned long-task state and results. Optional bounded wait; never caches.");
	Out.InputSchema = FNexusSchema::Object().Required(TEXT("OperationId"),FNexusSchema::Str(TEXT("Operation from this MCP session"))).Prop(TEXT("WaitSeconds"),FNexusSchema::Int(TEXT("Wait up to thirty seconds"),0,0,30)).Build();
	FNexusEditorToolSchema::Configure(Out, false);
	Out.RelatedCapabilities = { TEXT("ControlEditorAutomation"), TEXT("CompileEditorLiveCoding"), TEXT("GetEditorSlate") };
}

FCapabilityResult FNexusGetEditorOperationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString Id; Arguments->TryGetStringField(TEXT("OperationId"), Id);
	auto Operation = FNexusEditorOperations::Find(Id);
	if (!Operation.IsValid()) return FCapabilityResult::MakeArgInvalid(TEXT("Unknown operation in this MCP session"));
	return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusGetEditorOperationCapability)
