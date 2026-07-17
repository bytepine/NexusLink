// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusManageAssetAttributeSetCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "AttributeSet.h"
#include "GameplayEffectTypes.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "NexusMcpTool.h"

void FManageAssetAttributeSetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_attribute_set");
	Out.SearchAssetTypes = {TEXT("AttributeSet")};
	Out.Description = TEXT("批量 set/reset AttributeSet CDO 的 FGameplayAttributeData 属性默认值。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("AttributeSet Blueprint 路径")))
		.Prop(TEXT("operations"), FNexusSchema::StrArr(TEXT("操作数组；每项含 action(set/reset) + attributeName + 可选 baseValue")))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("attribute"), TEXT("default"), TEXT("health"), TEXT("stat") };
	Out.RelatedCapabilities = { TEXT("get_asset_attribute_set"), TEXT("save_asset"), TEXT("create_asset_attribute_set") };
	Out.WhenToUse = TEXT("设置 AS 属性初始值；改完自动重编译 Blueprint");
}

FCapabilityResult FManageAssetAttributeSetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{ OutError = TEXT("缺少 assetPath"); return; }

		const TArray<TSharedPtr<FJsonValue>> OpsArrVal = FNexusJsonUtils::ExtractOperations(Arguments);
		const TArray<TSharedPtr<FJsonValue>>* OpsArr = &OpsArrVal;
		if (OpsArr->Num() == 0)
		{ OutError = TEXT("operations 为必填且不能为空数组"); return; }

		FString LoadError;
		UBlueprint* BP = FNexusGasUtils::LoadAttributeSetBlueprint(AssetPath, LoadError);
		if (!BP) { OutError = LoadError; return; }
		if (!BP->GeneratedClass) { OutError = TEXT("Blueprint 未编译"); return; }

		UObject* CDO = BP->GeneratedClass->GetDefaultObject();
		if (!CDO) { OutError = TEXT("无法获取 CDO"); return; }

		int32 Applied = 0;
		for (int32 i = 0; i < OpsArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!(*OpsArr)[i].IsValid() || !(*OpsArr)[i]->TryGetObject(OpObjPtr) || !OpObjPtr)
			{ OutError = FString::Printf(TEXT("ops[%d] 不是有效的 JSON 对象"), i); return; }

			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;
			FString Action, AttrName;
			if (!Op->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
			{ OutError = FString::Printf(TEXT("ops[%d] 缺少 action（set/reset）"), i); return; }
			if (!Op->TryGetStringField(TEXT("attributeName"), AttrName) || AttrName.IsEmpty())
			{ OutError = FString::Printf(TEXT("ops[%d] 缺少 attributeName"), i); return; }

			// 查找属性
			FStructProperty* FoundProp = nullptr;
			for (TFieldIterator<FProperty> PropIt(BP->GeneratedClass); PropIt; ++PropIt)
			{
				FStructProperty* StructProp = CastField<FStructProperty>(*PropIt);
				if (!StructProp || !StructProp->Struct) continue;
				if (!StructProp->Struct->IsChildOf(FGameplayAttributeData::StaticStruct())) continue;
				if (PropIt->GetName().Equals(AttrName, ESearchCase::IgnoreCase))
				{
					FoundProp = StructProp;
					break;
				}
			}
			if (!FoundProp)
			{ OutError = FString::Printf(TEXT("ops[%d] 未找到 FGameplayAttributeData 属性: %s"), i, *AttrName); return; }

			FGameplayAttributeData* AttrData = FoundProp->ContainerPtrToValuePtr<FGameplayAttributeData>(CDO);
			if (!AttrData)
			{ OutError = FString::Printf(TEXT("ops[%d] 无法访问属性 %s 的 CDO 指针"), i, *AttrName); return; }

			if (Action == TEXT("set"))
			{
				if (!Op->HasField(TEXT("baseValue")))
				{ OutError = FString::Printf(TEXT("ops[%d] set 需要 baseValue"), i); return; }
				float BaseValue = (float)Op->GetNumberField(TEXT("baseValue"));
				AttrData->SetBaseValue(BaseValue);
				AttrData->SetCurrentValue(BaseValue);
			}
			else if (Action == TEXT("reset"))
			{
				AttrData->SetBaseValue(0.f);
				AttrData->SetCurrentValue(0.f);
			}
			else
			{
				OutError = FString::Printf(TEXT("ops[%d] 未知 action: %s（支持 set/reset）"), i, *Action);
				return;
			}
			++Applied;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);

		OutTop->SetStringField(TEXT("path"), AssetPath);
		OutTop->SetNumberField(TEXT("appliedOps"), Applied);
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetAttributeSetCapability)

#endif // WITH_GAS
