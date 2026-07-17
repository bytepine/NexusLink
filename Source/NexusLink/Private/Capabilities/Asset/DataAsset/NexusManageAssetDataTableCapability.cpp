// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusManageAssetDataTableCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Engine/DataTable.h"
#include "NexusMcpTool.h"

namespace
{
/** 将 JSON 标量转为 ImportText 输入串；object/array 返回 false。 */
static bool JsonValueToImportString(const TSharedPtr<FJsonValue>& V, FString& OutStr)
{
	if (!V.IsValid()) { OutStr.Reset(); return true; }
	switch (V->Type)
	{
	case EJson::String: return V->TryGetString(OutStr);
	case EJson::Number: OutStr = LexToString(V->AsNumber()); return true;
	case EJson::Boolean: OutStr = V->AsBool() ? TEXT("True") : TEXT("False"); return true;
	case EJson::Null:
	case EJson::None: OutStr.Reset(); return true;
	default: return false;
	}
}
} // namespace

void FManageAssetDataTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_data_table");
	Out.SearchAssetTypes = {TEXT("DataTable")};
	Out.Description = TEXT("批量编辑 DT 行：add/remove/set；ImportText 校验。");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> FieldsSchema = MakeShared<FJsonObject>();
		FieldsSchema->SetStringField(TEXT("type"), TEXT("object"));
		FieldsSchema->SetStringField(TEXT("description"), TEXT("{fieldName: value} for add; value may be string/number/bool/null"));

		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),    FNexusSchema::Enum(TEXT("行操作"), { TEXT("add"), TEXT("remove"), TEXT("set") }, TEXT("add")))
		.Prop(TEXT("rowName"),   FNexusSchema::Str(TEXT("行名")))
		.Prop(TEXT("fieldName"), FNexusSchema::Str(TEXT("字段名（仅 set）")))
		.Prop(TEXT("value"),     FNexusSchema::Str(TEXT("新值字符串（仅 set）")))
		.Required({ TEXT("action"), TEXT("rowName") })
		.Build();
		ItemSchema->GetObjectField(TEXT("properties"))->SetObjectField(TEXT("fields"), FieldsSchema);

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("DataTable 资产路径（共用）")))
		.Prop(TEXT("rows"),      FNexusSchema::ArrayOf(TEXT("批量行操作（至少一项）"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("rows") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = {
		TEXT("row"), TEXT("dt"), TEXT("datatable"), TEXT("field"), TEXT("value")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_data_table"), TEXT("create_asset_data_table"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("写操作：增删/设置 DT 行值");
}

FCapabilityResult FManageAssetDataTableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		if (!Arguments.IsValid())
		{
			OutError = TEXT("参数无效");
			return;
		}

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UDataTable* DT = FNexusAssetUtils::LoadAssetWithFallback<UDataTable>(AssetPath);
		if (!DT) { OutError = FString::Printf(TEXT("DataTable 未找到: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>>* RowsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("rows"), RowsArr) || !RowsArr)
		{
			OutError = TEXT("缺少 rows");
			return;
		}
		if (RowsArr->Num() == 0)
		{
			OutError = TEXT("rows 不能为空");
			return;
		}

		bool bDidMutate = false;
		for (const TSharedPtr<FJsonValue>& Val : *RowsArr)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("无效的行项"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FString Action  = Item->HasField(TEXT("action"))  ? Item->GetStringField(TEXT("action")).ToLower() : TEXT("");
			const FString RowName = Item->HasField(TEXT("rowName")) ? Item->GetStringField(TEXT("rowName"))          : TEXT("");
			OutEntry->SetStringField(TEXT("action"),  Action);
			OutEntry->SetStringField(TEXT("rowName"), RowName);

			if (Action.IsEmpty() || RowName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("action 与 rowName 必填"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FName RowKey(*RowName);
			const UScriptStruct* RowStruct = DT->GetRowStruct();

			if (Action == TEXT("add"))
			{
				if (DT->FindRowUnchecked(RowKey))
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("Row already exists"));
				}
				else if (!RowStruct)
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("DataTable 无行结构体"));
				}
				else
				{
					uint8* RowData = (uint8*)FMemory::Malloc(RowStruct->GetStructureSize());
					RowStruct->InitializeStruct(RowData);
					bool bAddOk = true;

					if (Item->HasField(TEXT("fields")))
					{
						const TSharedPtr<FJsonObject>& Fields = Item->GetObjectField(TEXT("fields"));
						for (auto& KV : Fields->Values)
						{
							FProperty* Prop = RowStruct->FindPropertyByName(FName(*KV.Key));
							if (!Prop) continue;

							FString ValStr;
							if (!JsonValueToImportString(KV.Value, ValStr))
							{
								OutEntry->SetStringField(TEXT("error"), FString::Printf(
									TEXT("字段 '%s' 不支持的 JSON 类型（请用 string/number/bool/null）"), *KV.Key));
								bAddOk = false;
								break;
							}

							void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
							if (!FNexusPropertyUtils::ImportTextFromString(Prop, ValStr, ValuePtr, DT))
							{
								OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("字段 '%s' ImportText 失败"), *KV.Key));
								bAddOk = false;
								break;
							}
						}
					}

					if (bAddOk)
					{
						DT->AddRow(RowKey, *((FTableRowBase*)RowData));
						bDidMutate = true;
					}
					FMemory::Free(RowData);
				}
			}
			else if (Action == TEXT("remove"))
			{
				if (!DT->FindRowUnchecked(RowKey))
					OutEntry->SetStringField(TEXT("error"), TEXT("Row does not exist"));
				else
				{
					DT->RemoveRow(RowKey);
					bDidMutate = true;
				}
			}
			else if (Action == TEXT("set"))
			{
				if (!RowStruct)
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("DataTable 无行结构体"));
				}
				else
				{
					const FString FieldName = Item->HasField(TEXT("fieldName")) ? Item->GetStringField(TEXT("fieldName")) : TEXT("");
					const FString NewValue  = Item->HasField(TEXT("value"))     ? Item->GetStringField(TEXT("value"))     : TEXT("");
					OutEntry->SetStringField(TEXT("fieldName"), FieldName);

					if (FieldName.IsEmpty())
					{
						OutEntry->SetStringField(TEXT("error"), TEXT("set 操作需要 fieldName"));
					}
					else
					{
						uint8* RowData = const_cast<uint8*>(DT->FindRowUnchecked(RowKey));
						if (!RowData)
						{
							OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("行 '%s' 不存在"), *RowName));
						}
						else
						{
							FProperty* Prop = RowStruct->FindPropertyByName(*FieldName);
							if (!Prop)
							{
								OutEntry->SetStringField(TEXT("error"), FString::Printf(
									TEXT("Field '%s' does not exist in row struct %s"), *FieldName, *RowStruct->GetName()));
							}
							else
							{
								void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
								FString OldValue;
								FNexusPropertyUtils::ExportText(Prop, OldValue, ValuePtr);
								if (!FNexusPropertyUtils::ImportTextFromString(Prop, NewValue, ValuePtr, DT))
								{
									OutEntry->SetStringField(TEXT("error"), FString::Printf(
										TEXT("字段 '%s' ImportText 失败"), *FieldName));
									FNexusPropertyUtils::ImportTextFromString(Prop, OldValue, ValuePtr, DT);
								}
								else
								{
									FString ActualValue;
									FNexusPropertyUtils::ExportText(Prop, ActualValue, ValuePtr);
									if (!OldValue.IsEmpty())    OutEntry->SetStringField(TEXT("oldValue"), OldValue);
									if (!ActualValue.IsEmpty()) OutEntry->SetStringField(TEXT("newValue"), ActualValue);
									bDidMutate = true;
								}
							}
						}
					}
				}
			}
			else
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}

		if (bDidMutate) DT->MarkPackageDirty();
	
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetDataTableCapability)
