// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusManageAssetDataTableCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Engine/DataTable.h"
#include "NexusMcpTool.h"

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

struct FDataTableActionState
{
	UDataTable* DT = nullptr;
	bool bDidMutate = false;
};

static FDataTableActionState* DTState(FNexusActionContext& Ctx)
{
	return static_cast<FDataTableActionState*>(Ctx.Target);
}

static UDataTable* DTFrom(FNexusActionContext& Ctx)
{
	FDataTableActionState* S = DTState(Ctx);
	return S ? S->DT : nullptr;
}

static void MarkDTDirty(FNexusActionContext& Ctx)
{
	if (FDataTableActionState* S = DTState(Ctx))
	{
		S->bDidMutate = true;
	}
}

static bool RequireRowName(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx, FString& OutRowName)
{
	OutRowName = FNexusArgs(Op).Str(TEXT("rowName"));
	Ctx.Entry->SetStringField(TEXT("rowName"), OutRowName);
	if (OutRowName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("action and rowName is required"));
		return false;
	}
	return true;
}

static void HandleDT_Add(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UDataTable* DT = DTFrom(Ctx);
	FString RowName;
	if (!RequireRowName(Op, Ctx, RowName)) return;

	const FName RowKey(*RowName);
	const UScriptStruct* RowStruct = DT->GetRowStruct();
	if (DT->FindRowUnchecked(RowKey))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Row already exists"));
		return;
	}
	if (!RowStruct)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("DataTable has no row struct"));
		return;
	}

	uint8* RowData = (uint8*)FMemory::Malloc(RowStruct->GetStructureSize());
	RowStruct->InitializeStruct(RowData);
	bool bAddOk = true;

	if (Op->HasField(TEXT("fields")))
	{
		const TSharedPtr<FJsonObject>& Fields = Op->GetObjectField(TEXT("fields"));
		for (auto& KV : Fields->Values)
		{
			FProperty* Prop = RowStruct->FindPropertyByName(FName(*KV.Key));
			if (!Prop) continue;

			FString ValStr;
			if (!JsonValueToImportString(KV.Value, ValStr))
			{
				Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Field '%s' unsupported JSON type (use string/number/bool/null)"), *KV.Key));
				bAddOk = false;
				break;
			}

			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
			if (!FNexusPropertyUtils::ImportTextFromString(Prop, ValStr, ValuePtr, DT))
			{
				Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("ImportText failed for field '%s'"), *KV.Key));
				bAddOk = false;
				break;
			}
		}
	}

	if (bAddOk)
	{
		DT->AddRow(RowKey, *((FTableRowBase*)RowData));
		MarkDTDirty(Ctx);
	}
	FMemory::Free(RowData);
}

static void HandleDT_Remove(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UDataTable* DT = DTFrom(Ctx);
	FString RowName;
	if (!RequireRowName(Op, Ctx, RowName)) return;

	const FName RowKey(*RowName);
	if (!DT->FindRowUnchecked(RowKey))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Row does not exist"));
		return;
	}
	DT->RemoveRow(RowKey);
	MarkDTDirty(Ctx);
}

static void HandleDT_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UDataTable* DT = DTFrom(Ctx);
	FString RowName;
	if (!RequireRowName(Op, Ctx, RowName)) return;

	const UScriptStruct* RowStruct = DT->GetRowStruct();
	if (!RowStruct)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("DataTable has no row struct"));
		return;
	}

	const FString FieldName = Op->HasField(TEXT("fieldName")) ? Op->GetStringField(TEXT("fieldName")) : TEXT("");
	const FString NewValue  = Op->HasField(TEXT("value"))     ? Op->GetStringField(TEXT("value"))     : TEXT("");
	Ctx.Entry->SetStringField(TEXT("fieldName"), FieldName);
	if (FieldName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set action requires fieldName"));
		return;
	}

	uint8* RowData = const_cast<uint8*>(DT->FindRowUnchecked(FName(*RowName)));
	if (!RowData)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Row '%s' does not exist"), *RowName));
		return;
	}
	FProperty* Prop = RowStruct->FindPropertyByName(*FieldName);
	if (!Prop)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Field '%s' does not exist in row struct %s"), *FieldName, *RowStruct->GetName()));
		return;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
	FString OldValue;
	FNexusPropertyUtils::ExportText(Prop, OldValue, ValuePtr);
	if (!FNexusPropertyUtils::ImportTextFromString(Prop, NewValue, ValuePtr, DT))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(
			TEXT("ImportText failed for field '%s'"), *FieldName));
		FNexusPropertyUtils::ImportTextFromString(Prop, OldValue, ValuePtr, DT);
		return;
	}
	FString ActualValue;
	FNexusPropertyUtils::ExportText(Prop, ActualValue, ValuePtr);
	if (!OldValue.IsEmpty())    Ctx.Entry->SetStringField(TEXT("oldValue"), OldValue);
	if (!ActualValue.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualValue);
	MarkDTDirty(Ctx);
}

bool FManageAssetDataTableCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UDataTable* DT = FNexusAssetUtils::LoadAssetWithFallback<UDataTable>(AssetPath);
	if (!DT)
	{
		OutError = FString::Printf(TEXT("DataTable not found: %s"), *AssetPath);
		return false;
	}
	FDataTableActionState* State = new FDataTableActionState();
	State->DT = DT;
	OutTarget = State;
	return true;
}

void FManageAssetDataTableCapability::FinalizeTarget(void* Target) const
{
	FDataTableActionState* State = static_cast<FDataTableActionState*>(Target);
	if (!State) return;
	if (State->bDidMutate && State->DT)
	{
		State->DT->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetDataTableCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add"),    &HandleDT_Add);
	OutHandlers.Add(TEXT("remove"), &HandleDT_Remove);
	OutHandlers.Add(TEXT("set"),    &HandleDT_Set);
}

REGISTER_MCP_CAPABILITY(FManageAssetDataTableCapability)
