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
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	switch ((EGameplayAbilityInstancingPolicy::Type)V)
	{
		case EGameplayAbilityInstancingPolicy::NonInstanced:          return TEXT("NonInstanced");
		case EGameplayAbilityInstancingPolicy::InstancedPerActor:     return TEXT("InstancedPerActor");
		case EGameplayAbilityInstancingPolicy::InstancedPerExecution: return TEXT("InstancedPerExecution");
		default: return TEXT("Unknown");
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
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

void FNexusGetAssetGameplayAbilityCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetGameplayAbility");
	Out.SearchAssetTypes = {TEXT("GameplayAbility")};
	Out.Description = TEXT("Read GA Blueprint. Sections=metadata|tags|costs|graphOverview; read-only.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("ability"), TEXT("gameplay"), TEXT("ga"), TEXT("Tag"), TEXT("policy") };
	Out.RelatedCapabilities = { TEXT("ManageAssetGameplayAbility"), TEXT("ManageAssetBlueprint"), TEXT("CreateAssetGameplayAbility") };
	Out.WhenToUse = TEXT("Read GA policy/Tag/Cost; graph reads via GetAssetBlueprint");
}

TSharedPtr<FJsonObject> FNexusGetAssetGameplayAbilityCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("GameplayAbility Blueprint path")))
		.Required({ TEXT("AssetPath") })
		.Build();
}

TArray<FString> FNexusGetAssetGameplayAbilityCapability::GetSectionNames() const
{
	return { TEXT("metadata"), TEXT("Tags"), TEXT("costs"), TEXT("graphOverview") };
}

TArray<FString> FNexusGetAssetGameplayAbilityCapability::GetDefaultSectionNames() const
{
	return { TEXT("metadata"), TEXT("Tags"), TEXT("costs") };
}

bool FNexusGetAssetGameplayAbilityCapability::PrepareEntry(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>&       OutEntry,
	void*&                         OutTargetOpaque,
	FString&                       OutError) const
{
	FString Path;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("AssetPath"), Path) || Path.IsEmpty())
	{ OutError = TEXT("Missing AssetPath"); return false; }

	FString LoadError;
	UBlueprint* BP = FNexusGasUtils::LoadGameplayAbilityBlueprint(Path, LoadError);
	if (!BP) { OutError = LoadError; return false; }

	OutEntry->SetStringField(TEXT("Path"), Path);
	OutEntry->SetStringField(TEXT("name"),      BP->GetName());
	if (BP->ParentClass) OutEntry->SetStringField(TEXT("ParentClass"), BP->ParentClass->GetName());

	OutTargetOpaque = static_cast<void*>(BP);
	return true;
}

void FNexusGetAssetGameplayAbilityCapability::ExecuteSection(
	const FString&                 SectionName,
	const TSharedPtr<FJsonObject>& Args,
	void*                          TargetOpaque,
	TSharedPtr<FJsonObject>&       InOutDetail,
	FString&                       OutError) const
{
	UBlueprint* BP = static_cast<UBlueprint*>(TargetOpaque);
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Invalid GA Blueprint target"); return; }

	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	if (!CDO) { OutError = TEXT("Unable to get GameplayAbility CDO"); return; }

	if (SectionName == TEXT("metadata"))
	{
		InOutDetail->SetStringField(TEXT("InstancingPolicy"),   GA_InstancingPolicyToStr(NxGasGetEnumByte(CDO, TEXT("InstancingPolicy"))));
		InOutDetail->SetStringField(TEXT("NetExecutionPolicy"), GA_NetExecPolicyToStr(NxGasGetEnumByte(CDO, TEXT("NetExecutionPolicy"))));
		// bReplicateInputDirectly 是 bitfield，FBoolProperty 专用读法
		FBoolProperty* BoolProp = FindFProperty<FBoolProperty>(CDO->GetClass(), TEXT("bReplicateInputDirectly"));
		if (BoolProp)
			InOutDetail->SetBoolField(TEXT("bReplicateInputDirectly"), BoolProp->GetPropertyValue_InContainer(CDO));
	}
	else if (SectionName == TEXT("Tags"))
	{
		auto TagArr = [&](const TCHAR* Prop) -> TArray<TSharedPtr<FJsonValue>>
		{
			const FGameplayTagContainer* Tags = NxGasPropPtr<FGameplayTagContainer>(CDO, Prop);
			return Tags ? FNexusGasUtils::SerializeTagContainer(*Tags) : TArray<TSharedPtr<FJsonValue>>{};
		};
		InOutDetail->SetArrayField(TEXT("AbilityTags"),           TagArr(TEXT("AbilityTags")));
		InOutDetail->SetArrayField(TEXT("ActivationOwnedTags"),   TagArr(TEXT("ActivationOwnedTags")));
		InOutDetail->SetArrayField(TEXT("ActivationRequiredTags"),TagArr(TEXT("ActivationRequiredTags")));
		InOutDetail->SetArrayField(TEXT("ActivationBlockedTags"), TagArr(TEXT("ActivationBlockedTags")));
		InOutDetail->SetArrayField(TEXT("CancelAbilitiesWithTag"),TagArr(TEXT("CancelAbilitiesWithTag")));
		InOutDetail->SetArrayField(TEXT("BlockAbilitiesWithTag"), TagArr(TEXT("BlockAbilitiesWithTag")));
	}
	else if (SectionName == TEXT("costs"))
	{
		UClass* CostClass     = NxGasGetClassProp(CDO, TEXT("CostGameplayEffectClass"));
		UClass* CooldownClass = NxGasGetClassProp(CDO, TEXT("CooldownGameplayEffectClass"));
		if (CostClass)     InOutDetail->SetStringField(TEXT("CostGE"),     CostClass->GetPathName());
		if (CooldownClass) InOutDetail->SetStringField(TEXT("CooldownGE"), CooldownClass->GetPathName());
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
		InOutDetail->SetArrayField(TEXT("Graphs"), Graphs);
	}
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetGameplayAbilityCapability)

#endif // WITH_GAS
