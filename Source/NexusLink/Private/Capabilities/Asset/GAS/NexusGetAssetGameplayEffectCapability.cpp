// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusGetAssetGameplayEffectCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusGasUtils.h"
#include "GameplayEffect.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"

// ── DurationPolicy 枚举 → 字符串 ─────────────────────────────────────────────

static FString GE_DurationPolicyToStr(uint8 V)
{
	switch ((EGameplayEffectDurationType)V)
	{
		case EGameplayEffectDurationType::Instant:     return TEXT("Instant");
		case EGameplayEffectDurationType::Infinite:    return TEXT("Infinite");
		case EGameplayEffectDurationType::HasDuration: return TEXT("HasDuration");
		default: return TEXT("Unknown");
	}
}

// ── FInheritedTagContainer 反射访问辅助 ─────────────────────────────────────
// FInheritedTagContainer 的 Added/Removed 成员是 public（USTRUCT），通过两步反射取指针：
// 1. FindFProperty 在 UClass 上定位外层 FStructProperty
// 2. ContainerPtrToValuePtr<FInheritedTagContainer> 得到结构体指针后直接访问 .Added

static TArray<TSharedPtr<FJsonValue>> GetGE_InheritedTagAdded(UObject* CDO, const TCHAR* PropName)
{
	FStructProperty* P = FindFProperty<FStructProperty>(CDO->GetClass(), PropName);
	if (!P) return {};
	FInheritedTagContainer* IC = P->ContainerPtrToValuePtr<FInheritedTagContainer>(CDO);
	return IC ? FNexusGasUtils::SerializeTagContainer(IC->Added) : TArray<TSharedPtr<FJsonValue>>{};
}

// ── Capability 元数据 ─────────────────────────────────────────────────────────

void FGetAssetGameplayEffectCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_gameplay_effect");
	Out.SearchAssetTypes = {TEXT("GameplayEffect")};
	Out.Description = TEXT("读 GE Blueprint。sections=policy|modifiers|tags|cues；只读。");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("effect"), TEXT("ge"), TEXT("modifier"), TEXT("duration") };
	Out.RelatedCapabilities = { TEXT("manage_asset_gameplay_effect"), TEXT("create_asset_gameplay_effect") };
	Out.WhenToUse = TEXT("读 GE 持续时间/Modifier/Tag/Cue；不含写操作");
}

TSharedPtr<FJsonObject> FGetAssetGameplayEffectCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("GameplayEffect Blueprint 路径")))
		.Required({ TEXT("assetPath") })
		.Build();
}

TArray<FString> FGetAssetGameplayEffectCapability::GetSectionNames() const
{
	return { TEXT("policy"), TEXT("modifiers"), TEXT("tags"), TEXT("cues") };
}

TArray<FString> FGetAssetGameplayEffectCapability::GetDefaultSectionNames() const
{
	return { TEXT("policy"), TEXT("modifiers"), TEXT("tags") };
}

bool FGetAssetGameplayEffectCapability::PrepareEntry(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>&       OutEntry,
	void*&                         OutTargetOpaque,
	FString&                       OutError) const
{
	FString Path;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("assetPath"), Path) || Path.IsEmpty())
	{ OutError = TEXT("缺少 assetPath"); return false; }

	FString LoadError;
	UBlueprint* BP = FNexusGasUtils::LoadGameplayEffectBlueprint(Path, LoadError);
	if (!BP) { OutError = LoadError; return false; }

	OutEntry->SetStringField(TEXT("assetPath"), Path);
	OutEntry->SetStringField(TEXT("name"),      BP->GetName());
	if (BP->ParentClass) OutEntry->SetStringField(TEXT("parentClass"), BP->ParentClass->GetName());

	OutTargetOpaque = static_cast<void*>(BP);
	return true;
}

void FGetAssetGameplayEffectCapability::ExecuteSection(
	const FString&                 SectionName,
	const TSharedPtr<FJsonObject>& Args,
	void*                          TargetOpaque,
	TSharedPtr<FJsonObject>&       InOutDetail,
	FString&                       OutError) const
{
	UBlueprint* BP = static_cast<UBlueprint*>(TargetOpaque);
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("无效的 GE Blueprint 目标"); return; }

	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	if (!CDO) { OutError = TEXT("无法获取 GameplayEffect CDO"); return; }

	if (SectionName == TEXT("policy"))
	{
		InOutDetail->SetStringField(TEXT("durationPolicy"), GE_DurationPolicyToStr(NxGasGetEnumByte(CDO, TEXT("DurationPolicy"))));
		if (FScalableFloat* P = NxGasPropPtr<FScalableFloat>(CDO, TEXT("Period")))
			InOutDetail->SetNumberField(TEXT("period"), P->Value);
		if (FGameplayEffectModifierMagnitude* M = NxGasPropPtr<FGameplayEffectModifierMagnitude>(CDO, TEXT("DurationMagnitude")))
		{
			float DurVal = 0.f;
			if (M->GetStaticMagnitudeIfPossible(1.f, DurVal))
				InOutDetail->SetNumberField(TEXT("duration"), DurVal);
		}
	}
	else if (SectionName == TEXT("modifiers"))
	{
		UGameplayEffect* GECDO = BP->GeneratedClass->GetDefaultObject<UGameplayEffect>();
		InOutDetail->SetArrayField(TEXT("modifiers"), FNexusGasUtils::SerializeGEModifiers(GECDO));
	}
	else if (SectionName == TEXT("tags"))
	{
		InOutDetail->SetArrayField(TEXT("gameplayEffectTags"), GetGE_InheritedTagAdded(CDO, TEXT("InheritableGameplayEffectTags")));
		InOutDetail->SetArrayField(TEXT("grantedTags"),        GetGE_InheritedTagAdded(CDO, TEXT("InheritableOwnedTagsContainer")));
		// InheritableBlockedAbilityTagsContainer 仅部分 UE 版本有，反射访问安全跳过
		TArray<TSharedPtr<FJsonValue>> BlockedTags = GetGE_InheritedTagAdded(CDO, TEXT("InheritableBlockedAbilityTagsContainer"));
		if (BlockedTags.Num() > 0 || FindFProperty<FProperty>(CDO->GetClass(), TEXT("InheritableBlockedAbilityTagsContainer")))
			InOutDetail->SetArrayField(TEXT("blockedAbilityTags"), BlockedTags);

		if (FGameplayTagRequirements* Req = NxGasPropPtr<FGameplayTagRequirements>(CDO, TEXT("ApplicationTagRequirements")))
		{
			InOutDetail->SetArrayField(TEXT("applicationRequiredTags"), FNexusGasUtils::SerializeTagContainer(Req->RequireTags));
			InOutDetail->SetArrayField(TEXT("applicationIgnoredTags"),  FNexusGasUtils::SerializeTagContainer(Req->IgnoreTags));
		}
	}
	else if (SectionName == TEXT("cues"))
	{
		TArray<TSharedPtr<FJsonValue>> Cues;
		if (TArray<FGameplayEffectCue>* CueArr = NxGasPropPtr<TArray<FGameplayEffectCue>>(CDO, TEXT("GameplayCues")))
		{
			for (const FGameplayEffectCue& Cue : *CueArr)
				Cues.Append(FNexusGasUtils::SerializeTagContainer(Cue.GameplayCueTags));
		}
		InOutDetail->SetArrayField(TEXT("gameplayCueTags"), Cues);
	}
}

REGISTER_MCP_CAPABILITY(FGetAssetGameplayEffectCapability)

#endif // WITH_GAS
