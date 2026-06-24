// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusManageAssetGameplayEffectCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusGasUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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
	Out.Description = TEXT("批量修改 GE CDO：ops[] 含 set_policy/set_tags/add_modifier/remove_modifier/set_modifier。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("GameplayEffect Blueprint 路径")))
		.Prop(TEXT("ops"),       FNexusSchema::StrArr(TEXT("操作数组；每项为含 action 字段的 JSON 对象")))
		.Required({ TEXT("assetPath"), TEXT("ops") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("effect"), TEXT("modifier"), TEXT("duration"), TEXT("tag") };
	Out.RelatedCapabilities = { TEXT("get_asset_gameplay_effect"), TEXT("save_asset"), TEXT("create_asset_gameplay_effect") };
	Out.WhenToUse = TEXT("批量修改 GE 策略/Tag/Modifier；改完会自动重编译 Blueprint");
}

FCapabilityResult FManageAssetGameplayEffectCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{ OutError = TEXT("缺少 assetPath"); return; }

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("ops"), OpsArr) || !OpsArr || OpsArr->Num() == 0)
		{ OutError = TEXT("ops 为必填且不能为空数组"); return; }

		FString LoadError;
		UBlueprint* BP = FNexusGasUtils::LoadGameplayEffectBlueprint(AssetPath, LoadError);
		if (!BP) { OutError = LoadError; return; }
		if (!BP->GeneratedClass) { OutError = TEXT("Blueprint 未编译"); return; }

		UObject* CDO = BP->GeneratedClass->GetDefaultObject();
		UGameplayEffect* GECDO = BP->GeneratedClass->GetDefaultObject<UGameplayEffect>();
		if (!CDO || !GECDO) { OutError = TEXT("无法获取 GameplayEffect CDO"); return; }

		int32 Applied = 0;
		for (int32 i = 0; i < OpsArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!(*OpsArr)[i].IsValid() || !(*OpsArr)[i]->TryGetObject(OpObjPtr) || !OpObjPtr)
			{ OutError = FString::Printf(TEXT("ops[%d] 不是有效的 JSON 对象"), i); return; }

			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;
			FString Action;
			if (!Op->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
			{ OutError = FString::Printf(TEXT("ops[%d] 缺少 action 字段"), i); return; }

			if (Action == TEXT("set_policy"))
			{
				FString PolicyStr;
				if (Op->TryGetStringField(TEXT("durationPolicy"), PolicyStr) && !PolicyStr.IsEmpty())
				{
					uint8 V = 0;
					if      (PolicyStr == TEXT("Instant"))     V = (uint8)EGameplayEffectDurationType::Instant;
					else if (PolicyStr == TEXT("Infinite"))    V = (uint8)EGameplayEffectDurationType::Infinite;
					else if (PolicyStr == TEXT("HasDuration")) V = (uint8)EGameplayEffectDurationType::HasDuration;
					else { OutError = FString::Printf(TEXT("ops[%d] 无效的 durationPolicy: %s"), i, *PolicyStr); return; }
					NxGasSetEnumByte(CDO, TEXT("DurationPolicy"), V);
				}
				if (Op->HasField(TEXT("duration")))
				{
					float DurVal = (float)Op->GetNumberField(TEXT("duration"));
					if (FGameplayEffectModifierMagnitude* M = NxGasPropPtr<FGameplayEffectModifierMagnitude>(CDO, TEXT("DurationMagnitude")))
						*M = FGameplayEffectModifierMagnitude(FScalableFloat(DurVal));
				}
				if (Op->HasField(TEXT("period")))
				{
					float PeriodVal = (float)Op->GetNumberField(TEXT("period"));
					if (FScalableFloat* S = NxGasPropPtr<FScalableFloat>(CDO, TEXT("Period")))
						S->Value = PeriodVal;
				}
			}
			else if (Action == TEXT("set_tags"))
			{
				FString ContainerName, Mode;
				if (!Op->TryGetStringField(TEXT("tagContainer"), ContainerName) || ContainerName.IsEmpty())
				{ OutError = FString::Printf(TEXT("ops[%d] set_tags 需要 tagContainer"), i); return; }
				if (!Op->TryGetStringField(TEXT("mode"), Mode) || Mode.IsEmpty()) Mode = TEXT("set");

				TArray<FString> Tags;
				const TArray<TSharedPtr<FJsonValue>>* TagsArr = nullptr;
				if (Op->TryGetArrayField(TEXT("tags"), TagsArr) && TagsArr)
				{
					for (const TSharedPtr<FJsonValue>& V : *TagsArr)
					{ FString S; if (V.IsValid() && V->TryGetString(S)) Tags.Add(S); }
				}

				static const TMap<FString, FString> ContainerMap = {
					{ TEXT("gameplayEffectTags"), TEXT("InheritableGameplayEffectTags")          },
					{ TEXT("grantedTags"),        TEXT("InheritableOwnedTagsContainer")          },
					{ TEXT("blockedAbilityTags"), TEXT("InheritableBlockedAbilityTagsContainer") },
				};
				const FString* PropName = ContainerMap.Find(ContainerName);
				if (!PropName) { OutError = FString::Printf(TEXT("ops[%d] 未知 tagContainer: %s"), i, *ContainerName); return; }

				FGameplayTagContainer* Container = GetGE_InheritedTagAddedMut(CDO, **PropName);
				if (!Container) { OutError = FString::Printf(TEXT("ops[%d] 属性不存在或此 UE 版本不支持: %s"), i, **PropName); return; }

				FString TagError;
				if (!FNexusGasUtils::ApplyTagContainer(*Container, Tags, Mode, TagError))
				{ OutError = FString::Printf(TEXT("ops[%d] %s"), i, *TagError); return; }
			}
			else if (Action == TEXT("add_modifier") || Action == TEXT("remove_modifier") || Action == TEXT("set_modifier"))
			{
				FString ModError;
				if (!FNexusGasUtils::ApplyGEModifierOp(GECDO, Action, Op, ModError))
				{ OutError = FString::Printf(TEXT("ops[%d] %s"), i, *ModError); return; }
			}
			else
			{
				OutError = FString::Printf(TEXT("ops[%d] 未知 action: %s"), i, *Action);
				return;
			}
			++Applied;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);

		OutTop->SetStringField(TEXT("assetPath"), AssetPath);
		OutTop->SetNumberField(TEXT("appliedOps"), Applied);
		OutTop->SetBoolField(TEXT("success"),     true);
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetGameplayEffectCapability)

#endif // WITH_GAS
