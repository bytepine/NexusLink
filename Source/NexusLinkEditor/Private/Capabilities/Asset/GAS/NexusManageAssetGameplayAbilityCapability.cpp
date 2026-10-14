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

void FNexusManageAssetGameplayAbilityCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetGameplayAbility");
	Out.SearchAssetTypes = {TEXT("GameplayAbility")};
	Out.Description = TEXT("Batch edit GA CDO: tags/policy/cost_cooldown. Graph via ManageAssetBlueprint.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),        FNexusSchema::Enum(TEXT("Operation type"), { TEXT("SetTags"), TEXT("SetPolicy"), TEXT("SetCostCooldown") }))
		.Prop(TEXT("TagContainer"),  FNexusSchema::Enum(TEXT("Tag container name"),
			{ TEXT("AbilityTags"), TEXT("ActivationOwnedTags"), TEXT("ActivationRequiredTags"),
			  TEXT("ActivationBlockedTags"), TEXT("CancelAbilitiesWithTag"), TEXT("BlockAbilitiesWithTag") }))
		.Prop(TEXT("Tags"),          FNexusSchema::StrArr(TEXT("Tag string array")))
		.Prop(TEXT("Mode"),          FNexusSchema::Enum(TEXT("set/add/remove"), { TEXT("Set"), TEXT("Add"), TEXT("Remove") }))
		.Prop(TEXT("InstancingPolicy"),   FNexusSchema::Enum(TEXT("Instancing policy"),
			{ TEXT("NonInstanced"), TEXT("InstancedPerActor"), TEXT("InstancedPerExecution") }))
		.Prop(TEXT("NetExecutionPolicy"), FNexusSchema::Enum(TEXT("Net execution policy"),
			{ TEXT("LocalPredicted"), TEXT("LocalOnly"), TEXT("ServerInitiated"), TEXT("ServerOnly") }))
		.Prop(TEXT("CostGE"),        FNexusSchema::Str(TEXT("Cost GE asset path (empty string clears)")))
		.Prop(TEXT("CooldownGE"),    FNexusSchema::Str(TEXT("Cooldown GE asset path (empty string clears)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("GameplayAbility Blueprint path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	// 补复数 "Tags"：ScoreToken 的 prefix/substring 匹配都要求关键词长度 >= 查询 token 长度，
	// 单数 "Tag" 匹配不到查询 token "Tags"（如 Query="gameplay tags"）
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("ability"), TEXT("gameplay"), TEXT("ga"), TEXT("Tag"), TEXT("Tags"), TEXT("policy"), TEXT("cost") };
	Out.RelatedCapabilities = { TEXT("GetAssetGameplayAbility"), TEXT("SaveAsset"), TEXT("ManageAssetBlueprint") };
	Out.WhenToUse = TEXT("CDO semantic fields; AbilityTask/logic graph via ManageAssetBlueprint");
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
	const FString ContainerName = A.Str(TEXT("TagContainer"));
	if (ContainerName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetTags requires TagContainer"));
		return;
	}
	FString Mode = A.Str(TEXT("Mode"));
	if (Mode.IsEmpty()) Mode = TEXT("Set");

	static const TMap<FString, FString> ContainerPropMap = {
		{ TEXT("AbilityTags"),            TEXT("AbilityTags")           },
		{ TEXT("ActivationOwnedTags"),    TEXT("ActivationOwnedTags")   },
		{ TEXT("ActivationRequiredTags"), TEXT("ActivationRequiredTags")},
		{ TEXT("ActivationBlockedTags"),  TEXT("ActivationBlockedTags") },
		{ TEXT("CancelAbilitiesWithTag"), TEXT("CancelAbilitiesWithTag")},
		{ TEXT("BlockAbilitiesWithTag"),  TEXT("BlockAbilitiesWithTag") },
	};
	const FString* PropName = ContainerPropMap.Find(ContainerName);
	if (!PropName)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unknown TagContainer: %s"), *ContainerName));
		return;
	}
	FGameplayTagContainer* Container = NxGasPropPtr<FGameplayTagContainer>(CDO, **PropName);
	if (!Container)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unable to access property: %s"), **PropName));
		return;
	}
	FString TagError;
	if (!FNexusGasUtils::ApplyTagContainer(*Container, A.StrArr(TEXT("Tags")), Mode, TagError))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TagError);
		return;
	}
	MarkGADirty(Ctx);
}

