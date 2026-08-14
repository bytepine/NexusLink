// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Localization/NexusCreateAssetStringTableCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "NexusMcpTool.h"

void FCreateAssetStringTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_string_table");
	Out.Description = TEXT("创建 StringTable。可选 namespace。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Prop(TEXT("namespace"), FNexusSchema::Str(TEXT("命名空间（可选）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("localization"), TEXT("loc"), TEXT("stringtable"), TEXT("i18n") };
	Out.RelatedCapabilities = { TEXT("get_asset_string_table"), TEXT("manage_asset_string_table") };
	Out.WhenToUse = TEXT("新建 StringTable；用 manage 增删 key");
}

FCapabilityResult FCreateAssetStringTableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UStringTable>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("StringTable already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UStringTable* Table = NewObject<UStringTable>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Table) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败")); return; }
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
