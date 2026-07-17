// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusManageAssetInputActionCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

// ── 辅助：按类名查找 UClass（兼容 4.27/5.x）────────────────────────────────

static UClass* FindClassByShortName(const FString& ClassName)
{
#if NX_UE_HAS_FIND_FIRST_OBJECT
	return FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
#else
	return FindObject<UClass>(ANY_PACKAGE, *ClassName);
#endif
}

// ── Capability ────────────────────────────────────────────────────────────────

void FManageAssetInputActionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_input_action");
	Out.SearchAssetTypes = {TEXT("InputAction")};
	Out.Description = TEXT("编辑 InputAction：set_value_type/add_trigger/remove_trigger/add_modifier/remove_modifier/set_flags。");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("操作类型"),
			{
				TEXT("set_value_type"),
				TEXT("add_trigger"),
				TEXT("remove_trigger"),
				TEXT("add_modifier"),
				TEXT("remove_modifier"),
				TEXT("set_flags"),
			}))
		.Prop(TEXT("valueType"), FNexusSchema::Enum(
			TEXT("set_value_type 时必填"),
			{ TEXT("Boolean"), TEXT("Axis1D"), TEXT("Axis2D"), TEXT("Axis3D") }))
		.Prop(TEXT("className"), FNexusSchema::Str(TEXT("Trigger/Modifier 类短名（如 InputTriggerPressed）")))
		.Prop(TEXT("consumesInput"), FNexusSchema::Bool(TEXT("set_flags：是否消耗输入")))
		.Prop(TEXT("reserveAllMappings"), FNexusSchema::Bool(TEXT("set_flags：保留全部映射")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("InputAction 资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("action"), TEXT("ia"), TEXT("trigger"), TEXT("modifier"), TEXT("axis") };
	Out.RelatedCapabilities = { TEXT("get_asset_input_action"), TEXT("create_asset_input_action") };
	Out.WhenToUse = TEXT("改 InputAction 的 ValueType/Trigger/Modifier/标志位");
}

FCapabilityResult FManageAssetInputActionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UInputAction* IA = FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(AssetPath);
		if (!IA)
		{
			OutError = FString::Printf(TEXT("InputAction 未找到: %s"), *AssetPath);
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

			if (Action == TEXT("set_value_type"))
			{
				FString VT;
				if (!Op->TryGetStringField(TEXT("valueType"), VT))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("set_value_type 需要 valueType"));
				}
				else
				{
					if (VT == TEXT("Axis1D"))
						IA->ValueType = EInputActionValueType::Axis1D;
					else if (VT == TEXT("Axis2D"))
						IA->ValueType = EInputActionValueType::Axis2D;
					else if (VT == TEXT("Axis3D"))
						IA->ValueType = EInputActionValueType::Axis3D;
					else
						IA->ValueType = EInputActionValueType::Boolean;
					OpResult->SetBoolField(TEXT("success"), true);
					bDirty = true;
				}
			}
			else if (Action == TEXT("add_trigger"))
			{
				FString ClassName;
				if (!Op->TryGetStringField(TEXT("className"), ClassName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_trigger 需要 className"));
				}
				else
				{
					UClass* TriggerClass = FindClassByShortName(ClassName);
					if (!TriggerClass || !TriggerClass->IsChildOf(UInputTrigger::StaticClass()))
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未找到 Trigger 类: %s"), *ClassName));
					}
					else
					{
						UInputTrigger* NewTrigger = NewObject<UInputTrigger>(IA, TriggerClass);
						IA->Triggers.Add(NewTrigger);
						OpResult->SetBoolField(TEXT("success"), true);
						OpResult->SetStringField(TEXT("addedTrigger"), TriggerClass->GetName());
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("remove_trigger"))
			{
				FString ClassName;
				if (!Op->TryGetStringField(TEXT("className"), ClassName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("remove_trigger 需要 className"));
				}
				else
				{
					int32 Before = IA->Triggers.Num();
					IA->Triggers.RemoveAll([&](const TObjectPtr<UInputTrigger>& T)
					{
						return T && T->GetClass()->GetName().Equals(ClassName, ESearchCase::IgnoreCase);
					});
					int32 Removed = Before - IA->Triggers.Num();
					OpResult->SetNumberField(TEXT("removedCount"), Removed);
					OpResult->SetBoolField(TEXT("success"), Removed > 0);
					if (Removed > 0) bDirty = true;
				}
			}
			else if (Action == TEXT("add_modifier"))
			{
				FString ClassName;
				if (!Op->TryGetStringField(TEXT("className"), ClassName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_modifier 需要 className"));
				}
				else
				{
					UClass* ModClass = FindClassByShortName(ClassName);
					if (!ModClass || !ModClass->IsChildOf(UInputModifier::StaticClass()))
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未找到 Modifier 类: %s"), *ClassName));
					}
					else
					{
						UInputModifier* NewMod = NewObject<UInputModifier>(IA, ModClass);
						IA->Modifiers.Add(NewMod);
						OpResult->SetBoolField(TEXT("success"), true);
						OpResult->SetStringField(TEXT("addedModifier"), ModClass->GetName());
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("remove_modifier"))
			{
				FString ClassName;
				if (!Op->TryGetStringField(TEXT("className"), ClassName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("remove_modifier 需要 className"));
				}
				else
				{
					int32 Before = IA->Modifiers.Num();
					IA->Modifiers.RemoveAll([&](const TObjectPtr<UInputModifier>& M)
					{
						return M && M->GetClass()->GetName().Equals(ClassName, ESearchCase::IgnoreCase);
					});
					int32 Removed = Before - IA->Modifiers.Num();
					OpResult->SetNumberField(TEXT("removedCount"), Removed);
					OpResult->SetBoolField(TEXT("success"), Removed > 0);
					if (Removed > 0) bDirty = true;
				}
			}
			else if (Action == TEXT("set_flags"))
			{
				bool bConsumeVal;
				if (Op->TryGetBoolField(TEXT("consumesInput"), bConsumeVal))
				{
					IA->bConsumesInput = bConsumeVal;
					bDirty = true;
				}
				bool bReserveVal;
				if (Op->TryGetBoolField(TEXT("reserveAllMappings"), bReserveVal))
				{
					IA->bReserveAllMappings = bReserveVal;
					bDirty = true;
				}
				OpResult->SetBoolField(TEXT("success"), true);
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OpResult));
		}

		if (bDirty)
		{
			IA->MarkPackageDirty();
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetInputActionCapability)

#endif // WITH_ENHANCED_INPUT
