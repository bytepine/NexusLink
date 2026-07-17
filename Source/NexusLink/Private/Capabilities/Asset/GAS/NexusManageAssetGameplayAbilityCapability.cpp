// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusManageAssetGameplayAbilityCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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
	Out.Description = TEXT("批量修改 GA CDO：operations[].action=set_tags/set_policy/set_cost_cooldown。Graph 编辑用 manage_asset_blueprint。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),        FNexusSchema::Enum(TEXT("操作类型"), { TEXT("set_tags"), TEXT("set_policy"), TEXT("set_cost_cooldown") }))
		.Prop(TEXT("tagContainer"),  FNexusSchema::Enum(TEXT("Tag 容器名"),
			{ TEXT("abilityTags"), TEXT("activationOwnedTags"), TEXT("activationRequiredTags"),
			  TEXT("activationBlockedTags"), TEXT("cancelAbilitiesWithTag"), TEXT("blockAbilitiesWithTag") }))
		.Prop(TEXT("tags"),          FNexusSchema::StrArr(TEXT("Tag 字符串数组")))
		.Prop(TEXT("mode"),          FNexusSchema::Enum(TEXT("set/add/remove"), { TEXT("set"), TEXT("add"), TEXT("remove") }))
		.Prop(TEXT("instancingPolicy"),   FNexusSchema::Enum(TEXT("实例化策略"),
			{ TEXT("NonInstanced"), TEXT("InstancedPerActor"), TEXT("InstancedPerExecution") }))
		.Prop(TEXT("netExecutionPolicy"), FNexusSchema::Enum(TEXT("网络执行策略"),
			{ TEXT("LocalPredicted"), TEXT("LocalOnly"), TEXT("ServerInitiated"), TEXT("ServerOnly") }))
		.Prop(TEXT("costGE"),        FNexusSchema::Str(TEXT("Cost GE 资产路径（传空字符串清空）")))
		.Prop(TEXT("cooldownGE"),    FNexusSchema::Str(TEXT("Cooldown GE 资产路径（传空字符串清空）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("GameplayAbility Blueprint 路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("ability"), TEXT("gameplay"), TEXT("ga"), TEXT("tag"), TEXT("policy"), TEXT("cost") };
	Out.RelatedCapabilities = { TEXT("get_asset_gameplay_ability"), TEXT("save_asset"), TEXT("manage_asset_blueprint") };
	Out.WhenToUse = TEXT("CDO 语义字段；逻辑图用 manage_asset_blueprint");
}

FCapabilityResult FManageAssetGameplayAbilityCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{ OutError = TEXT("assetPath 为必填项"); return; }

		FString LoadError;
		UBlueprint* BP = FNexusGasUtils::LoadGameplayAbilityBlueprint(AssetPath, LoadError);
		if (!BP) { OutError = LoadError; return; }
		if (!BP->GeneratedClass) { OutError = TEXT("Blueprint 未编译，无法获取 CDO"); return; }

		UObject* CDO = BP->GeneratedClass->GetDefaultObject();
		if (!CDO) { OutError = TEXT("无法获取 GameplayAbility CDO"); return; }

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0) { OutError = TEXT("缺少 operations 或为空"); return; }

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
		const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
		if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
		const TSharedPtr<FJsonObject>& OpArgs = *OpObjPtr;

		FString Action;
		if (!OpArgs->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{ OutError = TEXT("operations[] 每项需要 action"); return; }

		if (Action == TEXT("set_tags"))
		{
			FString ContainerName, Mode;
			if (!OpArgs->TryGetStringField(TEXT("tagContainer"), ContainerName) || ContainerName.IsEmpty())
			{ OutError = TEXT("set_tags 需要 tagContainer"); return; }
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
			if (!PropName) { OutError = FString::Printf(TEXT("未知 tagContainer: %s"), *ContainerName); return; }

			FGameplayTagContainer* Container = NxGasPropPtr<FGameplayTagContainer>(CDO, **PropName);
			if (!Container) { OutError = FString::Printf(TEXT("无法访问属性: %s"), **PropName); return; }

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
				else { OutError = FString::Printf(TEXT("无效的 instancingPolicy: %s"), *InstPolicyStr); return; }
				NxGasSetEnumByte(CDO, TEXT("InstancingPolicy"), V);
			}
			if (OpArgs->TryGetStringField(TEXT("netExecutionPolicy"), NetPolicyStr) && !NetPolicyStr.IsEmpty())
			{
				uint8 V = 0;
				if      (NetPolicyStr == TEXT("LocalPredicted"))  V = (uint8)EGameplayAbilityNetExecutionPolicy::LocalPredicted;
				else if (NetPolicyStr == TEXT("LocalOnly"))       V = (uint8)EGameplayAbilityNetExecutionPolicy::LocalOnly;
				else if (NetPolicyStr == TEXT("ServerInitiated")) V = (uint8)EGameplayAbilityNetExecutionPolicy::ServerInitiated;
				else if (NetPolicyStr == TEXT("ServerOnly"))      V = (uint8)EGameplayAbilityNetExecutionPolicy::ServerOnly;
				else { OutError = FString::Printf(TEXT("无效的 netExecutionPolicy: %s"), *NetPolicyStr); return; }
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
					{ OutError = FString::Printf(TEXT("costGE 不是有效的 GameplayEffect Blueprint: %s"), *CostPath); return; }
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
					{ OutError = FString::Printf(TEXT("cooldownGE 不是有效的 GameplayEffect Blueprint: %s"), *CooldownPath); return; }
					NxGasSetClassProp(CDO, TEXT("CooldownGameplayEffectClass"), CDBP->GeneratedClass);
				}
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("未知 action: %s （Graph 编辑用 manage_asset_blueprint）"), *Action);
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
