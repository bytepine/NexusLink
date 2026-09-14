// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusManageAssetDataAssetCapability.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Engine/DataAsset.h"
#include "NexusMcpTool.h"

void FManageAssetDataAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_data_asset");
	Out.SearchAssetTypes = {TEXT("DataAsset")};
	Out.Description = TEXT("Batch edit DataAsset. set=ImportText validate; reset=CDO.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Property operation"), { TEXT("set"), TEXT("reset") }, TEXT("set")))
		.Prop(TEXT("propertyName"), FNexusSchema::Str(TEXT("Editable property name")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("New value string (set only)")))
		.Required({ TEXT("propertyName") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("DataAsset asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch property ops (at least one)"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = {
		TEXT("property"), TEXT("dataasset"), TEXT("value"), TEXT("field"), TEXT("cdo")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_data_asset"), TEXT("create_asset_data_asset"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("Write ops: set or reset DataAsset to CDO defaults");
}

struct FDataAssetActionState
{
	UDataAsset* DA = nullptr;
	bool bDirty = false;
};

static FDataAssetActionState* DAState(FNexusActionContext& Ctx)
{
	return static_cast<FDataAssetActionState*>(Ctx.Target);
}

static UDataAsset* DAFrom(FNexusActionContext& Ctx)
{
	FDataAssetActionState* S = DAState(Ctx);
	return S ? S->DA : nullptr;
}

static void MarkDADirty(FNexusActionContext& Ctx)
{
	if (FDataAssetActionState* S = DAState(Ctx))
	{
		S->bDirty = true;
	}
}

static FProperty* ResolveDAProp(UDataAsset* DA, const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FString PropertyName = FNexusArgs(Op).Str(TEXT("propertyName"));
	Ctx.Entry->SetStringField(TEXT("propertyName"), PropertyName);
	if (PropertyName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("propertyName is required"));
		return nullptr;
	}
	FProperty* Prop = DA->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Property '%s' not found on class %s"),
			*PropertyName, *DA->GetClass()->GetName()));
		return nullptr;
	}
	if (!Prop->HasAnyPropertyFlags(CPF_Edit))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Property '%s' is not editable"), *PropertyName));
		return nullptr;
	}
	return Prop;
}

static void HandleDA_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UDataAsset* DA = DAFrom(Ctx);
	FProperty* Prop = ResolveDAProp(DA, Op, Ctx);
	if (!Prop) return;
	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(DA);
	FString OldValue;
	FNexusPropertyUtils::ExportText(Prop, OldValue, ValuePtr);
	const FString NewValue = Op->HasField(TEXT("value")) ? Op->GetStringField(TEXT("value")) : TEXT("");
	if (!FNexusPropertyUtils::ImportTextFromString(Prop, NewValue, ValuePtr, DA))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("ImportText failed: '%s'"), *Prop->GetName()));
		return;
	}
	FString ActualValue;
	FNexusPropertyUtils::ExportText(Prop, ActualValue, ValuePtr);
	if (!OldValue.IsEmpty())    Ctx.Entry->SetStringField(TEXT("oldValue"), OldValue);
	if (!ActualValue.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualValue);
	MarkDADirty(Ctx);
}

static void HandleDA_Reset(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UDataAsset* DA = DAFrom(Ctx);
	FProperty* Prop = ResolveDAProp(DA, Op, Ctx);
	if (!Prop) return;
	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(DA);
	FString OldValue;
	FNexusPropertyUtils::ExportText(Prop, OldValue, ValuePtr);
	// 从类 CDO 拷贝，等价于编辑器「恢复默认」；非 InitializeValue 的零内存语义
	UObject* CDO = DA->GetClass()->GetDefaultObject();
	const void* SrcPtr = Prop->ContainerPtrToValuePtr<void>(CDO);
	Prop->CopyCompleteValue(ValuePtr, SrcPtr);
	FString ResetValue;
	FNexusPropertyUtils::ExportText(Prop, ResetValue, ValuePtr);
	if (!OldValue.IsEmpty())   Ctx.Entry->SetStringField(TEXT("oldValue"),   OldValue);
	if (!ResetValue.IsEmpty()) Ctx.Entry->SetStringField(TEXT("resetValue"), ResetValue);
	MarkDADirty(Ctx);
}

bool FManageAssetDataAssetCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
	if (!Obj) { OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath); return false; }
	UDataAsset* DA = Cast<UDataAsset>(Obj);
	if (!DA) { OutError = FString::Printf(TEXT("Asset is not a DataAsset: %s"), *AssetPath); return false; }
	FDataAssetActionState* State = new FDataAssetActionState();
	State->DA = DA;
	OutTarget = State;
	return true;
}

void FManageAssetDataAssetCapability::FinalizeTarget(void* Target) const
{
	FDataAssetActionState* State = static_cast<FDataAssetActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->DA)
	{
		State->DA->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetDataAssetCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set"),   &HandleDA_Set);
	OutHandlers.Add(TEXT("reset"), &HandleDA_Reset);
}

REGISTER_MCP_CAPABILITY(FManageAssetDataAssetCapability)
