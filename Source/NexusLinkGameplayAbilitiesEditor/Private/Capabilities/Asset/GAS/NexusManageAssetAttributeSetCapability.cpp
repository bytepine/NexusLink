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

void FNexusManageAssetAttributeSetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetAttributeSet");
	Out.SearchAssetTypes = {TEXT("AttributeSet")};
	Out.Description = TEXT("Batch set/reset AttributeSet CDO FGameplayAttributeData defaults.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),        FNexusSchema::Enum(TEXT("Action"), { TEXT("Set"), TEXT("Reset") }))
		.Prop(TEXT("AttributeName"), FNexusSchema::Str(TEXT("Property name")))
		.Prop(TEXT("BaseValue"),     FNexusSchema::Num(TEXT("default BaseValue (set)")))
		.Required({ TEXT("Action"), TEXT("AttributeName") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("AttributeSet Blueprint path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("Attribute"), TEXT("default"), TEXT("health"), TEXT("stat") };
	Out.RelatedCapabilities = { TEXT("GetAssetAttributeSet"), TEXT("SaveAsset"), TEXT("CreateAssetAttributeSet") };
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
	if (!Op->TryGetStringField(TEXT("AttributeName"), AttrName) || AttrName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("missing AttributeName"));
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
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("FGameplayAttributeData property not found: %s"), *AttrName));
		return nullptr;
	}
	FGameplayAttributeData* AttrData = FoundProp->ContainerPtrToValuePtr<FGameplayAttributeData>(S->CDO);
	if (!AttrData)
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("cannot access CDO pointer for property %s"), *AttrName));
		return nullptr;
	}
	return AttrData;
}

static void HandleAS_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FGameplayAttributeData* AttrData = ResolveASAttr(Op, Ctx);
	if (!AttrData) return;
	if (!Op->HasField(TEXT("BaseValue")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("set requires BaseValue"));
		return;
	}
	const float BaseValue = static_cast<float>(Op->GetNumberField(TEXT("BaseValue")));
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

bool FNexusManageAssetAttributeSetCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetAttributeSetCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	if (FAttrSetActionState* State = static_cast<FAttrSetActionState*>(Target))
	{
		State->OutTop = OutTop;
		OutTop->SetStringField(TEXT("Path"), FNexusArgs(Args).Str(TEXT("AssetPath")));
	}
}

void FNexusManageAssetAttributeSetCapability::FinalizeTarget(void* Target) const
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
		State->OutTop->SetNumberField(TEXT("AppliedOps"), State->Applied);
	}
	delete State;
}

void FNexusManageAssetAttributeSetCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("Set"),   &HandleAS_Set);
	OutHandlers.Add(TEXT("Reset"), &HandleAS_Reset);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetAttributeSetCapability)

#endif // WITH_GAS
