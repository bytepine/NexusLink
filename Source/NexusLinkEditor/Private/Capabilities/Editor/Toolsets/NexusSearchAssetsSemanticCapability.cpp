// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusSearchAssetsSemanticCapability.h"
#if WITH_NEXUS_TOOLSETS
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"

void FNexusSearchAssetsSemanticCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("SearchAssetsSemantic");
	Out.Description = TEXT("Search indexed assets by meaning or find similar assets. Requires the engine SemanticSearch index and model.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Semantic search kind"), {TEXT("Search"), TEXT("FindSimilar")}, TEXT("Search")))
		.Prop(TEXT("Query"), FNexusSchema::Str(TEXT("Natural-language query for Search")))
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Indexed source asset object path for FindSimilar")))
		.Prop(TEXT("ClassPaths"), FNexusSchema::StrArr(TEXT("1-32 indexed asset class paths; e.g. /Script/Engine.Material")))
		.Prop(TEXT("PathRegexes"), FNexusSchema::StrArr(TEXT("Optional asset path regular expressions, maximum 32")))
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Maximum semantic matches"), 10, 1, 100))
		.Required({TEXT("ClassPaths")}).Build();
	FNexusEditorToolSchema::ConfigureNative(Out, false, TEXT("SemanticSearchToolset"));
	Out.ExtraSearchKeywords = {TEXT("semantic"), TEXT("similarity"), TEXT("vector"), TEXT("meaning")};
	Out.RelatedCapabilities = {TEXT("SearchAsset"), TEXT("GetEditorOperation")};
}
FCapabilityResult FNexusSearchAssetsSemanticCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments);
	const FString Action = A.Str(TEXT("Action"), TEXT("Search"));
	const TArray<FString> Classes = A.StrArr(TEXT("ClassPaths")), Regexes = A.StrArr(TEXT("PathRegexes"));
	if (Classes.Num() == 0 || Classes.Num() > 32 || Regexes.Num() > 32) return FCapabilityResult::MakeArgInvalid(TEXT("ClassPaths requires 1-32 classes; PathRegexes permits at most 32 entries"));
	if ((Action == TEXT("Search") ? A.Str(TEXT("Query")) : A.Str(TEXT("AssetPath"))).TrimStartAndEnd().IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("Search requires Query; FindSimilar requires AssetPath"));
	auto Input = MakeShared<FJsonObject>();
	if (Action == TEXT("Search")) Input->SetStringField(TEXT("Query"), A.Str(TEXT("Query")));
	else Input->SetObjectField(TEXT("AssetPath"), FNexusNativeEditorTools::Reference(A.Str(TEXT("AssetPath"))));
	TArray<TSharedPtr<FJsonValue>> Filters; for (const auto& Path : Classes) Filters.Add(MakeShared<FJsonValueObject>(FNexusNativeEditorTools::Reference(Path)));
	Input->SetArrayField(TEXT("ClassFilter"), Filters);
	TArray<TSharedPtr<FJsonValue>> Paths; for (const auto& Path : Regexes) { if (Path.Len() > 2048) return FCapabilityResult::MakeArgInvalid(TEXT("Path regex exceeds 2048 characters")); Paths.Add(MakeShared<FJsonValueString>(Path)); }
	Input->SetArrayField(TEXT("PathRegexes"), Paths); Input->SetNumberField(TEXT("K"), A.Num(TEXT("Limit"), 10));
	auto Operation = FNexusEditorOperations::Create(TEXT("OfficialToolset"));
	FNexusNativeEditorTools::Call(TEXT("SemanticSearchToolset"), Action, Input, Operation);
	return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusSearchAssetsSemanticCapability)
#endif
