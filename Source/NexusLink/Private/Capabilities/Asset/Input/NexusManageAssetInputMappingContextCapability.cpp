// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusManageAssetInputMappingContextCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "EnhancedActionKeyMapping.h"
#include "InputModifiers.h"
#include "InputTriggers.h"

void FManageAssetInputMappingContextCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_input_mapping_context");
	Out.Description = TEXT("编辑 IMC：add_mapping/remove_mapping/clear_mappings。");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("操作类型"),
			{ TEXT("add_mapping"), TEXT("remove_mapping"), TEXT("clear_mappings") }))
		.Prop(TEXT("actionPath"), FNexusSchema::Str(TEXT("add/remove_mapping：InputAction 资产路径")))
		.Prop(TEXT("key"),        FNexusSchema::Str(TEXT("add/remove_mapping：键名（W/S/Gamepad_LeftX）")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("InputMappingContext 资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("mapping"), TEXT("imc"), TEXT("keybind"), TEXT("key"), TEXT("action") };
	Out.RelatedCapabilities = { TEXT("get_asset_input_mapping_context"), TEXT("create_asset_input_mapping_context") };
	Out.WhenToUse = TEXT("往 IMC 里添加或移除 Action-Key 绑定");
}

FCapabilityResult FManageAssetInputMappingContextCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UInputMappingContext* IMC = FNexusAssetUtils::LoadAssetWithFallback<UInputMappingContext>(AssetPath);
		if (!IMC)
		{
			OutError = FString::Printf(TEXT("InputMappingContext 未找到: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Ops;
		if (!Arguments->TryGetArrayField(TEXT("operations"), Ops) || !Ops)
		{
			OutError = TEXT("operations 为必填数组");
			return;
		}

		bool bDirty = false;

		for (const TSharedPtr<FJsonValue>& OpVal : *Ops)
		{
			TSharedPtr<FJsonObject> Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;

			TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			if (Action == TEXT("add_mapping"))
			{
				FString ActionPath, KeyName;
				if (!Op->TryGetStringField(TEXT("actionPath"), ActionPath) || !Op->TryGetStringField(TEXT("key"), KeyName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_mapping 需要 actionPath 和 key"));
				}
				else
				{
					UInputAction* IA = FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(ActionPath);
					if (!IA)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("InputAction 未找到: %s"), *ActionPath));
					}
			else
				{
					FKey Key(*KeyName);
					FEnhancedActionKeyMapping NewMapping;
					NewMapping.Action = IA;
					NewMapping.Key = Key;
					IMC->Mappings.Add(NewMapping);
					OpResult->SetBoolField(TEXT("success"), true);
					OpResult->SetStringField(TEXT("addedKey"), KeyName);
					bDirty = true;
				}
				}
			}
			else if (Action == TEXT("remove_mapping"))
			{
				FString ActionPath, KeyName;
				bool bHasAction = Op->TryGetStringField(TEXT("actionPath"), ActionPath);
				bool bHasKey    = Op->TryGetStringField(TEXT("key"), KeyName);

				if (!bHasAction && !bHasKey)
				{
					OpResult->SetStringField(TEXT("error"), TEXT("remove_mapping 需要 actionPath 或 key（至少一个）"));
				}
				else
				{
					UInputAction* FilterIA = bHasAction
						? FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(ActionPath)
						: nullptr;
					FKey FilterKey = bHasKey ? FKey(*KeyName) : EKeys::Invalid;

					int32 Before = IMC->Mappings.Num();
					IMC->Mappings.RemoveAll([&](const FEnhancedActionKeyMapping& M)
					{
						bool bMatchAction = !FilterIA || M.Action.Get() == FilterIA;
						bool bMatchKey    = !bHasKey  || M.Key == FilterKey;
						return bMatchAction && bMatchKey;
					});
					int32 Removed = Before - IMC->Mappings.Num();
					OpResult->SetNumberField(TEXT("removedCount"), Removed);
					OpResult->SetBoolField(TEXT("success"), Removed > 0);
					if (Removed > 0) bDirty = true;
				}
			}
			else if (Action == TEXT("clear_mappings"))
			{
				int32 Count = IMC->Mappings.Num();
				IMC->Mappings.Empty();
				OpResult->SetNumberField(TEXT("clearedCount"), Count);
				OpResult->SetBoolField(TEXT("success"), true);
				bDirty = true;
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OpResult));
		}

		if (bDirty)
		{
			IMC->MarkPackageDirty();
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetInputMappingContextCapability)

#endif // WITH_ENHANCED_INPUT
