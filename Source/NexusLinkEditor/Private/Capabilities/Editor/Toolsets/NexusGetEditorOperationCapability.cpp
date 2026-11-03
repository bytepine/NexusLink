// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusGetEditorOperationCapability.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"
#include "Utils/NexusArgs.h"

void FNexusGetEditorOperationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetEditorOperation");
	Out.Description = TEXT("Read owned long-task state and results. Optional bounded wait; never caches.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("OperationId"), FNexusSchema::Str(TEXT("Owned operation; omit to list recent owned tasks")))
		.Prop(TEXT("bRunningOnly"), FNexusSchema::Bool(TEXT("Only running tasks when listing"), true, false))
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Most recent owned task summaries"), 50, 1, 128))
		.Prop(TEXT("WaitSeconds"), FNexusSchema::Int(TEXT("Wait up to thirty seconds; requires OperationId"), 0, 0, 30)).Build();
	FNexusEditorToolSchema::Configure(Out, false);
	Out.OutputSchema->SetArrayField(TEXT("required"), {MakeShared<FJsonValueString>(TEXT("TtlSeconds"))});
	Out.OutputSchema->GetObjectField(TEXT("properties"))->SetObjectField(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Owned task summaries; list mode only"), FNexusSchema::AnyObject(TEXT("Operation summary"))));
	Out.OutputSchema->GetObjectField(TEXT("properties"))->SetObjectField(TEXT("Operations_defaults"), FNexusSchema::AnyObject(TEXT("Shared summary fields after compaction")));
	Out.RelatedCapabilities = { TEXT("ControlEditorAutomation"), TEXT("CompileEditorLiveCoding"), TEXT("GetEditorSlate") };
}

FCapabilityResult FNexusGetEditorOperationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString Id; Arguments->TryGetStringField(TEXT("OperationId"), Id);
	if (Id.IsEmpty())
	{
		if (FNexusArgs(Arguments).Num(TEXT("WaitSeconds")) > 0) return FCapabilityResult::MakeArgInvalid(TEXT("WaitSeconds requires OperationId"));
		FCapabilityResult Result; Result.TopFields = MakeShared<FJsonObject>();
		Result.TopFields->SetArrayField(TEXT("Operations"), FNexusEditorOperations::List(FNexusArgs(Arguments).Bool(TEXT("bRunningOnly")), static_cast<int32>(FNexusArgs(Arguments).Num(TEXT("Limit"), 50))));
		Result.TopFields->SetNumberField(TEXT("TtlSeconds"), 0);
		return Result;
	}
	auto Operation = FNexusEditorOperations::Find(Id);
	if (!Operation.IsValid()) return FCapabilityResult::MakeArgInvalid(TEXT("Unknown operation in this MCP session"));
	return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusGetEditorOperationCapability)
