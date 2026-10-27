// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusCaptureEditorSlateCapability.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"

void FNexusCaptureEditorSlateCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CaptureEditorSlate");
	Out.Description = TEXT("Capture visible UI subtree. Saves a local image; requires a GUI editor.");
	Out.InputSchema = FNexusSchema::Object().Prop(TEXT("Ref"), FNexusSchema::Str(TEXT("Widget ref; empty selects active window"))).Build();
	FNexusEditorToolSchema::Configure(Out, false);
	Out.RelatedCapabilities = { TEXT("GetEditorOperation") };
}

FCapabilityResult FNexusCaptureEditorSlateCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusEditorToolSchema::Result(FNexusEditorToolsets::Slate(TEXT("Capture"), Arguments));
}
REGISTER_MCP_CAPABILITY(FNexusCaptureEditorSlateCapability)
