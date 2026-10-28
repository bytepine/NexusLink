// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusInteractEditorSlateCapability.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"

void FNexusInteractEditorSlateCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractEditorSlate");
	Out.Description = TEXT("Send UI input to a discovered target. Non-transactional; requires confirmation.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Input action"), {TEXT("Click"),TEXT("Type"),TEXT("PressKey"),TEXT("Focus"),TEXT("Hover"),TEXT("Drag"),TEXT("SelectOption"),TEXT("SetChecked"),TEXT("FillForm")}))
		.Required(TEXT("Ref"), FNexusSchema::Str(TEXT("Visible target or form root from UI snapshot")))
		.Prop(TEXT("Text"), FNexusSchema::Str(TEXT("Text to insert")))
		.Prop(TEXT("bReplace"), FNexusSchema::Bool(TEXT("Select all before Type"), true, false))
		.Prop(TEXT("Key"), FNexusSchema::Str(TEXT("Unreal key with optional Ctrl+/Shift+/Alt+/Cmd+")))
		.Prop(TEXT("EndRef"), FNexusSchema::Str(TEXT("Discovered drag destination")))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("Exact option label or checkbox true/false")))
		.Prop(TEXT("Fields"), FNexusSchema::ArrayOf(TEXT("Ordered form fields; prevalidated, not atomic"), FNexusSchema::Object()
			.Required(TEXT("Ref"), FNexusSchema::Str(TEXT("Discovered field under the form root")))
			.Required(TEXT("Value"), FNexusSchema::Str(TEXT("Replacement text, option label or true/false")))
			.Required(TEXT("FieldType"), FNexusSchema::Enum(TEXT("Form field type"), {TEXT("textbox"),TEXT("checkbox"),TEXT("combobox")})).Build().ToSharedRef()))
		.Build();
	FNexusEditorToolSchema::Configure(Out, true);
	Out.RelatedCapabilities = { TEXT("GetEditorOperation") };
}

FCapabilityResult FNexusInteractEditorSlateCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString Action; Arguments->TryGetStringField(TEXT("Action"), Action);
	if ((Action == TEXT("Type") && !Arguments->HasField(TEXT("Text"))) || (Action == TEXT("PressKey") && !Arguments->HasField(TEXT("Key")))) return FCapabilityResult::MakeArgInvalid(TEXT("Type requires Text; PressKey requires Key"));
	if (Action == TEXT("Drag") && !Arguments->HasField(TEXT("EndRef"))) return FCapabilityResult::MakeArgInvalid(TEXT("Drag requires EndRef"));
	if ((Action == TEXT("SelectOption") || Action == TEXT("SetChecked")) && !Arguments->HasField(TEXT("Value"))) return FCapabilityResult::MakeArgInvalid(TEXT("Selection requires Value"));
	return FNexusEditorToolSchema::Result(FNexusEditorToolsets::Slate(Action, Arguments));
}
REGISTER_MCP_CAPABILITY(FNexusInteractEditorSlateCapability)
