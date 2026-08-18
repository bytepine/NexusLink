// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusManageAssetInputMappingContextCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
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
	Out.SearchAssetTypes = {TEXT("InputMappingContext")};
	Out.Description = TEXT("Edit IMC: add_mapping/remove_mapping/clear_mappings.");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("Operation type"),
			{ TEXT("add_mapping"), TEXT("remove_mapping"), TEXT("clear_mappings") }))
		.Prop(TEXT("actionPath"), FNexusSchema::Str(TEXT("add/remove_mapping：InputAction asset path")))
		.Prop(TEXT("key"),        FNexusSchema::Str(TEXT("add/remove_mapping: key name (W/S/Gamepad_LeftX)")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("InputMappingContext asset path")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("mapping"), TEXT("imc"), TEXT("keybind"), TEXT("key"), TEXT("action") };
	Out.RelatedCapabilities = { TEXT("get_asset_input_mapping_context"), TEXT("create_asset_input_mapping_context") };
	Out.WhenToUse = TEXT("Add or remove Action-Key bindings in IMC");
}

FCapabilityResult FManageAssetInputMappingContextCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));

		UInputMappingContext* IMC = FNexusAssetUtils::LoadAssetWithFallback<UInputMappingContext>(AssetPath);
		if (!IMC)
		{
			OutError = FString::Printf(TEXT("InputMappingContext not found: %s"), *AssetPath);
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

			if (Action == TEXT("add_mapping"))
			{
				FString ActionPath, KeyName;
				if (!Op->TryGetStringField(TEXT("actionPath"), ActionPath) || !Op->TryGetStringField(TEXT("key"), KeyName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_mapping requires actionPath and key"));
				}
				else
				{
					UInputAction* IA = FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(ActionPath);
					if (!IA)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("InputAction not found: %s"), *ActionPath));
					}
			else
				{
					FKey Key(*KeyName);
					FEnhancedActionKeyMapping NewMapping;
					NewMapping.Action = IA;
					NewMapping.Key = Key;
					IMC->Mappings.Add(NewMapping);
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
					OpResult->SetStringField(TEXT("error"), TEXT("remove_mapping requires actionPath or key (at least one)"));
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
					if (Removed > 0) bDirty = true;
				}
			}
			else if (Action == TEXT("clear_mappings"))
			{
				int32 Count = IMC->Mappings.Num();
				IMC->Mappings.Empty();
				OpResult->SetNumberField(TEXT("clearedCount"), Count);
				bDirty = true;
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
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
