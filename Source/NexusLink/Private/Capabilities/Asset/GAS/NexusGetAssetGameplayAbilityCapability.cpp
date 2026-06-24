// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusGetAssetGameplayAbilityCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusBlueprintGraphUtils.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"

// ── 枚举 → 字符串 ─────────────────────────────────────────────────────────────

static FString GA_InstancingPolicyToStr(uint8 V)
{
	switch ((EGameplayAbilityInstancingPolicy::Type)V)
	{
		case EGameplayAbilityInstancingPolicy::NonInstanced:          return TEXT("NonInstanced");
		case EGameplayAbilityInstancingPolicy::InstancedPerActor:     return TEXT("InstancedPerActor");
		case EGameplayAbilityInstancingPolicy::InstancedPerExecution: return TEXT("InstancedPerExecution");
		default: return TEXT("Unknown");
	}
}

static FString GA_NetExecPolicyToStr(uint8 V)
{
	switch ((EGameplayAbilityNetExecutionPolicy::Type)V)
	{
		case EGameplayAbilityNetExecutionPolicy::LocalPredicted:  return TEXT("LocalPredicted");
		case EGameplayAbilityNetExecutionPolicy::LocalOnly:       return TEXT("LocalOnly");
		case EGameplayAbilityNetExecutionPolicy::ServerInitiated: return TEXT("ServerInitiated");
		case EGameplayAbilityNetExecutionPolicy::ServerOnly:      return TEXT("ServerOnly");
		default: return TEXT("Unknown");
	}
}

// ── Capability 元数据 ─────────────────────────────────────────────────────────

void FGetAssetGameplayAbilityCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_gameplay_ability");
	Out.Description = TEXT("读 GA Blueprint。sections=metadata|tags|costs|graphOverview；只读。");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("ability"), TEXT("gameplay"), TEXT("ga"), TEXT("tag"), TEXT("policy") };
	Out.RelatedCapabilities = { TEXT("manage_asset_gameplay_ability"), TEXT("manage_asset_blueprint"), TEXT("create_asset_gameplay_ability") };
	Out.WhenToUse = TEXT("读 GA 策略/Tag/Cost；Graph 读取用 get_asset_blueprint");
}

TSharedPtr<FJsonObject> FGetAssetGameplayAbilityCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("GameplayAbility Blueprint 路径")))
		.Required({ TEXT("assetPath") })
		.Build();
}

TArray<FString> FGetAssetGameplayAbilityCapability::GetSectionNames() const
{
	return { TEXT("metadata"), TEXT("tags"), TEXT("costs"), TEXT("graphOverview") };
}

TArray<FString> FGetAssetGameplayAbilityCapability::GetDefaultSectionNames() const
{
	return { TEXT("metadata"), TEXT("tags"), TEXT("costs") };
}

bool FGetAssetGameplayAbilityCapability::PrepareEntry(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>&       OutEntry,
	void*&                         OutTargetOpaque,
	FString&                       OutError) const
{
	FString Path;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("assetPath"), Path) || Path.IsEmpty())
	{ OutError = TEXT("缺少 assetPath"); return false; }

	FString LoadError;
	UBlueprint* BP = FNexusGasUtils::LoadGameplayAbilityBlueprint(Path, LoadError);
	if (!BP) { OutError = LoadError; return false; }

	OutEntry->SetStringField(TEXT("assetPath"), Path);
	OutEntry->SetStringField(TEXT("name"),      BP->GetName());
	if (BP->ParentClass) OutEntry->SetStringField(TEXT("parentClass"), BP->ParentClass->GetName());

	OutTargetOpaque = static_cast<void*>(BP);
	return true;
}

