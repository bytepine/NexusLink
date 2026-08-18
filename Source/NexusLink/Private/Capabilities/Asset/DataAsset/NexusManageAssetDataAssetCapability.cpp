// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusManageAssetDataAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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

FCapabilityResult FManageAssetDataAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));

		UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
		if (!Obj) { OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath); return; }

		UDataAsset* DA = Cast<UDataAsset>(Obj);
		if (!DA) { OutError = FString::Printf(TEXT("Asset is not a DataAsset: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>> OpsArr = FNexusJsonUtils::ExtractOperations(Arguments);
		if (OpsArr.Num() == 0)
		{
			OutError = TEXT("Missing or empty operations");
			return;
		}

		bool bDidMutate = false;
		for (const TSharedPtr<FJsonValue>& Val : OpsArr)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Invalid op item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FString Action       = Item->HasField(TEXT("action"))       ? Item->GetStringField(TEXT("action")).ToLower() : TEXT("set");
			const FString PropertyName = Item->HasField(TEXT("propertyName")) ? Item->GetStringField(TEXT("propertyName"))     : TEXT("");
			OutEntry->SetStringField(TEXT("propertyName"), PropertyName);
			OutEntry->SetStringField(TEXT("action"),       Action);

			if (PropertyName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("propertyName is required"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			FProperty* Prop = DA->GetClass()->FindPropertyByName(*PropertyName);
			if (!Prop)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Property '%s' not found on class %s"),
					*PropertyName, *DA->GetClass()->GetName()));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			if (!Prop->HasAnyPropertyFlags(CPF_Edit))
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Property '%s' is not editable"), *PropertyName));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(DA);
			FString OldValue;
			FNexusPropertyUtils::ExportText(Prop, OldValue, ValuePtr);

			if (Action == TEXT("set"))
			{
				const FString NewValue = Item->HasField(TEXT("value")) ? Item->GetStringField(TEXT("value")) : TEXT("");
				if (!FNexusPropertyUtils::ImportTextFromString(Prop, NewValue, ValuePtr, DA))
				{
					OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("ImportText failed: '%s'"), *PropertyName));
					OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
					continue;
				}

				FString ActualValue;
				FNexusPropertyUtils::ExportText(Prop, ActualValue, ValuePtr);
				if (!OldValue.IsEmpty())    OutEntry->SetStringField(TEXT("oldValue"), OldValue);
				if (!ActualValue.IsEmpty()) OutEntry->SetStringField(TEXT("newValue"), ActualValue);
				bDidMutate = true;
			}
			else if (Action == TEXT("reset"))
			{
				// 从类 CDO 拷贝，等价于编辑器「恢复默认」；非 InitializeValue 的零内存语义
				UObject* CDO = DA->GetClass()->GetDefaultObject();
				const void* SrcPtr = Prop->ContainerPtrToValuePtr<void>(CDO);
				Prop->CopyCompleteValue(ValuePtr, SrcPtr);

				FString ResetValue;
				FNexusPropertyUtils::ExportText(Prop, ResetValue, ValuePtr);
				if (!OldValue.IsEmpty())   OutEntry->SetStringField(TEXT("oldValue"),   OldValue);
				if (!ResetValue.IsEmpty()) OutEntry->SetStringField(TEXT("resetValue"), ResetValue);
				bDidMutate = true;
			}
			else
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s'"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}

		if (bDidMutate) DA->MarkPackageDirty();
	
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetDataAssetCapability)
