// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Localization/NexusCreateAssetStringTableCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "NexusMcpTool.h"

void FCreateAssetStringTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_string_table");
	Out.Description = TEXT("Create StringTable. optional namespace.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("namespace"), FNexusSchema::Str(TEXT("Namespace (optional)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("localization"), TEXT("loc"), TEXT("stringtable"), TEXT("i18n") };
	Out.RelatedCapabilities = { TEXT("get_asset_string_table"), TEXT("manage_asset_string_table") };
	Out.WhenToUse = TEXT("Create new StringTable; Use manage add/remove key");
}

FCapabilityResult FCreateAssetStringTableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		if (LoadObject<UStringTable>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("StringTable already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UStringTable* Table = NewObject<UStringTable>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Table) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Creation failed")); return; }
		FString Namespace;
		if (Arguments->TryGetStringField(TEXT("namespace"), Namespace) && !Namespace.IsEmpty())
		{
			Table->GetMutableStringTable()->SetNamespace(Namespace);
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Table, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Table->GetName());
		Entry->SetStringField(TEXT("path"), Table->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetStringTableCapability)
