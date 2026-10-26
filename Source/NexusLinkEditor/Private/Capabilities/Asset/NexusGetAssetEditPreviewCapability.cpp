// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Asset/NexusGetAssetEditPreviewCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"

void FNexusGetAssetEditPreviewCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetEditPreview");
	Out.Description = TEXT("Preview scalar BP defaults, DataTable fields or Widget properties in temporary storage; never writes, compiles, saves or records Undo.");
	const TSharedRef<FJsonObject> Operation = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Matching operation"), {TEXT("SetDefaults"), TEXT("Set"), TEXT("SetProperty")}))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("Blueprint/Widget scalar property path")))
		.Prop(TEXT("WidgetName"), FNexusSchema::Str(TEXT("Widget target for SetProperty")))
		.Prop(TEXT("RowName"), FNexusSchema::Str(TEXT("DataTable row for Set")))
		.Prop(TEXT("FieldName"), FNexusSchema::Str(TEXT("DataTable scalar field for Set")))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("Proposed value as ImportText text")))
		.Prop(TEXT("ExpectedValue"), FNexusSchema::Str(TEXT("Optional expected current value")))
		.Required({TEXT("Action"), TEXT("Value")}).Build().ToSharedRef();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Single asset path")))
		.Prop(TEXT("Capability"), FNexusSchema::Enum(TEXT("Target write capability"), {TEXT("ManageAssetBlueprint"), TEXT("ManageAssetDataTable"), TEXT("ManageAssetUserWidget")}))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Scalar edits to validate"), Operation))
		.Required({TEXT("AssetPath"), TEXT("Capability"), TEXT("Operations")}).Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor};
	Out.RelatedCapabilities = {TEXT("ManageAssetBlueprint"), TEXT("ManageAssetDataTable"), TEXT("ManageAssetUserWidget")};
	Out.ExtraSearchKeywords = {TEXT("preview"), TEXT("ExpectedValue"), TEXT("verify"), TEXT("dry run")};
}

FCapabilityResult FNexusGetAssetEditPreviewCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		const FNexusArgs Args(Arguments);
		const FString Path = Args.Str(TEXT("AssetPath"));
		UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
		if (!Asset) { Error = TEXT("Asset not found"); return; }
		const TArray<TSharedPtr<FJsonValue>> Operations = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Operations.Num() == 0) { Error = TEXT("Operations must not be empty"); return; }
		for (const TSharedPtr<FJsonValue>& Value : Operations)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("Path"), Path);
			FNexusAssetEditUtils::Preview(Asset, Args.Str(TEXT("Capability")), Value->AsObject(), Entry);
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetEditPreviewCapability)
