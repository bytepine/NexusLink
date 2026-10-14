// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusManageAssetInputMappingContextCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "EnhancedActionKeyMapping.h"
#include "InputModifiers.h"
#include "InputTriggers.h"

void FNexusManageAssetInputMappingContextCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetInputMappingContext");
	Out.SearchAssetTypes = {TEXT("InputMappingContext")};
	Out.Description = TEXT("Edit IMC: AddMapping/RemoveMapping/ClearMappings.");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(
			TEXT("Operation type"),
			{ TEXT("AddMapping"), TEXT("RemoveMapping"), TEXT("ClearMappings") }))
		.Prop(TEXT("ActionPath"), FNexusSchema::Str(TEXT("add/RemoveMapping：InputAction asset path")))
		.Prop(TEXT("Key"),        FNexusSchema::Str(TEXT("add/RemoveMapping: key name (W/S/Gamepad_LeftX)")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("InputMappingContext asset path")))
		.Required(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("mapping"), TEXT("imc"), TEXT("keybind"), TEXT("Key"), TEXT("Action") };
	Out.RelatedCapabilities = { TEXT("GetAssetInputMappingContext"), TEXT("CreateAssetInputMappingContext") };
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
	if (!IMC)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("InputMappingContext is null"));
		return;
	}
	FString ActionPath, KeyName;
	if (!Op->TryGetStringField(TEXT("ActionPath"), ActionPath) || !Op->TryGetStringField(TEXT("Key"), KeyName))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddMapping requires ActionPath and key"));
		return;
	}
	UInputAction* IA = FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(ActionPath);
	if (!IA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("InputAction not found: %s"), *ActionPath));
		return;
	}
	IMC->MapKey(IA, FKey(*KeyName));
	Ctx.Entry->SetStringField(TEXT("AddedKey"), KeyName);
	MarkIMCDirty(Ctx);
}

static void HandleIMC_RemoveMapping(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputMappingContext* IMC = IMCFrom(Ctx);
	if (!IMC)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("InputMappingContext is null"));
		return;
	}
	FString ActionPath, KeyName;
	const bool bHasAction = Op->TryGetStringField(TEXT("ActionPath"), ActionPath);
	const bool bHasKey    = Op->TryGetStringField(TEXT("Key"), KeyName);
	if (!bHasAction && !bHasKey)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveMapping requires ActionPath or key (at least one)"));
		return;
	}
	UInputAction* FilterIA = bHasAction
		? FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(ActionPath)
		: nullptr;
	const FKey FilterKey = bHasKey ? FKey(*KeyName) : EKeys::Invalid;
	const TArray<FEnhancedActionKeyMapping>& Maps = IMC->GetMappings();
	const int32 Before = Maps.Num();
	if (FilterIA && bHasKey)
	{
		IMC->UnmapKey(FilterIA, FilterKey);
	}
	else if (FilterIA)
	{
#if NX_UE_HAS_IMC_UNMAP_ALL_KEYS_FROM_ACTION
		IMC->UnmapAllKeysFromAction(FilterIA);
#else
		IMC->UnmapAction(FilterIA);
#endif
	}
	else
	{
		TArray<FEnhancedActionKeyMapping> Copy = Maps;
		for (const FEnhancedActionKeyMapping& M : Copy)
		{
			// 5.0 的 Action 是裸指针，5.1+ 才是 TObjectPtr
			const UInputAction* Action = M.Action;
			if (Action && M.Key == FilterKey)
			{
				IMC->UnmapKey(Action, FilterKey);
			}
		}
	}
	const int32 Removed = Before - IMC->GetMappings().Num();
	Ctx.Entry->SetNumberField(TEXT("RemovedCount"), Removed);
	if (Removed > 0) MarkIMCDirty(Ctx);
}

static void HandleIMC_ClearMappings(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	(void)Op;
	UInputMappingContext* IMC = IMCFrom(Ctx);
	if (!IMC)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("InputMappingContext is null"));
		return;
	}
	const int32 Count = IMC->GetMappings().Num();
	IMC->UnmapAll();
	Ctx.Entry->SetNumberField(TEXT("ClearedCount"), Count);
	MarkIMCDirty(Ctx);
}

bool FNexusManageAssetInputMappingContextCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetInputMappingContextCapability::FinalizeTarget(void* Target) const
{
	FIMCActionState* State = static_cast<FIMCActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->IMC)
	{
		State->IMC->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetInputMappingContextCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("AddMapping"),    &HandleIMC_AddMapping);
	OutHandlers.Add(TEXT("RemoveMapping"), &HandleIMC_RemoveMapping);
	OutHandlers.Add(TEXT("ClearMappings"), &HandleIMC_ClearMappings);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetInputMappingContextCapability)

#endif // WITH_ENHANCED_INPUT
