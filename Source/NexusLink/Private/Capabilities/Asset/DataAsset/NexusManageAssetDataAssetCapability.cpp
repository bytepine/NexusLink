// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusManageAssetDataAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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
	Out.Description = TEXT("批量编辑 DataAsset。set=ImportText 校验；reset=CDO；ops[] 非空。");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("属性操作"), { TEXT("set"), TEXT("reset") }, TEXT("set")))
		.Prop(TEXT("propertyName"), FNexusSchema::Str(TEXT("可编辑属性名")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("新值字符串（仅 set）")))
		.Required({ TEXT("propertyName") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("DataAsset 资产路径")))
		.Prop(TEXT("ops"),       FNexusSchema::ArrayOf(TEXT("批量属性操作（至少一项）"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("ops") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = {
		TEXT("property"), TEXT("dataasset"), TEXT("value"), TEXT("field"), TEXT("cdo")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_data_asset"), TEXT("create_asset_data_asset"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("写操作：设置或重置 DataAsset 为 CDO 默认");
}

FCapabilityResult FManageAssetDataAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		FString AssetPath;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
		if (!Obj) { OutError = FString::Printf(TEXT("资产未找到: %s"), *AssetPath); return; }

		UDataAsset* DA = Cast<UDataAsset>(Obj);
		if (!DA) { OutError = FString::Printf(TEXT("资产不是 DataAsset: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("ops"), OpsArr) || !OpsArr)
		{
			OutError = TEXT("缺少 ops");
			return;
		}
		if (OpsArr->Num() == 0)
		{
			OutError = TEXT("ops 不能为空");
			return;
		}

		bool bDidMutate = false;
		for (const TSharedPtr<FJsonValue>& Val : *OpsArr)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("无效的 op 项"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FString Action       = Item->HasField(TEXT("action"))       ? Item->GetStringField(TEXT("action")).ToLower() : TEXT("set");
			const FString PropertyName = Item->HasField(TEXT("propertyName")) ? Item->GetStringField(TEXT("propertyName"))     : TEXT("");
			OutEntry->SetStringField(TEXT("propertyName"), PropertyName);
			OutEntry->SetStringField(TEXT("action"),       Action);

			if (PropertyName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("propertyName 必填"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			FProperty* Prop = DA->GetClass()->FindPropertyByName(*PropertyName);
			if (!Prop)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("属性 '%s' 在类 %s 中未找到"),
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
					OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("ImportText 失败: '%s'"), *PropertyName));
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
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}

		if (bDidMutate) DA->MarkPackageDirty();
	
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetDataAssetCapability)
