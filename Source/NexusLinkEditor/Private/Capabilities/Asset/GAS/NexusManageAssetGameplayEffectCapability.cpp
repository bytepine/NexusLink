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

void FNexusManageAssetGameplayEffectCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetGameplayEffect");
	Out.SearchAssetTypes = {TEXT("GameplayEffect")};
	Out.Description = TEXT("Batch edit GE CDO: SetPolicy/SetTags/add/remove/set modifier.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),         FNexusSchema::Enum(TEXT("Operation type"),
			{ TEXT("SetPolicy"), TEXT("SetTags"), TEXT("AddModifier"), TEXT("RemoveModifier"), TEXT("SetModifier") }))
		.Prop(TEXT("DurationPolicy"), FNexusSchema::Enum(TEXT("Duration policy (SetPolicy)"),
			{ TEXT("Instant"), TEXT("Infinite"), TEXT("HasDuration") }))
		.Prop(TEXT("Duration"),       FNexusSchema::Num(TEXT("Duration (SetPolicy)")))
		.Prop(TEXT("Period"),         FNexusSchema::Num(TEXT("Period (SetPolicy)")))
		.Prop(TEXT("TagContainer"),   FNexusSchema::Enum(TEXT("Tag container (SetTags)"),
			{ TEXT("GameplayEffectTags"), TEXT("GrantedTags"), TEXT("BlockedAbilityTags") }))
		.Prop(TEXT("Tags"),           FNexusSchema::StrArr(TEXT("Tag string array (SetTags)")))
		.Prop(TEXT("Mode"),           FNexusSchema::Enum(TEXT("set/add/remove"), { TEXT("Set"), TEXT("Add"), TEXT("Remove") }))
		.Prop(TEXT("Attribute"),      FNexusSchema::Str(TEXT("Attribute name (AddModifier)")))
		.Prop(TEXT("ModifierOp"),     FNexusSchema::Enum(TEXT("Modifier operation (AddModifier)"),
			{ TEXT("Add"), TEXT("Multiply"), TEXT("Divide"), TEXT("Override") }))
		.Prop(TEXT("Magnitude"),      FNexusSchema::Num(TEXT("Magnitude (AddModifier/SetModifier)")))
		.Prop(TEXT("Index"),          FNexusSchema::Num(TEXT("Modifier index (RemoveModifier/SetModifier)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("GameplayEffect Blueprint path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("effect"), TEXT("modifier"), TEXT("Duration"), TEXT("Tag") };
	Out.RelatedCapabilities = { TEXT("GetAssetGameplayEffect"), TEXT("SaveAsset"), TEXT("CreateAssetGameplayEffect") };
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
	if (Op->TryGetStringField(TEXT("DurationPolicy"), PolicyStr) && !PolicyStr.IsEmpty())
	{
		uint8 V = 0;
		if      (PolicyStr == TEXT("Instant"))     V = (uint8)EGameplayEffectDurationType::Instant;
		else if (PolicyStr == TEXT("Infinite"))    V = (uint8)EGameplayEffectDurationType::Infinite;
		else if (PolicyStr == TEXT("HasDuration")) V = (uint8)EGameplayEffectDurationType::HasDuration;
		else
		{
			Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("invalid DurationPolicy: %s"), *PolicyStr));
			return;
		}
		NxGasSetEnumByte(CDO, TEXT("DurationPolicy"), V);
	}
	if (Op->HasField(TEXT("Duration")))
	{
		const float DurVal = static_cast<float>(Op->GetNumberField(TEXT("Duration")));
		if (FGameplayEffectModifierMagnitude* M = NxGasPropPtr<FGameplayEffectModifierMagnitude>(CDO, TEXT("DurationMagnitude")))
			*M = FGameplayEffectModifierMagnitude(FScalableFloat(DurVal));
	}
	if (Op->HasField(TEXT("Period")))
	{
		const float PeriodVal = static_cast<float>(Op->GetNumberField(TEXT("Period")));
		if (FScalableFloat* S = NxGasPropPtr<FScalableFloat>(CDO, TEXT("Period")))
			S->Value = PeriodVal;
	}
	MarkGEApplied(Ctx);
}

static void HandleGE_SetTags(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* CDO = GEState(Ctx)->CDO;
	const FNexusArgs A(Op);
	const FString ContainerName = A.Str(TEXT("TagContainer"));
	if (ContainerName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetTags requires TagContainer"));
		return;
	}
	FString Mode = A.Str(TEXT("Mode"));
	if (Mode.IsEmpty()) Mode = TEXT("Set");

	static const TMap<FString, FString> ContainerMap = {
		{ TEXT("GameplayEffectTags"), TEXT("InheritableGameplayEffectTags")          },
		{ TEXT("GrantedTags"),        TEXT("InheritableOwnedTagsContainer")          },
		{ TEXT("BlockedAbilityTags"), TEXT("InheritableBlockedAbilityTagsContainer") },
	};
	const FString* PropName = ContainerMap.Find(ContainerName);
	if (!PropName)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("unknown TagContainer: %s"), *ContainerName));
		return;
	}
	FGameplayTagContainer* Container = GetGE_InheritedTagAddedMut(CDO, **PropName);
	if (!Container)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("property missing or unsupported on this UE version: %s"), **PropName));
		return;
	}
	FString TagError;
	if (!FNexusGasUtils::ApplyTagContainer(*Container, A.StrArr(TEXT("Tags")), Mode, TagError))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TagError);
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
		Ctx.Entry->SetStringField(TEXT("Error"), ModError);
		return;
	}
	MarkGEApplied(Ctx);
}

bool FNexusManageAssetGameplayEffectCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetGameplayEffectCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	if (FGEActionState* State = static_cast<FGEActionState*>(Target))
	{
		State->OutTop = OutTop;
		OutTop->SetStringField(TEXT("Path"), FNexusArgs(Args).Str(TEXT("AssetPath")));
	}
}

void FNexusManageAssetGameplayEffectCapability::FinalizeTarget(void* Target) const
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
		State->OutTop->SetNumberField(TEXT("AppliedOps"), State->Applied);
	}
	delete State;
}

void FNexusManageAssetGameplayEffectCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetPolicy"),      &HandleGE_SetPolicy);
	OutHandlers.Add(TEXT("SetTags"),        &HandleGE_SetTags);
	OutHandlers.Add(TEXT("AddModifier"),    &HandleGE_ModifierOp);
	OutHandlers.Add(TEXT("RemoveModifier"), &HandleGE_ModifierOp);
	OutHandlers.Add(TEXT("SetModifier"),    &HandleGE_ModifierOp);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetGameplayEffectCapability)

#endif // WITH_GAS
