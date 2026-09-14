// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusManageAssetGameplayEffectCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusArgs.h"
#include "GameplayEffect.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "NexusMcpTool.h"

// ── FInheritedTagContainer.Added 反射访问辅助 ─────────────────────────────────

static FGameplayTagContainer* GetGE_InheritedTagAddedMut(UObject* CDO, const TCHAR* PropName)
{
	FStructProperty* P = FindFProperty<FStructProperty>(CDO->GetClass(), PropName);
	if (!P) return nullptr;
	FInheritedTagContainer* IC = P->ContainerPtrToValuePtr<FInheritedTagContainer>(CDO);
	return IC ? &IC->Added : nullptr;
}

// ── Capability 定义 ──────────────────────────────────────────────────────────

void FManageAssetGameplayEffectCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_gameplay_effect");
	Out.SearchAssetTypes = {TEXT("GameplayEffect")};
	Out.Description = TEXT("Batch edit GE CDO: set_policy/set_tags/add/remove/set modifier.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),         FNexusSchema::Enum(TEXT("Operation type"),
			{ TEXT("set_policy"), TEXT("set_tags"), TEXT("add_modifier"), TEXT("remove_modifier"), TEXT("set_modifier") }))
		.Prop(TEXT("durationPolicy"), FNexusSchema::Enum(TEXT("Duration policy (set_policy)"),
			{ TEXT("Instant"), TEXT("Infinite"), TEXT("HasDuration") }))
		.Prop(TEXT("duration"),       FNexusSchema::Num(TEXT("Duration (set_policy)")))
		.Prop(TEXT("period"),         FNexusSchema::Num(TEXT("Period (set_policy)")))
		.Prop(TEXT("tagContainer"),   FNexusSchema::Enum(TEXT("Tag container (set_tags)"),
			{ TEXT("gameplayEffectTags"), TEXT("grantedTags"), TEXT("blockedAbilityTags") }))
		.Prop(TEXT("tags"),           FNexusSchema::StrArr(TEXT("Tag string array (set_tags)")))
		.Prop(TEXT("mode"),           FNexusSchema::Enum(TEXT("set/add/remove"), { TEXT("set"), TEXT("add"), TEXT("remove") }))
		.Prop(TEXT("attribute"),      FNexusSchema::Str(TEXT("Attribute name (add_modifier)")))
		.Prop(TEXT("modifierOp"),     FNexusSchema::Enum(TEXT("Modifier operation (add_modifier)"),
			{ TEXT("Add"), TEXT("Multiply"), TEXT("Divide"), TEXT("Override") }))
		.Prop(TEXT("magnitude"),      FNexusSchema::Num(TEXT("Magnitude (add_modifier/set_modifier)")))
		.Prop(TEXT("index"),          FNexusSchema::Num(TEXT("Modifier index (remove_modifier/set_modifier)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("GameplayEffect Blueprint path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("effect"), TEXT("modifier"), TEXT("duration"), TEXT("tag") };
	Out.RelatedCapabilities = { TEXT("get_asset_gameplay_effect"), TEXT("save_asset"), TEXT("create_asset_gameplay_effect") };
	Out.WhenToUse = TEXT("Batch edit GE policy/tags/modifiers; auto recompiles Blueprint");
}

struct FGEActionState
{
	UBlueprint* BP = nullptr;
	UObject* CDO = nullptr;
	UGameplayEffect* GECDO = nullptr;
	int32 Applied = 0;
	TSharedPtr<FJsonObject> OutTop;
};

static FGEActionState* GEState(FNexusActionContext& Ctx)
{
	return static_cast<FGEActionState*>(Ctx.Target);
}

static void MarkGEApplied(FNexusActionContext& Ctx)
{
	if (FGEActionState* S = GEState(Ctx))
	{
		++S->Applied;
	}
}

static void HandleGE_SetPolicy(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* CDO = GEState(Ctx)->CDO;
	FString PolicyStr;
	if (Op->TryGetStringField(TEXT("durationPolicy"), PolicyStr) && !PolicyStr.IsEmpty())
	{
		uint8 V = 0;
		if      (PolicyStr == TEXT("Instant"))     V = (uint8)EGameplayEffectDurationType::Instant;
		else if (PolicyStr == TEXT("Infinite"))    V = (uint8)EGameplayEffectDurationType::Infinite;
		else if (PolicyStr == TEXT("HasDuration")) V = (uint8)EGameplayEffectDurationType::HasDuration;
		else
		{
			Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("invalid durationPolicy: %s"), *PolicyStr));
			return;
		}
		NxGasSetEnumByte(CDO, TEXT("DurationPolicy"), V);
	}
	if (Op->HasField(TEXT("duration")))
	{
		const float DurVal = static_cast<float>(Op->GetNumberField(TEXT("duration")));
		if (FGameplayEffectModifierMagnitude* M = NxGasPropPtr<FGameplayEffectModifierMagnitude>(CDO, TEXT("DurationMagnitude")))
			*M = FGameplayEffectModifierMagnitude(FScalableFloat(DurVal));
	}
	if (Op->HasField(TEXT("period")))
	{
		const float PeriodVal = static_cast<float>(Op->GetNumberField(TEXT("period")));
		if (FScalableFloat* S = NxGasPropPtr<FScalableFloat>(CDO, TEXT("Period")))
			S->Value = PeriodVal;
	}
	MarkGEApplied(Ctx);
}

