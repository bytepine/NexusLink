// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusManageAssetGameplayAbilityCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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

FCapabilityResult FManageAssetGameplayAbilityCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));

		FString LoadError;
		UBlueprint* BP = FNexusGasUtils::LoadGameplayAbilityBlueprint(AssetPath, LoadError);
		if (!BP) { OutError = LoadError; return; }
		if (!BP->GeneratedClass) { OutError = TEXT("Blueprint not compiled; cannot get CDO"); return; }

		UObject* CDO = BP->GeneratedClass->GetDefaultObject();
		if (!CDO) { OutError = TEXT("Unable to get GameplayAbility CDO"); return; }

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0) { OutError = TEXT("Missing or empty operations"); return; }

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
		const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
		if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
		const TSharedPtr<FJsonObject>& OpArgs = *OpObjPtr;

		FString Action;
		if (!OpArgs->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{ OutError = TEXT("Each operations[] item requires action"); return; }

		if (Action == TEXT("set_tags"))
		{
			FString ContainerName, Mode;
			if (!OpArgs->TryGetStringField(TEXT("tagContainer"), ContainerName) || ContainerName.IsEmpty())
			{ OutError = TEXT("set_tags requires tagContainer"); return; }
			if (!OpArgs->TryGetStringField(TEXT("mode"), Mode) || Mode.IsEmpty()) Mode = TEXT("set");

			TArray<FString> Tags;
			const TArray<TSharedPtr<FJsonValue>>* TagsArr = nullptr;
			if (OpArgs->TryGetArrayField(TEXT("tags"), TagsArr) && TagsArr)
			{
				for (const TSharedPtr<FJsonValue>& V : *TagsArr)
				{ FString S; if (V.IsValid() && V->TryGetString(S)) Tags.Add(S); }
			}

			static const TMap<FString, FString> ContainerPropMap = {
				{ TEXT("abilityTags"),            TEXT("AbilityTags")           },
				{ TEXT("activationOwnedTags"),    TEXT("ActivationOwnedTags")   },
				{ TEXT("activationRequiredTags"), TEXT("ActivationRequiredTags")},
				{ TEXT("activationBlockedTags"),  TEXT("ActivationBlockedTags") },
				{ TEXT("cancelAbilitiesWithTag"), TEXT("CancelAbilitiesWithTag")},
				{ TEXT("blockAbilitiesWithTag"),  TEXT("BlockAbilitiesWithTag") },
			};
			const FString* PropName = ContainerPropMap.Find(ContainerName);
			if (!PropName) { OutError = FString::Printf(TEXT("Unknown tagContainer: %s"), *ContainerName); return; }

			FGameplayTagContainer* Container = NxGasPropPtr<FGameplayTagContainer>(CDO, **PropName);
			if (!Container) { OutError = FString::Printf(TEXT("Unable to access property: %s"), **PropName); return; }

			FString TagError;
			if (!FNexusGasUtils::ApplyTagContainer(*Container, Tags, Mode, TagError))
			{ OutError = TagError; return; }
		}
		else if (Action == TEXT("set_policy"))
		{
			FString InstPolicyStr, NetPolicyStr;
			if (OpArgs->TryGetStringField(TEXT("instancingPolicy"), InstPolicyStr) && !InstPolicyStr.IsEmpty())
			{
				uint8 V = 0;
				if      (InstPolicyStr == TEXT("NonInstanced"))          V = (uint8)EGameplayAbilityInstancingPolicy::NonInstanced;
				else if (InstPolicyStr == TEXT("InstancedPerActor"))     V = (uint8)EGameplayAbilityInstancingPolicy::InstancedPerActor;
				else if (InstPolicyStr == TEXT("InstancedPerExecution")) V = (uint8)EGameplayAbilityInstancingPolicy::InstancedPerExecution;
				else { OutError = FString::Printf(TEXT("Invalid instancingPolicy: %s"), *InstPolicyStr); return; }
				NxGasSetEnumByte(CDO, TEXT("InstancingPolicy"), V);
			}
			if (OpArgs->TryGetStringField(TEXT("netExecutionPolicy"), NetPolicyStr) && !NetPolicyStr.IsEmpty())
			{
				uint8 V = 0;
				if      (NetPolicyStr == TEXT("LocalPredicted"))  V = (uint8)EGameplayAbilityNetExecutionPolicy::LocalPredicted;
				else if (NetPolicyStr == TEXT("LocalOnly"))       V = (uint8)EGameplayAbilityNetExecutionPolicy::LocalOnly;
				else if (NetPolicyStr == TEXT("ServerInitiated")) V = (uint8)EGameplayAbilityNetExecutionPolicy::ServerInitiated;
				else if (NetPolicyStr == TEXT("ServerOnly"))      V = (uint8)EGameplayAbilityNetExecutionPolicy::ServerOnly;
				else { OutError = FString::Printf(TEXT("Invalid netExecutionPolicy: %s"), *NetPolicyStr); return; }
				NxGasSetEnumByte(CDO, TEXT("NetExecutionPolicy"), V);
			}
		}
		else if (Action == TEXT("set_cost_cooldown"))
		{
			FString CostPath, CooldownPath;
			if (OpArgs->TryGetStringField(TEXT("costGE"), CostPath))
			{
				if (CostPath.IsEmpty())
				{
					NxGasSetClassProp(CDO, TEXT("CostGameplayEffectClass"), nullptr);
				}
				else
				{
					UBlueprint* CostBP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(CostPath);
					if (!CostBP || !CostBP->GeneratedClass || !CostBP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass()))
					{ OutError = FString::Printf(TEXT("costGE is not a valid GameplayEffect Blueprint: %s"), *CostPath); return; }
					NxGasSetClassProp(CDO, TEXT("CostGameplayEffectClass"), CostBP->GeneratedClass);
				}
			}
			if (OpArgs->TryGetStringField(TEXT("cooldownGE"), CooldownPath))
			{
				if (CooldownPath.IsEmpty())
				{
					NxGasSetClassProp(CDO, TEXT("CooldownGameplayEffectClass"), nullptr);
				}
				else
				{
					UBlueprint* CDBP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(CooldownPath);
					if (!CDBP || !CDBP->GeneratedClass || !CDBP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass()))
					{ OutError = FString::Printf(TEXT("cooldownGE is not a valid GameplayEffect Blueprint: %s"), *CooldownPath); return; }
					NxGasSetClassProp(CDO, TEXT("CooldownGameplayEffectClass"), CDBP->GeneratedClass);
				}
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("Unknown action: %s (use manage_asset_blueprint for graph edits)"), *Action);
			return;
		}
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);

		OutTop->SetStringField(TEXT("path"), AssetPath);
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetGameplayAbilityCapability)

#endif // WITH_GAS
