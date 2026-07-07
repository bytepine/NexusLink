// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Enum/NexusGetAssetEnumCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Engine/UserDefinedEnum.h"
#include "NexusMcpTool.h"

void FGetAssetEnumCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_enum");
	Out.Description = TEXT("读取 UserDefinedEnum 的枚举项（name/displayName/value）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("枚举资产包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("enum"), TEXT("entries"), TEXT("blueprint"), TEXT("read") };
	Out.RelatedCapabilities = { TEXT("create_asset_enum"), TEXT("manage_asset_enum") };
}

FCapabilityResult FGetAssetEnumCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		UUserDefinedEnum* Enum = LoadObject<UUserDefinedEnum>(nullptr, *AssetPath);
		if (!Enum)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("加载 UserDefinedEnum 失败: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Enum->GetName());
		Entry->SetStringField(TEXT("path"), Enum->GetPathName());

		TArray<TSharedPtr<FJsonValue>> EntriesArr;
		// NumEnums() 包含内部 _MAX 项，跳过最后一项
		const int32 Count = Enum->NumEnums() - 1;
		for (int32 i = 0; i < Count; ++i)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetNumberField(TEXT("index"),       i);
			Item->SetStringField(TEXT("name"),        Enum->GetNameStringByIndex(i));
			Item->SetStringField(TEXT("displayName"), Enum->GetDisplayNameTextByIndex(i).ToString());
			Item->SetNumberField(TEXT("value"),       (double)Enum->GetValueByIndex(i));
			EntriesArr.Add(MakeShared<FJsonValueObject>(Item));
		}

		Entry->SetNumberField(TEXT("entryCount"), Count);
		Entry->SetArrayField(TEXT("entries"), EntriesArr);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetEnumCapability)
