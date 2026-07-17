// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetAnimBlueprintCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#if WITH_EDITOR
#include "Utils/NexusBlueprintGraphUtils.h"
#include "EdGraph/EdGraph.h"
#endif
#include "NexusMcpTool.h"

// ── FNexusCapability 基础钩子 ─────────────────────────────────────────────────

void FGetAssetAnimBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_anim_blueprint");
	Out.SearchAssetTypes = {TEXT("AnimBlueprint")};
	Out.Description = TEXT("检查 ABP 结构。sections=variables|statemachines|defaults|graphOverview。");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("abp"), TEXT("statemachine"), TEXT("variables"), TEXT("defaults"), TEXT("cdo")
	};
	Out.RelatedCapabilities = { TEXT("manage_asset_anim_blueprint"), TEXT("create_asset_anim_blueprint") };
	Out.WhenToUse = TEXT("读 ABP 变量/状态机/默认值；不含写操作");
}

TSharedPtr<FJsonObject> FGetAssetAnimBlueprintCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("动画蓝图资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个动画蓝图路径（批量）")))
		.Prop(TEXT("nameFilter"), FNexusSchema::Str(TEXT("变量/默认值名称过滤")))
		.Build();
}

// ── multi-section 钩子 ────────────────────────────────────────────────────────

TArray<FString> FGetAssetAnimBlueprintCapability::GetSectionNames() const
{
	return { TEXT("variables"), TEXT("statemachines"), TEXT("defaults"), TEXT("graphOverview") };
}

TArray<FString> FGetAssetAnimBlueprintCapability::GetDefaultSectionNames() const
{
	return { TEXT("variables"), TEXT("statemachines") };
}

bool FGetAssetAnimBlueprintCapability::PrepareEntry(const TSharedPtr<FJsonObject>& Args,
                                                    TSharedPtr<FJsonObject>&       OutEntry,
                                                    void*&                         OutTargetOpaque,
                                                    FString&                       OutError) const
{
	FString Path;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("assetPath"), Path) || Path.IsEmpty())
	{
		OutError = TEXT("缺少 assetPath");
		return false;
	}

	OutEntry->SetStringField(TEXT("path"), Path);

	UAnimBlueprint* AnimBP = FNexusAssetUtils::LoadAssetWithFallback<UAnimBlueprint>(Path);
	if (!AnimBP)
	{
		OutError = FString::Printf(TEXT("AnimBlueprint 未找到: %s"), *Path);
		return false;
	}

	OutEntry->SetStringField(TEXT("name"), AnimBP->GetName());
	if (AnimBP->TargetSkeleton)
	{
		OutEntry->SetStringField(TEXT("skeleton"), AnimBP->TargetSkeleton->GetPathName());
	}
	if (AnimBP->ParentClass)
	{
		OutEntry->SetStringField(TEXT("parentClass"), AnimBP->ParentClass->GetName());
	}

	OutTargetOpaque = static_cast<void*>(AnimBP);
	return true;
}

void FGetAssetAnimBlueprintCapability::ExecuteSection(const FString&                 SectionName,
                                                      const TSharedPtr<FJsonObject>& Args,
                                                      void*                          TargetOpaque,
                                                      TSharedPtr<FJsonObject>&       InOutDetail,
                                                      FString&                       OutError) const
{
	UAnimBlueprint* AnimBP = static_cast<UAnimBlueprint*>(TargetOpaque);
	if (!AnimBP) { OutError = TEXT("无效的 AnimBlueprint 目标"); return; }

	FString NameFilter;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("nameFilter"), NameFilter);

	if (SectionName == TEXT("variables"))
	{
#if WITH_EDITOR
		TArray<TSharedPtr<FJsonValue>> Variables;
		for (const FBPVariableDescription& Var : AnimBP->NewVariables)
		{
			const FString VarName = Var.VarName.ToString();
			if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(VarName, NameFilter)) { continue; }

			TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
			VarObj->SetStringField(TEXT("name"), VarName);
			VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
			if (!Var.VarType.PinSubCategory.IsNone())
			{
				VarObj->SetStringField(TEXT("subType"), Var.VarType.PinSubCategory.ToString());
			}
			if (!Var.Category.IsEmpty())
			{
				VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
			}
			Variables.Add(MakeShared<FJsonValueObject>(VarObj));
		}
		InOutDetail->SetArrayField(TEXT("variables"), Variables);
#else
		OutError = TEXT("variables 仅在编辑器构建可用");
