// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Runtime/Input/NexusInteractRuntimeInputCapability.h"
#include "Utils/NexusRuntimeInputUtils.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"

void FNexusInteractRuntimeInputCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractRuntimeInput");
	Out.Description = TEXT("Inject local-player keys/InputActions for 0-30 seconds, then release. Zero holds one game tick. Release uses the owning OperationId; query GetRuntimeInput. No OS input or gameplay-success guarantee.");
	const auto Input = FNexusSchema::Object()
		.Prop(TEXT("Key"), FNexusSchema::Str(TEXT("Scalar key name; exclusive with AssetPath")))
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Discovered InputAction asset path; requires EnhancedInput UE5.7+")))
		.Prop(TEXT("X"), FNexusSchema::Num(TEXT("Input X within [-1,1], default 1")))
		.Prop(TEXT("Y"), FNexusSchema::Num(TEXT("Action Y, default 0")))
		.Prop(TEXT("Z"), FNexusSchema::Num(TEXT("Action Z, default 0"))).Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Command"), {TEXT("Apply"), TEXT("Release")}))
		.Prop(TEXT("Inputs"), FNexusSchema::ArrayOf(TEXT("1-16 simultaneous inputs for Apply"), Input.ToSharedRef()))
		.Prop(TEXT("DurationSeconds"), FNexusSchema::Num(TEXT("Hold 0-30 real seconds; 0 means one actual game tick")))
		.Prop(TEXT("WorldPath"), FNexusSchema::Str(TEXT("Exact play world object path from ControlPie Status; required for ambiguous worlds")))
		.Prop(TEXT("WorldName"), FNexusSchema::Str(TEXT("Optional exact world object name; may be ambiguous across PIE clients")))
		.Prop(TEXT("LocalPlayerIndex"), FNexusSchema::Int(TEXT("Index within selected world's local players"), 0, 0))
		.Prop(TEXT("OperationId"), FNexusSchema::Str(TEXT("Input operation to Release"))).Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime};
	Out.bAutoTransaction = false; Out.bAllowBatch = false;
	Out.ExtraSearchKeywords = {TEXT("input"), TEXT("key"), TEXT("inject"), TEXT("press"), TEXT("release"), TEXT("player")};
	Out.RelatedCapabilities = {TEXT("GetRuntimeInput"), TEXT("ControlPie"), TEXT("CaptureViewport")};
}
FCapabilityResult FNexusInteractRuntimeInputCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusArgs(Arguments).Str(TEXT("Action")) == TEXT("Apply") ? FNexusRuntimeInputUtils::Apply(Arguments) : FNexusRuntimeInputUtils::Query(Arguments, true);
}
REGISTER_MCP_CAPABILITY(FNexusInteractRuntimeInputCapability)
