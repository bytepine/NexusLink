// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Enum/NexusManageAssetEnumCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"
#include "UObject/UnrealType.h"
#include "NexusMcpTool.h"

void FManageAssetEnumCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_enum");
	Out.Description = TEXT("修改 UserDefinedEnum 枚举项。operations[].action: add_entry / remove_entry / set_display_name。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"),      FNexusSchema::Str(TEXT("add_entry / remove_entry / set_display_name")))
		.Prop(TEXT("index"),           FNexusSchema::Int(TEXT("枚举项索引（remove_entry/set_display_name 必填）")))
		.Prop(TEXT("displayName"),     FNexusSchema::Str(TEXT("显示名称（set_display_name 必填）")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),   FNexusSchema::Str(TEXT("枚举资产包路径")))
		.Required(TEXT("operations"),  FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("enum"), TEXT("entry"), TEXT("add"), TEXT("remove"), TEXT("rename"), TEXT("display") };
	Out.RelatedCapabilities = { TEXT("create_asset_enum"), TEXT("get_asset_enum") };
}

FCapabilityResult FManageAssetEnumCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")) || !Arguments->HasField(TEXT("operations")))
		{
			OutError = TEXT("缺少 assetPath 或 operations");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		UUserDefinedEnum* Enum = LoadObject<UUserDefinedEnum>(nullptr, *AssetPath);
		if (!Enum)
		{
			OutError = FString::Printf(TEXT("加载 UserDefinedEnum 失败: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>& OpsArr = Arguments->GetArrayField(TEXT("operations"));
		for (const TSharedPtr<FJsonValue>& OpVal : OpsArr)
		{
			const TSharedPtr<FJsonObject>& Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;

			const FString Action = Op->HasField(TEXT("action")) ? Op->GetStringField(TEXT("action")) : TEXT("");

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("action"), Action);

			if (Action == TEXT("add_entry"))
			{
				FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
				Entry->SetNumberField(TEXT("newIndex"), Enum->NumEnums() - 2); // 最新项（-2：跳 _MAX）
				Entry->SetBoolField(TEXT("success"), true);
			}
			else if (Action == TEXT("remove_entry"))
			{
				if (!Op->HasField(TEXT("index")))
				{
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("remove_entry 需要 index"));
					continue;
				}
				const int32 Idx = (int32)Op->GetNumberField(TEXT("index"));
				const int32 ValidCount = Enum->NumEnums() - 1;
				if (Idx < 0 || Idx >= ValidCount)
				{
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
						FString::Printf(TEXT("index %d 超出范围 [0,%d)"), Idx, ValidCount));
					continue;
				}
				FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, Idx);
				Entry->SetBoolField(TEXT("success"), true);
			}
			else if (Action == TEXT("set_display_name"))
			{
				if (!Op->HasField(TEXT("index")) || !Op->HasField(TEXT("displayName")))
				{
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("set_display_name 需要 index 和 displayName"));
					continue;
				}
				const int32 Idx         = (int32)Op->GetNumberField(TEXT("index"));
				const FString DispName  = Op->GetStringField(TEXT("displayName"));
				const int32 ValidCount  = Enum->NumEnums() - 1;
				if (Idx < 0 || Idx >= ValidCount)
				{
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
						FString::Printf(TEXT("index %d 超出范围 [0,%d)"), Idx, ValidCount));
					continue;
				}
				// 通过反射写入 DisplayNameMap（跨版本稳定）
				bool bSetOk = false;
				const FName EntryKey = FName(*Enum->GetNameStringByIndex(Idx));
				if (FMapProperty* MapProp = FindFProperty<FMapProperty>(Enum->GetClass(), TEXT("DisplayNameMap")))
				{
					using FDispMap = TMap<FName, FText>;
					FDispMap* Map = MapProp->ContainerPtrToValuePtr<FDispMap>(Enum);
					if (Map)
					{
						Map->Add(EntryKey, FText::FromString(DispName));
						bSetOk = true;
					}
				}
				if (!bSetOk)
				{
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("set_display_name: 写入 DisplayNameMap 失败"));
					continue;
				}
				Entry->SetBoolField(TEXT("success"), true);
			}
			else
			{
				FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
					FString::Printf(TEXT("未知 action: %s"), *Action));
				continue;
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		Enum->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetEnumCapability)
