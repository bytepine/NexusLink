// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Asset/NexusGetAssetExportOptionsCapability.h"
#include "Utils/NexusAssetExportUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "HAL/FileManager.h"

void FNexusGetAssetExportOptionsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetExportOptions");
	Out.Description = TEXT("Inspect registered export formats and destination existence. No dialogs or file writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset path from SearchAsset")))
		.Prop(TEXT("OutputPath"), FNexusSchema::Str(TEXT("Optional primary output path to inspect"))).Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor};
	Out.bCacheable = false;
	Out.RelatedCapabilities = {TEXT("ExportAsset"), TEXT("SearchAsset")};
	Out.ExtraSearchKeywords = {TEXT("export"), TEXT("format"), TEXT("extension"), TEXT("overwrite")};
}

FCapabilityResult FNexusGetAssetExportOptionsCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		const FNexusArgs Args(Arguments);
		UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Args.Str(TEXT("AssetPath")));
		if (!Asset) { Error = TEXT("Asset not found"); return; }
		auto Entry = FNexusAssetExportUtils::Describe(Asset);
		Entry->SetStringField(TEXT("AssetPath"), Asset->GetPathName());
		if (!Args.Str(TEXT("OutputPath")).IsEmpty())
		{
			const FString Path = FPaths::ConvertRelativePathToFull(Args.Str(TEXT("OutputPath")));
			Entry->SetStringField(TEXT("OutputPath"), Path);
			Entry->SetBoolField(TEXT("bOutputExists"), IFileManager::Get().FileExists(*Path));
		}
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}
REGISTER_MCP_CAPABILITY(FNexusGetAssetExportOptionsCapability)
