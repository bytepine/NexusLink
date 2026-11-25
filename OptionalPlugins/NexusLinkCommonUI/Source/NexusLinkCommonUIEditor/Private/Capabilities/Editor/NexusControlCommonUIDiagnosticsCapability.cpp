// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusControlCommonUIDiagnosticsCapability.h"
#if WITH_NEXUS_COMMON_UI_DEBUGGING
#include "Utils/NexusCommonUIDiagnostics.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
void FNexusControlCommonUIDiagnosticsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ControlCommonUIDiagnostics");
	Out.Description = TEXT("Start/stop bounded CommonUI PIE event logging without callstacks. Does not replace an active logger; Stop requires this session's OperationId. Read retained events via GetCommonUIInfo.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Capture command"), {TEXT("Start"), TEXT("Stop")}))
		.Prop(TEXT("DurationSeconds"), FNexusSchema::Num(TEXT("Automatic stop after 1-60 seconds, default 10")))
		.Prop(TEXT("BufferSize"), FNexusSchema::Int(TEXT("Ring-buffer capacity"), 512, 64, 4096))
		.Prop(TEXT("OperationId"), FNexusSchema::Str(TEXT("Owned operation to Stop"))).Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor, FNexusMcpTags::Widget};
	Out.bAutoTransaction = false; Out.bAllowBatch = false;
	Out.ExtraSearchKeywords = {TEXT("commonui"), TEXT("events"), TEXT("logging"), TEXT("capture")};
	Out.RelatedCapabilities = {TEXT("GetCommonUIInfo"), TEXT("GetEditorOperation")};
}
FCapabilityResult FNexusControlCommonUIDiagnosticsCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const { return FNexusCommonUIDiagnostics::Control(Arguments); }
REGISTER_MCP_CAPABILITY(FNexusControlCommonUIDiagnosticsCapability)
#endif
