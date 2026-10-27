// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusListEditorAutomationTestsCapability.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"

void FNexusListEditorAutomationTestsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ListEditorAutomationTests");
	Out.Description = TEXT("Enumerate discovered local test names. Supports substring filter; discover first.");
	Out.InputSchema = FNexusSchema::Object().Prop(TEXT("NameFilter"),FNexusSchema::Str(TEXT("Full-name substring"))).Prop(TEXT("Limit"),FNexusSchema::Int(TEXT("Maximum returned names"),200,1,2000)).Build();
	FNexusEditorToolSchema::Configure(Out, false);
	Out.RelatedCapabilities = { TEXT("GetEditorOperation") };
}

FCapabilityResult FNexusListEditorAutomationTestsCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusEditorToolSchema::Result(FNexusEditorToolsets::Automation(TEXT("List"), Arguments));
}
REGISTER_MCP_CAPABILITY(FNexusListEditorAutomationTestsCapability)
