// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusManageAssetAttributeSetCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusGasUtils.h"
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

struct FAttrSetActionState
{
	UBlueprint* BP = nullptr;
	UObject* CDO = nullptr;
	int32 Applied = 0;
	TSharedPtr<FJsonObject> OutTop;
};

static FAttrSetActionState* ASState(FNexusActionContext& Ctx)
{
	return static_cast<FAttrSetActionState*>(Ctx.Target);
}

static void MarkASApplied(FNexusActionContext& Ctx)
{
	if (FAttrSetActionState* S = ASState(Ctx))
	{
		++S->Applied;
	}
}

static FGameplayAttributeData* ResolveASAttr(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FAttrSetActionState* S = ASState(Ctx);
	FString AttrName;
	if (!Op->TryGetStringField(TEXT("attributeName"), AttrName) || AttrName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("missing attributeName"));
		return nullptr;
	}
	FStructProperty* FoundProp = nullptr;
	for (TFieldIterator<FProperty> PropIt(S->BP->GeneratedClass); PropIt; ++PropIt)
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
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("FGameplayAttributeData property not found: %s"), *AttrName));
		return nullptr;
	}
	FGameplayAttributeData* AttrData = FoundProp->ContainerPtrToValuePtr<FGameplayAttributeData>(S->CDO);
	if (!AttrData)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("cannot access CDO pointer for property %s"), *AttrName));
		return nullptr;
	}
	return AttrData;
}

static void HandleAS_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FGameplayAttributeData* AttrData = ResolveASAttr(Op, Ctx);
	if (!AttrData) return;
	if (!Op->HasField(TEXT("baseValue")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set requires baseValue"));
		return;
	}
	const float BaseValue = static_cast<float>(Op->GetNumberField(TEXT("baseValue")));
	AttrData->SetBaseValue(BaseValue);
	AttrData->SetCurrentValue(BaseValue);
	MarkASApplied(Ctx);
}

static void HandleAS_Reset(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FGameplayAttributeData* AttrData = ResolveASAttr(Op, Ctx);
	if (!AttrData) return;
	AttrData->SetBaseValue(0.f);
	AttrData->SetCurrentValue(0.f);
	MarkASApplied(Ctx);
}

bool FManageAssetAttributeSetCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	FString LoadError;
	UBlueprint* BP = FNexusGasUtils::LoadAttributeSetBlueprint(AssetPath, LoadError);
	if (!BP) { OutError = LoadError; return false; }
	if (!BP->GeneratedClass) { OutError = TEXT("Blueprint not compiled"); return false; }
	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	if (!CDO) { OutError = TEXT("Unable to get CDO"); return false; }
	FAttrSetActionState* State = new FAttrSetActionState();
	State->BP = BP;
	State->CDO = CDO;
	OutTarget = State;
	return true;
}

void FManageAssetAttributeSetCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	if (FAttrSetActionState* State = static_cast<FAttrSetActionState*>(Target))
	{
		State->OutTop = OutTop;
		OutTop->SetStringField(TEXT("path"), FNexusArgs(Args).Str(TEXT("assetPath")));
	}
}

void FManageAssetAttributeSetCapability::FinalizeTarget(void* Target) const
{
	FAttrSetActionState* State = static_cast<FAttrSetActionState*>(Target);
	if (!State) return;
	if (State->Applied > 0 && State->BP)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(State->BP);
		FKismetEditorUtilities::CompileBlueprint(State->BP);
	}
	if (State->OutTop.IsValid())
	{
		State->OutTop->SetNumberField(TEXT("appliedOps"), State->Applied);
	}
	delete State;
}

void FManageAssetAttributeSetCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set"),   &HandleAS_Set);
	OutHandlers.Add(TEXT("reset"), &HandleAS_Reset);
}

REGISTER_MCP_CAPABILITY(FManageAssetAttributeSetCapability)

#endif // WITH_GAS
