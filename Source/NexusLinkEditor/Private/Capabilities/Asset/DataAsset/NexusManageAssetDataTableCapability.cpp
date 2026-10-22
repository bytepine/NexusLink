// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusManageAssetDataTableCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Engine/DataTable.h"
#include "UObject/StructOnScope.h"
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

void FNexusManageAssetDataTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetDataTable");
	Out.SearchAssetTypes = {TEXT("DataTable")};
	Out.Description = TEXT("Batch edit DataTable rows: add/remove/set; ImportText validate.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> FieldsSchema = FNexusSchema::AnyObject(
			TEXT("{FieldName: value} for add; string/number/bool; null keeps the default value"));

		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),    FNexusSchema::Enum(TEXT("Row operation"), { TEXT("Add"), TEXT("Remove"), TEXT("Set") }, TEXT("Add")))
		.Prop(TEXT("RowName"),   FNexusSchema::Str(TEXT("Row name")))
		.Prop(TEXT("FieldName"), FNexusSchema::Str(TEXT("Field name (set only)")))
		.Prop(TEXT("Value"),     FNexusSchema::Str(TEXT("New value string (set only)")))
		.Required({ TEXT("Action"), TEXT("RowName") })
		.Build();
		ItemSchema->GetObjectField(TEXT("properties"))->SetObjectField(TEXT("Fields"), FieldsSchema);

		return FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("DataTable asset path (shared)")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch row ops (at least one)"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = {
		TEXT("Row"), TEXT("dt"), TEXT("datatable"), TEXT("field"), TEXT("Value")
	};
	Out.RelatedCapabilities = { TEXT("GetAssetDataTable"), TEXT("CreateAssetDataTable"), TEXT("SaveAsset") };
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
	OutRowName = FNexusArgs(Op).Str(TEXT("RowName"));
	Ctx.Entry->SetStringField(TEXT("RowName"), OutRowName);
	if (OutRowName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("action and RowName is required"));
		return false;
	}
	return true;
}

static void HandleDT_Add(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UDataTable* DT = DTFrom(Ctx);
	if (!DT)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("DataTable is null"));
		return;
	}
	FString RowName;
	if (!RequireRowName(Op, Ctx, RowName)) return;

	const FName RowKey(*RowName);
	const UScriptStruct* RowStruct = DT->GetRowStruct();
	if (DT->FindRowUnchecked(RowKey))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Row already exists"));
		return;
	}
	if (!RowStruct)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("DataTable has no row struct"));
		return;
	}

	// 引擎作用域对象负责对齐、初始化及析构，失败路径同样释放字符串/容器成员。
	FStructOnScope RowStorage(RowStruct);
	uint8* RowData = RowStorage.GetStructMemory();
	bool bAddOk = true;

	if (Op->HasField(TEXT("Fields")))
	{
		const TSharedPtr<FJsonObject>& Fields = Op->GetObjectField(TEXT("Fields"));
		for (auto& KV : Fields->Values)
		{
			FProperty* Prop = RowStruct->FindPropertyByName(FName(*KV.Key));
			if (!Prop)
			{
				Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Field '%s' does not exist in row struct"), *KV.Key));
				bAddOk = false;
				break;
			}
			if (!KV.Value.IsValid() || KV.Value->IsNull()) continue;

			FString ValStr;
			if (!JsonValueToImportString(KV.Value, ValStr))
			{
				Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(
					TEXT("Field '%s' unsupported JSON type (use string/number/bool/null)"), *KV.Key));
				bAddOk = false;
				break;
			}

			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
			if (!FNexusPropertyUtils::ImportTextFromString(Prop, ValStr, ValuePtr, DT))
			{
				Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("ImportText failed for field '%s'"), *KV.Key));
				bAddOk = false;
				break;
			}
		}
	}

	if (bAddOk)
	{
		DT->Modify();
		DT->AddRow(RowKey, *((FTableRowBase*)RowData));
		MarkDTDirty(Ctx);
	}
}

static void HandleDT_Remove(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UDataTable* DT = DTFrom(Ctx);
	if (!DT)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("DataTable is null"));
		return;
	}
	FString RowName;
	if (!RequireRowName(Op, Ctx, RowName)) return;

	const FName RowKey(*RowName);
	if (!DT->FindRowUnchecked(RowKey))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Row does not exist"));
		return;
	}
	DT->Modify();
	DT->RemoveRow(RowKey);
	MarkDTDirty(Ctx);
}

static void HandleDT_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UDataTable* DT = DTFrom(Ctx);
	if (!DT)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("DataTable is null"));
		return;
	}
	FString RowName;
	if (!RequireRowName(Op, Ctx, RowName)) return;

	const UScriptStruct* RowStruct = DT->GetRowStruct();
	if (!RowStruct)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("DataTable has no row struct"));
		return;
	}

	const FString FieldName = Op->HasField(TEXT("FieldName")) ? Op->GetStringField(TEXT("FieldName")) : TEXT("");
	const FString NewValue  = Op->HasField(TEXT("Value"))     ? Op->GetStringField(TEXT("Value"))     : TEXT("");
	Ctx.Entry->SetStringField(TEXT("FieldName"), FieldName);
	if (FieldName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("set action requires FieldName"));
		return;
	}

	uint8* RowData = const_cast<uint8*>(DT->FindRowUnchecked(FName(*RowName)));
	if (!RowData)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Row '%s' does not exist"), *RowName));
		return;
	}
	FProperty* Prop = RowStruct->FindPropertyByName(*FieldName);
	if (!Prop)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(
			TEXT("Field '%s' does not exist in row struct %s"), *FieldName, *RowStruct->GetName()));
		return;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
	FString OldValue;
	FNexusPropertyUtils::ExportText(Prop, OldValue, ValuePtr);
	DT->Modify();
	if (!FNexusPropertyUtils::ImportTextFromString(Prop, NewValue, ValuePtr, DT))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(
			TEXT("ImportText failed for field '%s'"), *FieldName));
		FNexusPropertyUtils::ImportTextFromString(Prop, OldValue, ValuePtr, DT);
		return;
	}
	FString ActualValue;
	FNexusPropertyUtils::ExportText(Prop, ActualValue, ValuePtr);
	if (!OldValue.IsEmpty())    Ctx.Entry->SetStringField(TEXT("OldValue"), OldValue);
	if (!ActualValue.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualValue);
	MarkDTDirty(Ctx);
}

bool FNexusManageAssetDataTableCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UDataTable* DT = FNexusAssetUtils::LoadAssetWithFallback<UDataTable>(AssetPath);
	if (!DT)
	{
		OutError = FString::Printf(TEXT("DataTable not found: %s"), *AssetPath);
		return false;
	}
	// 直接创建/加载的表未必带事务标记；缺少该标记时 Modify() 不会记录 Undo。
	DT->SetFlags(RF_Transactional);
	FDataTableActionState* State = new FDataTableActionState();
	State->DT = DT;
	OutTarget = State;
	return true;
}

void FNexusManageAssetDataTableCapability::FinalizeTarget(void* Target) const
{
	FDataTableActionState* State = static_cast<FDataTableActionState*>(Target);
	if (!State) return;
	if (State->bDidMutate && State->DT)
	{
		State->DT->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetDataTableCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("Add"),    &HandleDT_Add);
	OutHandlers.Add(TEXT("Remove"), &HandleDT_Remove);
	OutHandlers.Add(TEXT("Set"),    &HandleDT_Set);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetDataTableCapability)
