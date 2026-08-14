// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Localization/NexusManageAssetStringTableCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "NexusMcpTool.h"

void FManageAssetStringTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_string_table");
	Out.SearchAssetTypes = {TEXT("StringTable")};
	Out.Description = TEXT("批量编辑 StringTable。operations[].action=add_key/remove_key/set_source。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"),
			{ TEXT("add_key"), TEXT("remove_key"), TEXT("set_source") }))
		.Prop(TEXT("key"), FNexusSchema::Str(TEXT("条目 key")))
		.Prop(TEXT("source"), FNexusSchema::Str(TEXT("源字符串（add_key/set_source）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("StringTable 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("localization"), TEXT("loc"), TEXT("key"), TEXT("source") };
	Out.RelatedCapabilities = { TEXT("get_asset_string_table"), TEXT("create_asset_string_table") };
}

FCapabilityResult FManageAssetStringTableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
			return;
		}

		FStringTableRef Mutable = Table->GetMutableStringTable();
		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpPtr;

			FString Action, Key, Source;
			Op->TryGetStringField(TEXT("action"), Action);
			Op->TryGetStringField(TEXT("key"), Key);
			Op->TryGetStringField(TEXT("source"), Source);
			Action = Action.ToLower();

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);

			if (Key.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("需要 key"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			if (Action == TEXT("add_key") || Action == TEXT("set_source"))
			{
				if (Source.IsEmpty() && Action == TEXT("add_key"))
				{
					Entry->SetStringField(TEXT("error"), TEXT("add_key 需要 source"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
#if NX_UE_HAS_STRING_TABLE_SOURCE_DEV_NOTES
				Mutable->SetSourceString(Key, Source, FString());
#else
				Mutable->SetSourceString(Key, Source);
#endif
				bDirty = true;
				Entry->SetStringField(TEXT("key"), Key);
				Entry->SetStringField(TEXT("source"), Source);
			}
			else if (Action == TEXT("remove_key"))
			{
				Mutable->RemoveSourceString(Key);
				bDirty = true;
				Entry->SetStringField(TEXT("key"), Key);
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bDirty) Table->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetStringTableCapability)