void FGetAssetGameplayAbilityCapability::ExecuteSection(
	const FString&                 SectionName,
	const TSharedPtr<FJsonObject>& Args,
	void*                          TargetOpaque,
	TSharedPtr<FJsonObject>&       InOutDetail,
	FString&                       OutError) const
{
	UBlueprint* BP = static_cast<UBlueprint*>(TargetOpaque);
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("无效的 GA Blueprint 目标"); return; }

	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	if (!CDO) { OutError = TEXT("无法获取 GameplayAbility CDO"); return; }

	if (SectionName == TEXT("metadata"))
	{
		InOutDetail->SetStringField(TEXT("instancingPolicy"),   GA_InstancingPolicyToStr(NxGasGetEnumByte(CDO, TEXT("InstancingPolicy"))));
		InOutDetail->SetStringField(TEXT("netExecutionPolicy"), GA_NetExecPolicyToStr(NxGasGetEnumByte(CDO, TEXT("NetExecutionPolicy"))));
		// bReplicateInputDirectly 是 bitfield，FBoolProperty 专用读法
		FBoolProperty* BoolProp = FindFProperty<FBoolProperty>(CDO->GetClass(), TEXT("bReplicateInputDirectly"));
		if (BoolProp)
			InOutDetail->SetBoolField(TEXT("bReplicateInputDirectly"), BoolProp->GetPropertyValue_InContainer(CDO));
	}
	else if (SectionName == TEXT("tags"))
	{
		auto TagArr = [&](const TCHAR* Prop) -> TArray<TSharedPtr<FJsonValue>>
		{
			const FGameplayTagContainer* Tags = NxGasPropPtr<FGameplayTagContainer>(CDO, Prop);
			return Tags ? FNexusGasUtils::SerializeTagContainer(*Tags) : TArray<TSharedPtr<FJsonValue>>{};
		};
		InOutDetail->SetArrayField(TEXT("abilityTags"),           TagArr(TEXT("AbilityTags")));
		InOutDetail->SetArrayField(TEXT("activationOwnedTags"),   TagArr(TEXT("ActivationOwnedTags")));
		InOutDetail->SetArrayField(TEXT("activationRequiredTags"),TagArr(TEXT("ActivationRequiredTags")));
		InOutDetail->SetArrayField(TEXT("activationBlockedTags"), TagArr(TEXT("ActivationBlockedTags")));
		InOutDetail->SetArrayField(TEXT("cancelAbilitiesWithTag"),TagArr(TEXT("CancelAbilitiesWithTag")));
		InOutDetail->SetArrayField(TEXT("blockAbilitiesWithTag"), TagArr(TEXT("BlockAbilitiesWithTag")));
	}
	else if (SectionName == TEXT("costs"))
	{
		UClass* CostClass     = NxGasGetClassProp(CDO, TEXT("CostGameplayEffectClass"));
		UClass* CooldownClass = NxGasGetClassProp(CDO, TEXT("CooldownGameplayEffectClass"));
		if (CostClass)     InOutDetail->SetStringField(TEXT("costGE"),     CostClass->GetPathName());
		if (CooldownClass) InOutDetail->SetStringField(TEXT("cooldownGE"), CooldownClass->GetPathName());
	}
	else if (SectionName == TEXT("graphOverview"))
	{
		TArray<UEdGraph*> AllGraphs;
		FNexusBlueprintGraphUtils::CollectAllGraphs(BP, AllGraphs);
		TArray<TSharedPtr<FJsonValue>> Graphs;
		for (const UEdGraph* G : AllGraphs)
		{
			TSharedPtr<FJsonObject> GObj = MakeShared<FJsonObject>();
			GObj->SetStringField(TEXT("name"), G->GetName());
			GObj->SetStringField(TEXT("type"), FNexusBlueprintGraphUtils::GetBPGraphType(G));
			Graphs.Add(MakeShared<FJsonValueObject>(GObj));
		}
		InOutDetail->SetArrayField(TEXT("graphs"), Graphs);
	}
}

REGISTER_MCP_CAPABILITY(FGetAssetGameplayAbilityCapability)

#endif // WITH_GAS
