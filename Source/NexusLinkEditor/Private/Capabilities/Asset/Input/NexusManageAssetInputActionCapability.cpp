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

void FNexusManageAssetInputActionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetInputAction");
	Out.SearchAssetTypes = {TEXT("InputAction")};
	Out.Description = TEXT("Edit InputAction: SetValueType/AddTrigger/RemoveTrigger/AddModifier/RemoveModifier/SetFlags.");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(
			TEXT("Operation type"),
			{
				TEXT("SetValueType"),
				TEXT("AddTrigger"),
				TEXT("RemoveTrigger"),
				TEXT("AddModifier"),
				TEXT("RemoveModifier"),
				TEXT("SetFlags"),
			}))
		.Prop(TEXT("ValueType"), FNexusSchema::Enum(
			TEXT("Required when SetValueType"),
			{ TEXT("Boolean"), TEXT("Axis1D"), TEXT("Axis2D"), TEXT("Axis3D") }))
		.Prop(TEXT("ClassName"), FNexusSchema::Str(TEXT("Trigger/Modifier class short name (e.g. InputTriggerPressed)")))
		.Prop(TEXT("bConsumesInput"), FNexusSchema::Bool(TEXT("SetFlags: consume input")))
		.Prop(TEXT("bReserveAllMappings"), FNexusSchema::Bool(TEXT("SetFlags: keep all mappings")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("InputAction asset path")))
		.Required(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("Action"), TEXT("ia"), TEXT("Trigger"), TEXT("modifier"), TEXT("axis") };
	Out.RelatedCapabilities = { TEXT("GetAssetInputAction"), TEXT("CreateAssetInputAction") };
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
	if (!IA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("InputAction is null"));
		return;
	}
	FString VT;
	if (!Op->TryGetStringField(TEXT("ValueType"), VT))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetValueType requires ValueType"));
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
	if (!IA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("InputAction is null"));
		return;
	}
	FString ClassName;
	if (!Op->TryGetStringField(TEXT("ClassName"), ClassName))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddTrigger requires ClassName"));
		return;
	}
	UClass* TriggerClass = FindClassByShortName(ClassName);
	if (!TriggerClass || !TriggerClass->IsChildOf(UInputTrigger::StaticClass()))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Trigger class not found: %s"), *ClassName));
		return;
	}
	UInputTrigger* NewTrigger = NewObject<UInputTrigger>(IA, TriggerClass);
	IA->Triggers.Add(NewTrigger);
	Ctx.Entry->SetStringField(TEXT("AddedTrigger"), TriggerClass->GetName());
	MarkIADirty(Ctx);
}

static void HandleIA_RemoveTrigger(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputAction* IA = IAFrom(Ctx);
	if (!IA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("InputAction is null"));
		return;
	}
	FString ClassName;
	if (!Op->TryGetStringField(TEXT("ClassName"), ClassName))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveTrigger requires ClassName"));
		return;
	}
	const int32 Before = IA->Triggers.Num();
	IA->Triggers.RemoveAll([&](const TObjectPtr<UInputTrigger>& T)
	{
		return T && T->GetClass()->GetName().Equals(ClassName, ESearchCase::IgnoreCase);
	});
	const int32 Removed = Before - IA->Triggers.Num();
	Ctx.Entry->SetNumberField(TEXT("RemovedCount"), Removed);
	if (Removed > 0) MarkIADirty(Ctx);
}

static void HandleIA_AddModifier(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputAction* IA = IAFrom(Ctx);
	if (!IA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("InputAction is null"));
		return;
	}
	FString ClassName;
	if (!Op->TryGetStringField(TEXT("ClassName"), ClassName))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddModifier requires ClassName"));
		return;
	}
	UClass* ModClass = FindClassByShortName(ClassName);
	if (!ModClass || !ModClass->IsChildOf(UInputModifier::StaticClass()))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Modifier class not found: %s"), *ClassName));
		return;
	}
	UInputModifier* NewMod = NewObject<UInputModifier>(IA, ModClass);
	IA->Modifiers.Add(NewMod);
	Ctx.Entry->SetStringField(TEXT("AddedModifier"), ModClass->GetName());
	MarkIADirty(Ctx);
}

static void HandleIA_RemoveModifier(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputAction* IA = IAFrom(Ctx);
	if (!IA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("InputAction is null"));
		return;
	}
	FString ClassName;
	if (!Op->TryGetStringField(TEXT("ClassName"), ClassName))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveModifier requires ClassName"));
		return;
	}
	const int32 Before = IA->Modifiers.Num();
	IA->Modifiers.RemoveAll([&](const TObjectPtr<UInputModifier>& M)
	{
		return M && M->GetClass()->GetName().Equals(ClassName, ESearchCase::IgnoreCase);
	});
	const int32 Removed = Before - IA->Modifiers.Num();
	Ctx.Entry->SetNumberField(TEXT("RemovedCount"), Removed);
	if (Removed > 0) MarkIADirty(Ctx);
}

static void HandleIA_SetFlags(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UInputAction* IA = IAFrom(Ctx);
	if (!IA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("InputAction is null"));
		return;
	}
	bool bConsumeVal;
	if (Op->TryGetBoolField(TEXT("bConsumesInput"), bConsumeVal))
	{
		IA->bConsumeInput = bConsumeVal;
		MarkIADirty(Ctx);
	}
	bool bReserveVal;
	if (Op->TryGetBoolField(TEXT("bReserveAllMappings"), bReserveVal))
	{
		IA->bReserveAllMappings = bReserveVal;
		MarkIADirty(Ctx);
	}
}

bool FNexusManageAssetInputActionCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetInputActionCapability::FinalizeTarget(void* Target) const
{
	FInputActionState* State = static_cast<FInputActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->IA)
	{
		State->IA->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetInputActionCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetValueType"),  &HandleIA_SetValueType);
	OutHandlers.Add(TEXT("AddTrigger"),     &HandleIA_AddTrigger);
	OutHandlers.Add(TEXT("RemoveTrigger"),  &HandleIA_RemoveTrigger);
	OutHandlers.Add(TEXT("AddModifier"),    &HandleIA_AddModifier);
	OutHandlers.Add(TEXT("RemoveModifier"), &HandleIA_RemoveModifier);
	OutHandlers.Add(TEXT("SetFlags"),       &HandleIA_SetFlags);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetInputActionCapability)

#endif // WITH_ENHANCED_INPUT
