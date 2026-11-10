// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Runtime/Input/NexusGetRuntimeInputCapability.h"
#include "Utils/NexusRuntimeInputUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
void FNexusGetRuntimeInputCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetRuntimeInput");
	Out.Description = TEXT("Read this session's input operation and active inputs' observed PlayerInput key/EnhancedInput injection states. bInjecting is tool lifetime, not physical input or confirmed gameplay response.");
	Out.InputSchema = FNexusSchema::Object().Required(TEXT("OperationId"), FNexusSchema::Str(TEXT("OperationId returned by InteractRuntimeInput"))).Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime};
	Out.ExtraSearchKeywords = {TEXT("input"), TEXT("inject"), TEXT("held"), TEXT("player")};
	Out.RelatedCapabilities = {TEXT("InteractRuntimeInput")};
}
FCapabilityResult FNexusGetRuntimeInputCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const { return FNexusRuntimeInputUtils::Query(Arguments, false); }
REGISTER_MCP_CAPABILITY(FNexusGetRuntimeInputCapability)
