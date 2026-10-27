// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusInteractEditorSlateCapability.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"

void FNexusInteractEditorSlateCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractEditorSlate");
	Out.Description = TEXT("Send UI input to a discovered target. Non-transactional; requires confirmation.");
	Out.InputSchema = FNexusSchema::Object().Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Input action"),{TEXT("Click"),TEXT("Type"),TEXT("PressKey"),TEXT("Focus")})).Required(TEXT("Ref"),FNexusSchema::Str(TEXT("Visible target from UI snapshot"))).Prop(TEXT("Text"),FNexusSchema::Str(TEXT("Text to insert"))).Prop(TEXT("Key"),FNexusSchema::Str(TEXT("Unreal key name"))).Build();
	FNexusEditorToolSchema::Configure(Out, true);
	Out.RelatedCapabilities = { TEXT("GetEditorOperation") };
}

FCapabilityResult FNexusInteractEditorSlateCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString Action; Arguments->TryGetStringField(TEXT("Action"), Action);
	if ((Action == TEXT("Type") && !Arguments->HasField(TEXT("Text"))) || (Action == TEXT("PressKey") && !Arguments->HasField(TEXT("Key")))) return FCapabilityResult::MakeArgInvalid(TEXT("Type requires Text; PressKey requires Key"));
	return FNexusEditorToolSchema::Result(FNexusEditorToolsets::Slate(Action, Arguments));
}
REGISTER_MCP_CAPABILITY(FNexusInteractEditorSlateCapability)
