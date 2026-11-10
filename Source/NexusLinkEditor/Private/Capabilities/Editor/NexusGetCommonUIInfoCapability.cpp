// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusGetCommonUIInfoCapability.h"
#if WITH_NEXUS_COMMON_UI_DEBUGGING
#include "Utils/NexusCommonUIDiagnostics.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
void FNexusGetCommonUIInfoCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetCommonUIInfo");
	Out.Description = TEXT("Read CommonUI PIE players, activation tree, routing, focus, bindings and bounded event log. UE5.9+ CommonUIDebugging. LocalPlayerIndex is session-wide; discover Players first.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor, FNexusMcpTags::Widget};
	Out.ExtraSearchKeywords = {TEXT("commonui"), TEXT("focus"), TEXT("routing"), TEXT("activation"), TEXT("input"), TEXT("events")};
	Out.RelatedCapabilities = {TEXT("ControlCommonUIDiagnostics"), TEXT("ListRuntimeWidgets"), TEXT("GetAssetUserWidget")};
}
TSharedPtr<FJsonObject> FNexusGetCommonUIInfoCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("LocalPlayerIndex"), FNexusSchema::Int(TEXT("Session player index from Players; -1 allows EventLog across retained players"), 0, -1))
		.Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Players/bindings/events pagination offset; tree uses a prefix"), 0, 0))
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Maximum returned records"), 100, 1, 500))
		.Prop(TEXT("Epoch"), FNexusSchema::Num(TEXT("EventLog epoch from previous query; -1 starts a new cursor")))
		.Prop(TEXT("AfterSeq"), FNexusSchema::Num(TEXT("EventLog entries strictly after this sequence; -1 reads from start"))).Build();
}
TArray<FString> FNexusGetCommonUIInfoCapability::GetSectionNames() const { return {TEXT("Players"), TEXT("ActivatableTree"), TEXT("InputRouting"), TEXT("Focus"), TEXT("ActionBindings"), TEXT("EventLog")}; }
void FNexusGetCommonUIInfoCapability::ExecuteSection(const FString& SectionName, const TSharedPtr<FJsonObject>& Args, void* TargetOpaque, TSharedPtr<FJsonObject>& Detail, FString& Error) const { FNexusCommonUIDiagnostics::Read(SectionName, Args, Detail, Error); }
REGISTER_MCP_CAPABILITY(FNexusGetCommonUIInfoCapability)
#endif