static void HandleGA_SetPolicy(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* CDO = GAState(Ctx)->CDO;
	FString InstPolicyStr, NetPolicyStr;
	if (Op->TryGetStringField(TEXT("InstancingPolicy"), InstPolicyStr) && !InstPolicyStr.IsEmpty())
	{
		uint8 V = 0;
		bool bParsed = true;
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if      (InstPolicyStr == TEXT("NonInstanced"))          V = (uint8)EGameplayAbilityInstancingPolicy::NonInstanced;
		else if (InstPolicyStr == TEXT("InstancedPerActor"))     V = (uint8)EGameplayAbilityInstancingPolicy::InstancedPerActor;
		else if (InstPolicyStr == TEXT("InstancedPerExecution")) V = (uint8)EGameplayAbilityInstancingPolicy::InstancedPerExecution;
		else bParsed = false;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		if (!bParsed)
		{
			Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Invalid InstancingPolicy: %s"), *InstPolicyStr));
			return;
		}
		NxGasSetEnumByte(CDO, TEXT("InstancingPolicy"), V);
		MarkGADirty(Ctx);
	}
	if (Op->TryGetStringField(TEXT("NetExecutionPolicy"), NetPolicyStr) && !NetPolicyStr.IsEmpty())
	{
		uint8 V = 0;
		if      (NetPolicyStr == TEXT("LocalPredicted"))  V = (uint8)EGameplayAbilityNetExecutionPolicy::LocalPredicted;
		else if (NetPolicyStr == TEXT("LocalOnly"))       V = (uint8)EGameplayAbilityNetExecutionPolicy::LocalOnly;
		else if (NetPolicyStr == TEXT("ServerInitiated")) V = (uint8)EGameplayAbilityNetExecutionPolicy::ServerInitiated;
		else if (NetPolicyStr == TEXT("ServerOnly"))      V = (uint8)EGameplayAbilityNetExecutionPolicy::ServerOnly;
		else
		{
			Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Invalid NetExecutionPolicy: %s"), *NetPolicyStr));
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
	if (Op->TryGetStringField(TEXT("CostGE"), CostPath))
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
				Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("CostGE is not a valid GameplayEffect Blueprint: %s"), *CostPath));
				return;
			}
			NxGasSetClassProp(CDO, TEXT("CostGameplayEffectClass"), CostBP->GeneratedClass);
		}
		MarkGADirty(Ctx);
	}
	if (Op->TryGetStringField(TEXT("CooldownGE"), CooldownPath))
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
				Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("CooldownGE is not a valid GameplayEffect Blueprint: %s"), *CooldownPath));
				return;
			}
			NxGasSetClassProp(CDO, TEXT("CooldownGameplayEffectClass"), CDBP->GeneratedClass);
		}
		MarkGADirty(Ctx);
	}
}

bool FNexusManageAssetGameplayAbilityCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetGameplayAbilityCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	(void)Target;
	OutTop->SetStringField(TEXT("Path"), FNexusArgs(Args).Str(TEXT("AssetPath")));
}

void FNexusManageAssetGameplayAbilityCapability::FinalizeTarget(void* Target) const
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

void FNexusManageAssetGameplayAbilityCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetTags"),          &HandleGA_SetTags);
	OutHandlers.Add(TEXT("SetPolicy"),        &HandleGA_SetPolicy);
	OutHandlers.Add(TEXT("SetCostCooldown"), &HandleGA_SetCostCooldown);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetGameplayAbilityCapability)

#endif // WITH_GAS
