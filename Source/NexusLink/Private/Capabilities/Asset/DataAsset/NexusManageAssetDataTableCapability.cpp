// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusManageAssetDataTableCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
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
	Out.Description = TEXT("Batch edit DataTable rows: add/remove/set; ImportText validate.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> FieldsSchema = MakeShared<FJsonObject>();
		FieldsSchema->SetStringField(TEXT("type"), TEXT("object"));
		FieldsSchema->SetStringField(TEXT("description"), TEXT("{fieldName: value} for add; value may be string/number/bool/null"));

		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),    FNexusSchema::Enum(TEXT("Row operation"), { TEXT("add"), TEXT("remove"), TEXT("set") }, TEXT("add")))
		.Prop(TEXT("rowName"),   FNexusSchema::Str(TEXT("Row name")))
		.Prop(TEXT("fieldName"), FNexusSchema::Str(TEXT("Field name (set only)")))
		.Prop(TEXT("value"),     FNexusSchema::Str(TEXT("New value string (set only)")))
		.Required({ TEXT("action"), TEXT("rowName") })
		.Build();
		ItemSchema->GetObjectField(TEXT("properties"))->SetObjectField(TEXT("fields"), FieldsSchema);

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("DataTable asset path (shared)")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch row ops (at least one)"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = {
		TEXT("row"), TEXT("dt"), TEXT("datatable"), TEXT("field"), TEXT("value")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_data_table"), TEXT("create_asset_data_table"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("Write ops: add/remove/set DT row values");
}

FCapabilityResult FManageAssetDataTableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);


		const FString AssetPath = A.Str(TEXT("assetPath"));

		UDataTable* DT = FNexusAssetUtils::LoadAssetWithFallback<UDataTable>(AssetPath);
		if (!DT) { OutError = FString::Printf(TEXT("DataTable not found: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("Missing or empty operations");
			return;
		}

		bool bDidMutate = false;
		for (const TSharedPtr<FJsonValue>& Val : Ops)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Invalid row item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FString Action  = Item->HasField(TEXT("action"))  ? Item->GetStringField(TEXT("action")).ToLower() : TEXT("");
			const FString RowName = Item->HasField(TEXT("rowName")) ? Item->GetStringField(TEXT("rowName"))          : TEXT("");
			OutEntry->SetStringField(TEXT("action"),  Action);
			OutEntry->SetStringField(TEXT("rowName"), RowName);

			if (Action.IsEmpty() || RowName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("action and rowName is required"));
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
					OutEntry->SetStringField(TEXT("error"), TEXT("DataTable has no row struct"));
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
									TEXT("Field '%s' unsupported JSON type (use string/number/bool/null)"), *KV.Key));
								bAddOk = false;
								break;
							}

							void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
							if (!FNexusPropertyUtils::ImportTextFromString(Prop, ValStr, ValuePtr, DT))
							{
								OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("ImportText failed for field '%s'"), *KV.Key));
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
					OutEntry->SetStringField(TEXT("error"), TEXT("DataTable has no row struct"));
				}
				else
				{
					const FString FieldName = Item->HasField(TEXT("fieldName")) ? Item->GetStringField(TEXT("fieldName")) : TEXT("");
					const FString NewValue  = Item->HasField(TEXT("value"))     ? Item->GetStringField(TEXT("value"))     : TEXT("");
					OutEntry->SetStringField(TEXT("fieldName"), FieldName);

					if (FieldName.IsEmpty())
					{
						OutEntry->SetStringField(TEXT("error"), TEXT("set action requires fieldName"));
					}
					else
					{
						uint8* RowData = const_cast<uint8*>(DT->FindRowUnchecked(RowKey));
						if (!RowData)
						{
							OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Row '%s' does not exist"), *RowName));
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
										TEXT("ImportText failed for field '%s'"), *FieldName));
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
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s'"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}

		if (bDidMutate) DT->MarkPackageDirty();
	
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetDataTableCapability)
