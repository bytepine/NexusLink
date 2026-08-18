// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusManageAssetGameplayAbilityCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusArgs.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "NexusMcpTool.h"

// ── Capability 定义 ──────────────────────────────────────────────────────────

void FManageAssetGameplayAbilityCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_gameplay_ability");
	Out.SearchAssetTypes = {TEXT("GameplayAbility")};
	Out.Description = TEXT("Batch edit GA CDO: tags/policy/cost_cooldown. Graph via manage_asset_blueprint.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),        FNexusSchema::Enum(TEXT("Operation type"), { TEXT("set_tags"), TEXT("set_policy"), TEXT("set_cost_cooldown") }))
		.Prop(TEXT("tagContainer"),  FNexusSchema::Enum(TEXT("Tag container name"),
			{ TEXT("abilityTags"), TEXT("activationOwnedTags"), TEXT("activationRequiredTags"),
			  TEXT("activationBlockedTags"), TEXT("cancelAbilitiesWithTag"), TEXT("blockAbilitiesWithTag") }))
		.Prop(TEXT("tags"),          FNexusSchema::StrArr(TEXT("Tag string array")))
		.Prop(TEXT("mode"),          FNexusSchema::Enum(TEXT("set/add/remove"), { TEXT("set"), TEXT("add"), TEXT("remove") }))
		.Prop(TEXT("instancingPolicy"),   FNexusSchema::Enum(TEXT("Instancing policy"),
			{ TEXT("NonInstanced"), TEXT("InstancedPerActor"), TEXT("InstancedPerExecution") }))
		.Prop(TEXT("netExecutionPolicy"), FNexusSchema::Enum(TEXT("Net execution policy"),
			{ TEXT("LocalPredicted"), TEXT("LocalOnly"), TEXT("ServerInitiated"), TEXT("ServerOnly") }))
		.Prop(TEXT("costGE"),        FNexusSchema::Str(TEXT("Cost GE asset path (empty string clears)")))
		.Prop(TEXT("cooldownGE"),    FNexusSchema::Str(TEXT("Cooldown GE asset path (empty string clears)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("GameplayAbility Blueprint path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("ability"), TEXT("gameplay"), TEXT("ga"), TEXT("tag"), TEXT("policy"), TEXT("cost") };
	Out.RelatedCapabilities = { TEXT("get_asset_gameplay_ability"), TEXT("save_asset"), TEXT("manage_asset_blueprint") };
	Out.WhenToUse = TEXT("CDO semantic fields; AbilityTask/logic graph via manage_asset_blueprint");
}

struct FGAActionState
{
	UBlueprint* BP = nullptr;
	UObject* CDO = nullptr;
	bool bDirty = false;
};

static FGAActionState* GAState(FNexusActionContext& Ctx)
{
	return static_cast<FGAActionState*>(Ctx.Target);
}

static void MarkGADirty(FNexusActionContext& Ctx)
{
	if (FGAActionState* S = GAState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleGA_SetTags(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* CDO = GAState(Ctx)->CDO;
	const FNexusArgs A(Op);
	const FString ContainerName = A.Str(TEXT("tagContainer"));
	if (ContainerName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_tags requires tagContainer"));
		return;
	}
	FString Mode = A.Str(TEXT("mode"));
	if (Mode.IsEmpty()) Mode = TEXT("set");

	static const TMap<FString, FString> ContainerPropMap = {
		{ TEXT("abilityTags"),            TEXT("AbilityTags")           },
		{ TEXT("activationOwnedTags"),    TEXT("ActivationOwnedTags")   },
		{ TEXT("activationRequiredTags"), TEXT("ActivationRequiredTags")},
		{ TEXT("activationBlockedTags"),  TEXT("ActivationBlockedTags") },
		{ TEXT("cancelAbilitiesWithTag"), TEXT("CancelAbilitiesWithTag")},
		{ TEXT("blockAbilitiesWithTag"),  TEXT("BlockAbilitiesWithTag") },
	};
	const FString* PropName = ContainerPropMap.Find(ContainerName);
	if (!PropName)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown tagContainer: %s"), *ContainerName));
		return;
	}
	FGameplayTagContainer* Container = NxGasPropPtr<FGameplayTagContainer>(CDO, **PropName);
	if (!Container)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unable to access property: %s"), **PropName));
		return;
	}
	FString TagError;
	if (!FNexusGasUtils::ApplyTagContainer(*Container, A.StrArr(TEXT("tags")), Mode, TagError))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TagError);
		return;
	}
	MarkGADirty(Ctx);
}

