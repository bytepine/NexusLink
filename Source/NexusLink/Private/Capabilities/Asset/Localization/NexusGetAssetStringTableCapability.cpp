// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Localization/NexusGetAssetStringTableCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "NexusMcpTool.h"

void FGetAssetStringTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_string_table");
	Out.SearchAssetTypes = {TEXT("StringTable")};
	Out.Description = TEXT("读取 StringTable：namespace / keys / 源字符串摘要。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("StringTable 资产路径")))
		.Prop(TEXT("limit"), FNexusSchema::Int(TEXT("最多返回条目数"), 50))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("localization"), TEXT("loc"), TEXT("key"), TEXT("i18n") };
	Out.RelatedCapabilities = { TEXT("manage_asset_string_table"), TEXT("create_asset_string_table") };
}

FCapabilityResult FGetAssetStringTableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UStringTable* Table = FNexusAssetUtils::LoadAssetWithFallback<UStringTable>(AssetPath);
		if (!Table)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载 StringTable 失败: %s"), *AssetPath));
			return;
		}

		int32 Limit = 50;
		if (Arguments.IsValid() && Arguments->HasField(TEXT("limit")))
		{
			Limit = FMath::Max(1, static_cast<int32>(Arguments->GetNumberField(TEXT("limit"))));
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Table->GetName());
		Entry->SetStringField(TEXT("path"), Table->GetPathName());
		Entry->SetStringField(TEXT("namespace"), Table->GetMutableStringTable()->GetNamespace());

		TArray<TSharedPtr<FJsonValue>> KeysArr;
		int32 Total = 0;
		Table->GetMutableStringTable()->EnumerateSourceStrings(
			[&](const FString& Key, const FString& Source) -> bool
			{
				if (Total < Limit)
				{
					TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
					Row->SetStringField(TEXT("key"), Key);
					Row->SetStringField(TEXT("source"), Source);
					KeysArr.Add(MakeShared<FJsonValueObject>(Row));
				}
				++Total;
				return true;
			});
		Entry->SetNumberField(TEXT("keyCount"), Total);
		Entry->SetArrayField(TEXT("keys"), KeysArr);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetStringTableCapability)
