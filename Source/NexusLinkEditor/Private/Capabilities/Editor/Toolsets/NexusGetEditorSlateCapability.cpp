// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusGetEditorSlateCapability.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"

void FNexusGetEditorSlateCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetEditorSlate");
	Out.Description = TEXT("Inspect visible windows or UI tree. Returns refs; query again after rebuilds.");
	Out.InputSchema = FNexusSchema::Object().Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Inspection kind"), {TEXT("Windows"), TEXT("Snapshot")}, TEXT("Snapshot"))).Prop(TEXT("Ref"), FNexusSchema::Str(TEXT("Subtree ref from this editor session"))).Prop(TEXT("MaxDepth"), FNexusSchema::Int(TEXT("Tree depth"),20,0,30)).Build();
	FNexusEditorToolSchema::Configure(Out, false);
	Out.RelatedCapabilities = { TEXT("GetEditorOperation") };
}

FCapabilityResult FNexusGetEditorSlateCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString Action = TEXT("Snapshot"); Arguments->TryGetStringField(TEXT("Action"), Action);
	return FNexusEditorToolSchema::Result(FNexusEditorToolsets::Slate(Action, Arguments));
}
REGISTER_MCP_CAPABILITY(FNexusGetEditorSlateCapability)
