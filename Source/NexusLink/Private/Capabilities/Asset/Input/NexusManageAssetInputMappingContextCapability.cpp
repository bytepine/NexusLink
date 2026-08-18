// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusManageAssetInputMappingContextCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusArgs.h"
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

struct FIMCActionState
{
	UInputMappingContext* IMC = nullptr;
	bool bDirty = false;
};

static FIMCActionState* IMCState(FNexusActionContext& Ctx)
{
	return static_cast<FIMCActionState*>(Ctx.Target);
}

static UInputMappingContext* IMCFrom(FNexusActionContext& Ctx)
{
	FIMCActionState* S = IMCState(Ctx);
	return S ? S->IMC : nullptr;
}

static void MarkIMCDirty(FNexusActionContext& Ctx)
{
	if (FIMCActionState* S = IMCState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleIMC_AddMapping(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputMappingContext* IMC = IMCFrom(Ctx);
	FString ActionPath, KeyName;
	if (!Op->TryGetStringField(TEXT("actionPath"), ActionPath) || !Op->TryGetStringField(TEXT("key"), KeyName))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_mapping requires actionPath and key"));
		return;
	}
	UInputAction* IA = FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(ActionPath);
	if (!IA)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("InputAction not found: %s"), *ActionPath));
		return;
	}
	FEnhancedActionKeyMapping NewMapping;
	NewMapping.Action = IA;
	NewMapping.Key = FKey(*KeyName);
	IMC->Mappings.Add(NewMapping);
	Ctx.Entry->SetStringField(TEXT("addedKey"), KeyName);
	MarkIMCDirty(Ctx);
}

static void HandleIMC_RemoveMapping(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputMappingContext* IMC = IMCFrom(Ctx);
	FString ActionPath, KeyName;
	const bool bHasAction = Op->TryGetStringField(TEXT("actionPath"), ActionPath);
	const bool bHasKey    = Op->TryGetStringField(TEXT("key"), KeyName);
	if (!bHasAction && !bHasKey)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_mapping requires actionPath or key (at least one)"));
		return;
	}
	UInputAction* FilterIA = bHasAction
		? FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(ActionPath)
		: nullptr;
	const FKey FilterKey = bHasKey ? FKey(*KeyName) : EKeys::Invalid;
	const int32 Before = IMC->Mappings.Num();
	IMC->Mappings.RemoveAll([&](const FEnhancedActionKeyMapping& M)
	{
		const bool bMatchAction = !FilterIA || M.Action.Get() == FilterIA;
		const bool bMatchKey    = !bHasKey  || M.Key == FilterKey;
		return bMatchAction && bMatchKey;
	});
	const int32 Removed = Before - IMC->Mappings.Num();
	Ctx.Entry->SetNumberField(TEXT("removedCount"), Removed);
	if (Removed > 0) MarkIMCDirty(Ctx);
}

static void HandleIMC_ClearMappings(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	(void)Op;
	UInputMappingContext* IMC = IMCFrom(Ctx);
	const int32 Count = IMC->Mappings.Num();
	IMC->Mappings.Empty();
	Ctx.Entry->SetNumberField(TEXT("clearedCount"), Count);
	MarkIMCDirty(Ctx);
}

bool FManageAssetInputMappingContextCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UInputMappingContext* IMC = FNexusAssetUtils::LoadAssetWithFallback<UInputMappingContext>(AssetPath);
	if (!IMC)
	{
		OutError = FString::Printf(TEXT("InputMappingContext not found: %s"), *AssetPath);
		return false;
	}
	FIMCActionState* State = new FIMCActionState();
	State->IMC = IMC;
	OutTarget = State;
	return true;
}

void FManageAssetInputMappingContextCapability::FinalizeTarget(void* Target) const
{
	FIMCActionState* State = static_cast<FIMCActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->IMC)
	{
		State->IMC->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetInputMappingContextCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add_mapping"),    &HandleIMC_AddMapping);
	OutHandlers.Add(TEXT("remove_mapping"), &HandleIMC_RemoveMapping);
	OutHandlers.Add(TEXT("clear_mappings"), &HandleIMC_ClearMappings);
}

REGISTER_MCP_CAPABILITY(FManageAssetInputMappingContextCapability)

#endif // WITH_ENHANCED_INPUT
