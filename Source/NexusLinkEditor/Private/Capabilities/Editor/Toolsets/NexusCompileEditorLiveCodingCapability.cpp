// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusCompileEditorLiveCodingCapability.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"

void FNexusCompileEditorLiveCodingCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CompileEditorLiveCoding");
	Out.Description = TEXT("Start nonblocking C++ patch build. Windows only; Live Coding must already be enabled.");
	Out.InputSchema = FNexusSchema::Object().Build();
	FNexusEditorToolSchema::Configure(Out, true);
	Out.RelatedCapabilities = { TEXT("GetEditorOperation") };
}

FCapabilityResult FNexusCompileEditorLiveCodingCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusEditorToolSchema::Result(FNexusEditorToolsets::Compile());
}
REGISTER_MCP_CAPABILITY(FNexusCompileEditorLiveCodingCapability)
