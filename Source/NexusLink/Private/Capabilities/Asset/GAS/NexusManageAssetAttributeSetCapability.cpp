// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusManageAssetAttributeSetCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
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
	Out.Description = TEXT("Batch set/reset AttributeSet CDO FGameplayAttributeData defaults.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),        FNexusSchema::Enum(TEXT("Action"), { TEXT("set"), TEXT("reset") }))
		.Prop(TEXT("attributeName"), FNexusSchema::Str(TEXT("Property name")))
		.Prop(TEXT("baseValue"),     FNexusSchema::Num(TEXT("default BaseValue (set)")))
		.Required({ TEXT("action"), TEXT("attributeName") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("AttributeSet Blueprint path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("attribute"), TEXT("default"), TEXT("health"), TEXT("stat") };
	Out.RelatedCapabilities = { TEXT("get_asset_attribute_set"), TEXT("save_asset"), TEXT("create_asset_attribute_set") };
	Out.WhenToUse = TEXT("Set AS property defaults; auto recompiles Blueprint");
}

FCapabilityResult FManageAssetAttributeSetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));

		const TArray<TSharedPtr<FJsonValue>> OpsArrVal = FNexusJsonUtils::ExtractOperations(Arguments);
		const TArray<TSharedPtr<FJsonValue>>* OpsArr = &OpsArrVal;
		if (OpsArr->Num() == 0)
		{ OutError = TEXT("operations is required and must be non-empty"); return; }

		FString LoadError;
		UBlueprint* BP = FNexusGasUtils::LoadAttributeSetBlueprint(AssetPath, LoadError);
		if (!BP) { OutError = LoadError; return; }
		if (!BP->GeneratedClass) { OutError = TEXT("Blueprint not compiled"); return; }

		UObject* CDO = BP->GeneratedClass->GetDefaultObject();
		if (!CDO) { OutError = TEXT("Unable to get CDO"); return; }

		int32 Applied = 0;
		for (int32 i = 0; i < OpsArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!(*OpsArr)[i].IsValid() || !(*OpsArr)[i]->TryGetObject(OpObjPtr) || !OpObjPtr)
			{ OutError = FString::Printf(TEXT("ops[%d] is not a valid JSON object"), i); return; }

			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;
			FString Action, AttrName;
			if (!Op->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
			{ OutError = FString::Printf(TEXT("ops[%d] missing action (set/reset)"), i); return; }
			if (!Op->TryGetStringField(TEXT("attributeName"), AttrName) || AttrName.IsEmpty())
			{ OutError = FString::Printf(TEXT("ops[%d] missing attributeName"), i); return; }

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
			{ OutError = FString::Printf(TEXT("ops[%d] FGameplayAttributeData property not found: %s"), i, *AttrName); return; }

			FGameplayAttributeData* AttrData = FoundProp->ContainerPtrToValuePtr<FGameplayAttributeData>(CDO);
			if (!AttrData)
			{ OutError = FString::Printf(TEXT("ops[%d] cannot access CDO pointer for property %s"), i, *AttrName); return; }

			if (Action == TEXT("set"))
			{
				if (!Op->HasField(TEXT("baseValue")))
				{ OutError = FString::Printf(TEXT("ops[%d] set requires baseValue"), i); return; }
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
				OutError = FString::Printf(TEXT("ops[%d] unknown action: %s (supports set/reset)"), i, *Action);
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