static void HandleGE_SetTags(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* CDO = GEState(Ctx)->CDO;
	const FNexusArgs A(Op);
	const FString ContainerName = A.Str(TEXT("tagContainer"));
	if (ContainerName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_tags requires tagContainer"));
		return;
	}
	FString Mode = A.Str(TEXT("mode"));
	if (Mode.IsEmpty()) Mode = TEXT("set");

	static const TMap<FString, FString> ContainerMap = {
		{ TEXT("gameplayEffectTags"), TEXT("InheritableGameplayEffectTags")          },
		{ TEXT("grantedTags"),        TEXT("InheritableOwnedTagsContainer")          },
		{ TEXT("blockedAbilityTags"), TEXT("InheritableBlockedAbilityTagsContainer") },
	};
	const FString* PropName = ContainerMap.Find(ContainerName);
	if (!PropName)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("unknown tagContainer: %s"), *ContainerName));
		return;
	}
	FGameplayTagContainer* Container = GetGE_InheritedTagAddedMut(CDO, **PropName);
	if (!Container)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("property missing or unsupported on this UE version: %s"), **PropName));
		return;
	}
	FString TagError;
	if (!FNexusGasUtils::ApplyTagContainer(*Container, A.StrArr(TEXT("tags")), Mode, TagError))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TagError);
		return;
	}
	MarkGEApplied(Ctx);
}

static void HandleGE_ModifierOp(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UGameplayEffect* GECDO = GEState(Ctx)->GECDO;
	FString ModError;
	if (!FNexusGasUtils::ApplyGEModifierOp(GECDO, Ctx.Action, Op, ModError))
	{
		Ctx.Entry->SetStringField(TEXT("error"), ModError);
		return;
	}
	MarkGEApplied(Ctx);
}

bool FManageAssetGameplayEffectCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	FString LoadError;
	UBlueprint* BP = FNexusGasUtils::LoadGameplayEffectBlueprint(AssetPath, LoadError);
	if (!BP) { OutError = LoadError; return false; }
	if (!BP->GeneratedClass) { OutError = TEXT("Blueprint not compiled"); return false; }
	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	UGameplayEffect* GECDO = BP->GeneratedClass->GetDefaultObject<UGameplayEffect>();
	if (!CDO || !GECDO) { OutError = TEXT("Unable to get GameplayEffect CDO"); return false; }
	FGEActionState* State = new FGEActionState();
	State->BP = BP;
	State->CDO = CDO;
	State->GECDO = GECDO;
	OutTarget = State;
	return true;
}

void FManageAssetGameplayEffectCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	if (FGEActionState* State = static_cast<FGEActionState*>(Target))
	{
		State->OutTop = OutTop;
		OutTop->SetStringField(TEXT("path"), FNexusArgs(Args).Str(TEXT("assetPath")));
	}
}

void FManageAssetGameplayEffectCapability::FinalizeTarget(void* Target) const
{
	FGEActionState* State = static_cast<FGEActionState*>(Target);
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

void FManageAssetGameplayEffectCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_policy"),      &HandleGE_SetPolicy);
	OutHandlers.Add(TEXT("set_tags"),        &HandleGE_SetTags);
	OutHandlers.Add(TEXT("add_modifier"),    &HandleGE_ModifierOp);
	OutHandlers.Add(TEXT("remove_modifier"), &HandleGE_ModifierOp);
	OutHandlers.Add(TEXT("set_modifier"),    &HandleGE_ModifierOp);
}

REGISTER_MCP_CAPABILITY(FManageAssetGameplayEffectCapability)

#endif // WITH_GAS
