// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusInteractEditorSlateCapability.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"

void FNexusInteractEditorSlateCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractEditorSlate");
	Out.Description = TEXT("Send UI input to a discovered target. Non-transactional; validates visible, enabled targets.");
	auto Axis = FNexusSchema::Num(TEXT("Normalized local coordinate, 0 to 1"));
	Axis->SetNumberField(TEXT("minimum"), 0); Axis->SetNumberField(TEXT("maximum"), 1);
	const auto Position = FNexusSchema::Object().Required(TEXT("X"), Axis).Required(TEXT("Y"), Axis).Build().ToSharedRef();
	auto WheelDelta = FNexusSchema::Num(TEXT("Scroll wheel steps; nonzero, positive scrolls up"));
	WheelDelta->SetNumberField(TEXT("minimum"), -20); WheelDelta->SetNumberField(TEXT("maximum"), 20);
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Input action"), {TEXT("Click"),TEXT("DoubleClick"),TEXT("Scroll"),TEXT("Type"),TEXT("PressKey"),TEXT("Focus"),TEXT("Hover"),TEXT("Drag"),TEXT("SelectOption"),TEXT("SetChecked"),TEXT("FillForm")}))
		.Required(TEXT("Ref"), FNexusSchema::Str(TEXT("Visible target or form root from UI snapshot")))
		.Prop(TEXT("Button"), FNexusSchema::Enum(TEXT("Click/DoubleClick/Drag mouse button"), {TEXT("Left"),TEXT("Right"),TEXT("Middle")}, TEXT("Left")))
		.Prop(TEXT("Modifiers"), FNexusSchema::EnumArr(TEXT("Pointer-event modifiers; does not change platform key state"), {TEXT("Ctrl"),TEXT("Shift"),TEXT("Alt"),TEXT("Cmd")}))
		.Prop(TEXT("Position"), Position)
		.Prop(TEXT("EndPosition"), Position)
		.Prop(TEXT("WheelDelta"), WheelDelta)
		.Prop(TEXT("Text"), FNexusSchema::Str(TEXT("Text to insert")))
		.Prop(TEXT("bReplace"), FNexusSchema::Bool(TEXT("Select all before Type"), true, false))
		.Prop(TEXT("Key"), FNexusSchema::Str(TEXT("Unreal key with optional Ctrl+/Shift+/Alt+/Cmd+")))
		.Prop(TEXT("EndRef"), FNexusSchema::Str(TEXT("Discovered drag destination")))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("Exact option label or checkbox true/false")))
		.Prop(TEXT("Fields"), FNexusSchema::ArrayOf(TEXT("Ordered form fields; prevalidated, not atomic"), FNexusSchema::Object()
			.Required(TEXT("Ref"), FNexusSchema::Str(TEXT("Discovered field under the form root")))
			.Required(TEXT("Value"), FNexusSchema::Str(TEXT("Replacement text, option label or true/false")))
			.Required(TEXT("FieldType"), FNexusSchema::Enum(TEXT("Form field type"), {TEXT("TextBox"),TEXT("CheckBox"),TEXT("ComboBox")})).Build().ToSharedRef()))
		.Build();
	FNexusEditorToolSchema::Configure(Out, true);
	Out.ExtraSearchKeywords = { TEXT("right click"), TEXT("double click"), TEXT("mouse wheel"), TEXT("scroll"), TEXT("context menu"), TEXT("pointer") };
	Out.RelatedCapabilities = { TEXT("GetEditorSlate"), TEXT("CaptureEditorSlate"), TEXT("GetEditorOperation") };
}

FCapabilityResult FNexusInteractEditorSlateCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString Action; Arguments->TryGetStringField(TEXT("Action"), Action);
	const bool bButtonAction = Action == TEXT("Click") || Action == TEXT("DoubleClick") || Action == TEXT("Drag");
	const bool bPointerAction = bButtonAction || Action == TEXT("Hover") || Action == TEXT("Scroll");
	if (!bButtonAction && Arguments->HasField(TEXT("Button"))) return FCapabilityResult::MakeArgInvalid(TEXT("Button requires Click/DoubleClick/Drag"));
	if (!bPointerAction && (Arguments->HasField(TEXT("Modifiers")) || Arguments->HasField(TEXT("Position"))))
		return FCapabilityResult::MakeArgInvalid(TEXT("Modifiers/Position require a pointer action"));
	if (Action != TEXT("Drag") && Arguments->HasField(TEXT("EndPosition"))) return FCapabilityResult::MakeArgInvalid(TEXT("EndPosition requires Drag"));
	double WheelDelta = 0;
	if (Action == TEXT("Scroll") && (!Arguments->TryGetNumberField(TEXT("WheelDelta"), WheelDelta) || !FMath::IsFinite(WheelDelta) || WheelDelta == 0 || FMath::Abs(WheelDelta) > 20))
		return FCapabilityResult::MakeArgInvalid(TEXT("Scroll requires nonzero WheelDelta in [-20,20]"));
	if (Action != TEXT("Scroll") && Arguments->HasField(TEXT("WheelDelta"))) return FCapabilityResult::MakeArgInvalid(TEXT("WheelDelta requires Scroll"));
	if ((Action == TEXT("Type") && !Arguments->HasField(TEXT("Text"))) || (Action == TEXT("PressKey") && !Arguments->HasField(TEXT("Key")))) return FCapabilityResult::MakeArgInvalid(TEXT("Type requires Text; PressKey requires Key"));
	if (Action == TEXT("Drag") && !Arguments->HasField(TEXT("EndRef"))) return FCapabilityResult::MakeArgInvalid(TEXT("Drag requires EndRef"));
	if ((Action == TEXT("SelectOption") || Action == TEXT("SetChecked")) && !Arguments->HasField(TEXT("Value"))) return FCapabilityResult::MakeArgInvalid(TEXT("Selection requires Value"));
	return FNexusEditorToolSchema::Result(FNexusEditorControlServices::Slate(Action, Arguments));
}
REGISTER_MCP_CAPABILITY(FNexusInteractEditorSlateCapability)