static void HandleGA_SetPolicy(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* CDO = GAState(Ctx)->CDO;
	FString InstPolicyStr, NetPolicyStr;
	if (Op->TryGetStringField(TEXT("instancingPolicy"), InstPolicyStr) && !InstPolicyStr.IsEmpty())
	{
		uint8 V = 0;
		if      (InstPolicyStr == TEXT("NonInstanced"))          V = (uint8)EGameplayAbilityInstancingPolicy::NonInstanced;
		else if (InstPolicyStr == TEXT("InstancedPerActor"))     V = (uint8)EGameplayAbilityInstancingPolicy::InstancedPerActor;
		else if (InstPolicyStr == TEXT("InstancedPerExecution")) V = (uint8)EGameplayAbilityInstancingPolicy::InstancedPerExecution;
		else
		{
			Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid instancingPolicy: %s"), *InstPolicyStr));
			return;
		}
		NxGasSetEnumByte(CDO, TEXT("InstancingPolicy"), V);
		MarkGADirty(Ctx);
	}
	if (Op->TryGetStringField(TEXT("netExecutionPolicy"), NetPolicyStr) && !NetPolicyStr.IsEmpty())
	{
		uint8 V = 0;
		if      (NetPolicyStr == TEXT("LocalPredicted"))  V = (uint8)EGameplayAbilityNetExecutionPolicy::LocalPredicted;
		else if (NetPolicyStr == TEXT("LocalOnly"))       V = (uint8)EGameplayAbilityNetExecutionPolicy::LocalOnly;
		else if (NetPolicyStr == TEXT("ServerInitiated")) V = (uint8)EGameplayAbilityNetExecutionPolicy::ServerInitiated;
		else if (NetPolicyStr == TEXT("ServerOnly"))      V = (uint8)EGameplayAbilityNetExecutionPolicy::ServerOnly;
		else
		{
			Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid netExecutionPolicy: %s"), *NetPolicyStr));
			return;
		}
		NxGasSetEnumByte(CDO, TEXT("NetExecutionPolicy"), V);
		MarkGADirty(Ctx);
	}
}

static void HandleGA_SetCostCooldown(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* CDO = GAState(Ctx)->CDO;
	FString CostPath, CooldownPath;
	if (Op->TryGetStringField(TEXT("costGE"), CostPath))
	{
		if (CostPath.IsEmpty())
		{
			NxGasSetClassProp(CDO, TEXT("CostGameplayEffectClass"), nullptr);
		}
		else
		{
			UBlueprint* CostBP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(CostPath);
			if (!CostBP || !CostBP->GeneratedClass || !CostBP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass()))
			{
				Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("costGE is not a valid GameplayEffect Blueprint: %s"), *CostPath));
				return;
			}
			NxGasSetClassProp(CDO, TEXT("CostGameplayEffectClass"), CostBP->GeneratedClass);
		}
		MarkGADirty(Ctx);
	}
	if (Op->TryGetStringField(TEXT("cooldownGE"), CooldownPath))
	{
		if (CooldownPath.IsEmpty())
		{
			NxGasSetClassProp(CDO, TEXT("CooldownGameplayEffectClass"), nullptr);
		}
		else
		{
			UBlueprint* CDBP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(CooldownPath);
			if (!CDBP || !CDBP->GeneratedClass || !CDBP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass()))
			{
				Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("cooldownGE is not a valid GameplayEffect Blueprint: %s"), *CooldownPath));
				return;
			}
			NxGasSetClassProp(CDO, TEXT("CooldownGameplayEffectClass"), CDBP->GeneratedClass);
		}
		MarkGADirty(Ctx);
	}
}

bool FManageAssetGameplayAbilityCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	FString LoadError;
	UBlueprint* BP = FNexusGasUtils::LoadGameplayAbilityBlueprint(AssetPath, LoadError);
	if (!BP) { OutError = LoadError; return false; }
	if (!BP->GeneratedClass) { OutError = TEXT("Blueprint not compiled; cannot get CDO"); return false; }
	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	if (!CDO) { OutError = TEXT("Unable to get GameplayAbility CDO"); return false; }
	FGAActionState* State = new FGAActionState();
	State->BP = BP;
	State->CDO = CDO;
	OutTarget = State;
	return true;
}

void FManageAssetGameplayAbilityCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	(void)Target;
	OutTop->SetStringField(TEXT("path"), FNexusArgs(Args).Str(TEXT("assetPath")));
}

void FManageAssetGameplayAbilityCapability::FinalizeTarget(void* Target) const
{
	FGAActionState* State = static_cast<FGAActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->BP)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(State->BP);
		FKismetEditorUtilities::CompileBlueprint(State->BP);
	}
	delete State;
}

void FManageAssetGameplayAbilityCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_tags"),          &HandleGA_SetTags);
	OutHandlers.Add(TEXT("set_policy"),        &HandleGA_SetPolicy);
	OutHandlers.Add(TEXT("set_cost_cooldown"), &HandleGA_SetCostCooldown);
}

REGISTER_MCP_CAPABILITY(FManageAssetGameplayAbilityCapability)

#endif // WITH_GAS
