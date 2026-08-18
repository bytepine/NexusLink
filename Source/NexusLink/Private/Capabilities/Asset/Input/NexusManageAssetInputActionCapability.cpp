// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusManageAssetInputActionCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusArgs.h"
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

struct FInputActionState
{
	UInputAction* IA = nullptr;
	bool bDirty = false;
};

static FInputActionState* IAState(FNexusActionContext& Ctx)
{
	return static_cast<FInputActionState*>(Ctx.Target);
}

static UInputAction* IAFrom(FNexusActionContext& Ctx)
{
	FInputActionState* S = IAState(Ctx);
	return S ? S->IA : nullptr;
}

static void MarkIADirty(FNexusActionContext& Ctx)
{
	if (FInputActionState* S = IAState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleIA_SetValueType(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputAction* IA = IAFrom(Ctx);
	FString VT;
	if (!Op->TryGetStringField(TEXT("valueType"), VT))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_value_type requires valueType"));
		return;
	}
	if (VT == TEXT("Axis1D"))
		IA->ValueType = EInputActionValueType::Axis1D;
	else if (VT == TEXT("Axis2D"))
		IA->ValueType = EInputActionValueType::Axis2D;
	else if (VT == TEXT("Axis3D"))
		IA->ValueType = EInputActionValueType::Axis3D;
	else
		IA->ValueType = EInputActionValueType::Boolean;
	MarkIADirty(Ctx);
}

static void HandleIA_AddTrigger(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputAction* IA = IAFrom(Ctx);
	FString ClassName;
	if (!Op->TryGetStringField(TEXT("className"), ClassName))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_trigger requires className"));
		return;
	}
	UClass* TriggerClass = FindClassByShortName(ClassName);
	if (!TriggerClass || !TriggerClass->IsChildOf(UInputTrigger::StaticClass()))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Trigger class not found: %s"), *ClassName));
		return;
	}
	UInputTrigger* NewTrigger = NewObject<UInputTrigger>(IA, TriggerClass);
	IA->Triggers.Add(NewTrigger);
	Ctx.Entry->SetStringField(TEXT("addedTrigger"), TriggerClass->GetName());
	MarkIADirty(Ctx);
}

static void HandleIA_RemoveTrigger(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputAction* IA = IAFrom(Ctx);
	FString ClassName;
	if (!Op->TryGetStringField(TEXT("className"), ClassName))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_trigger requires className"));
		return;
	}
	const int32 Before = IA->Triggers.Num();
	IA->Triggers.RemoveAll([&](const TObjectPtr<UInputTrigger>& T)
	{
		return T && T->GetClass()->GetName().Equals(ClassName, ESearchCase::IgnoreCase);
	});
	const int32 Removed = Before - IA->Triggers.Num();
	Ctx.Entry->SetNumberField(TEXT("removedCount"), Removed);
	if (Removed > 0) MarkIADirty(Ctx);
}

static void HandleIA_AddModifier(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputAction* IA = IAFrom(Ctx);
	FString ClassName;
	if (!Op->TryGetStringField(TEXT("className"), ClassName))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_modifier requires className"));
		return;
	}
	UClass* ModClass = FindClassByShortName(ClassName);
	if (!ModClass || !ModClass->IsChildOf(UInputModifier::StaticClass()))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Modifier class not found: %s"), *ClassName));
		return;
	}
	UInputModifier* NewMod = NewObject<UInputModifier>(IA, ModClass);
	IA->Modifiers.Add(NewMod);
	Ctx.Entry->SetStringField(TEXT("addedModifier"), ModClass->GetName());
	MarkIADirty(Ctx);
}

static void HandleIA_RemoveModifier(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputAction* IA = IAFrom(Ctx);
	FString ClassName;
	if (!Op->TryGetStringField(TEXT("className"), ClassName))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_modifier requires className"));
		return;
	}
	const int32 Before = IA->Modifiers.Num();
	IA->Modifiers.RemoveAll([&](const TObjectPtr<UInputModifier>& M)
	{
		return M && M->GetClass()->GetName().Equals(ClassName, ESearchCase::IgnoreCase);
	});
	const int32 Removed = Before - IA->Modifiers.Num();
	Ctx.Entry->SetNumberField(TEXT("removedCount"), Removed);
	if (Removed > 0) MarkIADirty(Ctx);
}

static void HandleIA_SetFlags(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputAction* IA = IAFrom(Ctx);
	bool bConsumeVal;
	if (Op->TryGetBoolField(TEXT("consumesInput"), bConsumeVal))
	{
		IA->bConsumesInput = bConsumeVal;
		MarkIADirty(Ctx);
	}
	bool bReserveVal;
	if (Op->TryGetBoolField(TEXT("reserveAllMappings"), bReserveVal))
	{
		IA->bReserveAllMappings = bReserveVal;
		MarkIADirty(Ctx);
	}
}

bool FManageAssetInputActionCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UInputAction* IA = FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(AssetPath);
	if (!IA)
	{
		OutError = FString::Printf(TEXT("InputAction not found: %s"), *AssetPath);
		return false;
	}
	FInputActionState* State = new FInputActionState();
	State->IA = IA;
	OutTarget = State;
	return true;
}

void FManageAssetInputActionCapability::FinalizeTarget(void* Target) const
{
	FInputActionState* State = static_cast<FInputActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->IA)
	{
		State->IA->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetInputActionCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_value_type"),  &HandleIA_SetValueType);
	OutHandlers.Add(TEXT("add_trigger"),     &HandleIA_AddTrigger);
	OutHandlers.Add(TEXT("remove_trigger"),  &HandleIA_RemoveTrigger);
	OutHandlers.Add(TEXT("add_modifier"),    &HandleIA_AddModifier);
	OutHandlers.Add(TEXT("remove_modifier"), &HandleIA_RemoveModifier);
	OutHandlers.Add(TEXT("set_flags"),       &HandleIA_SetFlags);
}

REGISTER_MCP_CAPABILITY(FManageAssetInputActionCapability)

#endif // WITH_ENHANCED_INPUT