#endif
	}
	else if (SectionName == TEXT("statemachines"))
	{
		TArray<TSharedPtr<FJsonValue>> StateMachines;
		UAnimBlueprintGeneratedClass* GenClass = Cast<UAnimBlueprintGeneratedClass>(AnimBP->GeneratedClass);
		if (GenClass)
		{
			const TArray<FBakedAnimationStateMachine>& BakedSMs = GenClass->GetBakedStateMachines();
			for (const FBakedAnimationStateMachine& SM : BakedSMs)
			{
				TSharedPtr<FJsonObject> SMObj = MakeShared<FJsonObject>();
				SMObj->SetStringField(TEXT("name"), SM.MachineName.ToString());

				TArray<TSharedPtr<FJsonValue>> States;
				for (const FBakedAnimationState& State : SM.States)
				{
					TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
					StateObj->SetStringField(TEXT("name"), State.StateName.ToString());
					States.Add(MakeShared<FJsonValueObject>(StateObj));
				}
				if (States.Num() > 0)
				{
					SMObj->SetArrayField(TEXT("states"), States);
				}
				StateMachines.Add(MakeShared<FJsonValueObject>(SMObj));
			}
		}
		InOutDetail->SetArrayField(TEXT("stateMachines"), StateMachines);
	}
#if WITH_EDITOR
	else if (SectionName == TEXT("graphOverview"))
	{
		UBlueprint* BP = Cast<UBlueprint>(AnimBP);
		if (!BP)
		{
			OutError = TEXT("无法将 AnimBlueprint 转为 Blueprint 以枚举图");
			return;
		}
		TArray<UEdGraph*> AllGraphs;
		FNexusBlueprintGraphUtils::CollectAllGraphs(BP, AllGraphs);
		TArray<TSharedPtr<FJsonValue>> List;
		for (const UEdGraph* G : AllGraphs)
		{
			if (G)
			{
				List.Add(MakeShared<FJsonValueObject>(FNexusBlueprintGraphUtils::BuildBPGraphSummary(G)));
			}
		}
		InOutDetail->SetArrayField(TEXT("graphs"), List);
	}
#else
	else if (SectionName == TEXT("graphOverview"))
	{
		OutError = TEXT("graphOverview 仅在编辑器构建可用");
	}
#endif
	else if (SectionName == TEXT("defaults"))
	{
		TArray<TSharedPtr<FJsonValue>> Defaults;
		UObject* CDO = AnimBP->GeneratedClass ? AnimBP->GeneratedClass->GetDefaultObject(false) : nullptr;
		if (CDO)
		{
			for (TFieldIterator<FProperty> It(AnimBP->GeneratedClass); It; ++It)
			{
				FProperty* Prop = *It;
				if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
				const FString PropName = Prop->GetName();
				if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(PropName, NameFilter)) continue;
				TSharedPtr<FJsonObject> DObj = MakeShared<FJsonObject>();
				DObj->SetStringField(TEXT("name"), PropName);
				DObj->SetStringField(TEXT("type"), Prop->GetCPPType());
				FString Val;
				FNexusPropertyUtils::ExportText(Prop, Val, Prop->ContainerPtrToValuePtr<void>(CDO));
				if (!Val.IsEmpty()) DObj->SetStringField(TEXT("value"), Val);
				Defaults.Add(MakeShared<FJsonValueObject>(DObj));
			}
		}
		InOutDetail->SetArrayField(TEXT("defaults"), Defaults);
	}
	else
	{
		OutError = FString::Printf(TEXT("未处理的 section '%s'"), *SectionName);
	}
}

TArray<TSharedPtr<FJsonObject>> FGetAssetAnimBlueprintCapability::ExpandPerEntry(
	const TSharedPtr<FJsonObject>& Args) const
{
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (!Args.IsValid() || !Args->TryGetArrayField(TEXT("assetPaths"), PathsArr) || !PathsArr || PathsArr->Num() == 0)
	{
		return {};
	}

	TArray<TSharedPtr<FJsonObject>> Result;
	for (const TSharedPtr<FJsonValue>& V : *PathsArr)
	{
		FString Path;
		if (!V.IsValid() || !V->TryGetString(Path) || Path.IsEmpty()) { continue; }

		TSharedPtr<FJsonObject> EntryArgs = MakeShared<FJsonObject>();
		for (const auto& Pair : Args->Values)
		{
			if (Pair.Key != TEXT("assetPaths")) { EntryArgs->SetField(Pair.Key, Pair.Value); }
		}
		EntryArgs->SetStringField(TEXT("assetPath"), Path);
		Result.Add(EntryArgs);
	}
	return Result;
}

REGISTER_MCP_CAPABILITY(FGetAssetAnimBlueprintCapability)
