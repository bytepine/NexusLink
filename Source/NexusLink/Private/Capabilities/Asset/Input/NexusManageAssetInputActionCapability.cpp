// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusManageAssetInputActionCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
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
	Out.Description = TEXT("Edit InputAction: set_value_type/add_trigger/remove_trigger/add_modifier/remove_modifier/set_flags.");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("Operation type"),
			{
				TEXT("set_value_type"),
				TEXT("add_trigger"),
				TEXT("remove_trigger"),
				TEXT("add_modifier"),
				TEXT("remove_modifier"),
				TEXT("set_flags"),
			}))
		.Prop(TEXT("valueType"), FNexusSchema::Enum(
			TEXT("Required when set_value_type"),
			{ TEXT("Boolean"), TEXT("Axis1D"), TEXT("Axis2D"), TEXT("Axis3D") }))
		.Prop(TEXT("className"), FNexusSchema::Str(TEXT("Trigger/Modifier class short name (e.g. InputTriggerPressed)")))
		.Prop(TEXT("consumesInput"), FNexusSchema::Bool(TEXT("set_flags: consume input")))
		.Prop(TEXT("reserveAllMappings"), FNexusSchema::Bool(TEXT("set_flags: keep all mappings")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("InputAction asset path")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("action"), TEXT("ia"), TEXT("trigger"), TEXT("modifier"), TEXT("axis") };
	Out.RelatedCapabilities = { TEXT("get_asset_input_action"), TEXT("create_asset_input_action") };
	Out.WhenToUse = TEXT("Edit InputAction ValueType/Trigger/Modifier/flags");
}

FCapabilityResult FManageAssetInputActionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));

		UInputAction* IA = FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(AssetPath);
		if (!IA)
		{
			OutError = FString::Printf(TEXT("InputAction not found: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("operations is a required array");
			return;
		}

		bool bDirty = false;

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
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
					OpResult->SetStringField(TEXT("error"), TEXT("set_value_type requires valueType"));
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
					bDirty = true;
				}
			}
			else if (Action == TEXT("add_trigger"))
			{
				FString ClassName;
				if (!Op->TryGetStringField(TEXT("className"), ClassName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_trigger requires className"));
				}
				else
				{
					UClass* TriggerClass = FindClassByShortName(ClassName);
					if (!TriggerClass || !TriggerClass->IsChildOf(UInputTrigger::StaticClass()))
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Trigger class not found: %s"), *ClassName));
					}
					else
					{
						UInputTrigger* NewTrigger = NewObject<UInputTrigger>(IA, TriggerClass);
						IA->Triggers.Add(NewTrigger);
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
					OpResult->SetStringField(TEXT("error"), TEXT("remove_trigger requires className"));
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
					if (Removed > 0) bDirty = true;
				}
			}
			else if (Action == TEXT("add_modifier"))
			{
				FString ClassName;
				if (!Op->TryGetStringField(TEXT("className"), ClassName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_modifier requires className"));
				}
				else
				{
					UClass* ModClass = FindClassByShortName(ClassName);
					if (!ModClass || !ModClass->IsChildOf(UInputModifier::StaticClass()))
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Modifier class not found: %s"), *ClassName));
					}
					else
					{
						UInputModifier* NewMod = NewObject<UInputModifier>(IA, ModClass);
						IA->Modifiers.Add(NewMod);
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
					OpResult->SetStringField(TEXT("error"), TEXT("remove_modifier requires className"));
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
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
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
