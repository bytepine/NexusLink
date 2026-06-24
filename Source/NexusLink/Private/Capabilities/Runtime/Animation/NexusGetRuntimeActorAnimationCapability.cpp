// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Animation/NexusGetRuntimeActorAnimationCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimNode_StateMachine.h"
#include "NexusMcpTool.h"

static UAnimInstance* FindAnimInstanceForCap(AActor* Actor)
{
	if (!Actor) return nullptr;
	TArray<UActorComponent*> Comps;
	Actor->GetComponents(USkeletalMeshComponent::StaticClass(), Comps);
	for (UActorComponent* Comp : Comps)
	{
		USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(Comp);
		if (SkelMesh && SkelMesh->GetAnimInstance())
		{
			return SkelMesh->GetAnimInstance();
		}
	}
	return nullptr;
}

// ── FNexusCapability 基础钩子 ─────────────────────────────────────────────────

void FGetRuntimeActorAnimationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_actor_animation");
	Out.Description = TEXT("从运行中骨骼网格取 AnimInstance。sections=state|slots|variables。");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("anim"), TEXT("montage"), TEXT("state"), TEXT("skeletal"), TEXT("animinstance") };
	Out.RelatedCapabilities = { TEXT("interact_runtime_actor_animation"), TEXT("get_asset_anim_montage") };
	Out.Prerequisites = { TEXT("pie") };
}

TSharedPtr<FJsonObject> FGetRuntimeActorAnimationCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("actorName"),   FNexusSchema::Str(TEXT("Actor 名")))
		.Prop(TEXT("actorNames"),  FNexusSchema::StrArr(TEXT("多个 Actor 名（批量）")))
		.Prop(TEXT("nameFilter"),  FNexusSchema::Str(TEXT("变量/槽位名过滤")))
		.Build();
}

// ── multi-section 钩子 ────────────────────────────────────────────────────────

TArray<FString> FGetRuntimeActorAnimationCapability::GetSectionNames() const
{
	return { TEXT("state"), TEXT("slots"), TEXT("variables") };
}

TArray<FString> FGetRuntimeActorAnimationCapability::GetDefaultSectionNames() const
{
	return { TEXT("state") };
}

bool FGetRuntimeActorAnimationCapability::PrepareEntry(const TSharedPtr<FJsonObject>& Args,
                                                TSharedPtr<FJsonObject>&       OutEntry,
                                                void*&                         OutTargetOpaque,
                                                FString&                       OutError) const
{
	FString ActorName;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("actorName"), ActorName) || ActorName.IsEmpty())
	{
		OutError = TEXT("缺少 actorName");
		return false;
	}

	OutEntry->SetStringField(TEXT("actorName"), ActorName);

	UWorld* World = FNexusRuntimeUtils::GetActiveWorld();
	if (!World)
	{
		OutError = TEXT("无活动 World");
		return false;
	}

	AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Actor 未找到: %s"), *ActorName);
		return false;
	}

	UAnimInstance* AnimInst = FindAnimInstanceForCap(Actor);
	if (!AnimInst)
	{
		OutError = TEXT("Actor 无 AnimInstance（无 SkeletalMeshComponent 或动画未初始化）");
		return false;
	}

	OutEntry->SetStringField(TEXT("resolvedName"), Actor->GetName());
	OutEntry->SetStringField(TEXT("animClass"),    AnimInst->GetClass()->GetName());

	OutTargetOpaque = static_cast<void*>(AnimInst);
	return true;
}

void FGetRuntimeActorAnimationCapability::ExecuteSection(const FString&                 SectionName,
                                                  const TSharedPtr<FJsonObject>& Args,
                                                  void*                          TargetOpaque,
                                                  TSharedPtr<FJsonObject>&       InOutDetail,
                                                  FString&                       OutError) const
{
	UAnimInstance* AnimInst = static_cast<UAnimInstance*>(TargetOpaque);
	if (!AnimInst) { OutError = TEXT("无效的 AnimInstance 目标"); return; }

	FString NameFilter;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("nameFilter"), NameFilter);

	if (SectionName == TEXT("state"))
	{
		UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage();
		if (ActiveMontage)
		{
			TSharedPtr<FJsonObject> MontageObj = MakeShared<FJsonObject>();
			MontageObj->SetStringField(TEXT("name"),      ActiveMontage->GetName());
			MontageObj->SetNumberField(TEXT("position"),  AnimInst->Montage_GetPosition(ActiveMontage));
			MontageObj->SetNumberField(TEXT("playRate"),  AnimInst->Montage_GetPlayRate(ActiveMontage));
			MontageObj->SetBoolField(TEXT("isPlaying"),   AnimInst->Montage_IsPlaying(ActiveMontage));
			InOutDetail->SetObjectField(TEXT("activeMontage"), MontageObj);
		}

		UAnimBlueprintGeneratedClass* AnimBPClass = Cast<UAnimBlueprintGeneratedClass>(AnimInst->GetClass());
		if (AnimBPClass)
		{
			TArray<TSharedPtr<FJsonValue>> StateMachines;
			for (int32 MachineIdx = 0;
			     MachineIdx < AnimBPClass->AnimNodeProperties.Num() && StateMachines.Num() < 20;
			     ++MachineIdx)
			{
				const FBakedAnimationStateMachine* BakedSM = AnimBPClass->GetBakedStateMachines().IsValidIndex(MachineIdx)
					? &AnimBPClass->GetBakedStateMachines()[MachineIdx] : nullptr;
				if (!BakedSM) continue;

				TSharedPtr<FJsonObject> SMObj = MakeShared<FJsonObject>();
				SMObj->SetStringField(TEXT("name"), BakedSM->MachineName.ToString());

				const int32 CurrentStateIdx = AnimInst->GetStateMachineInstance(MachineIdx)->GetCurrentState();
				if (BakedSM->States.IsValidIndex(CurrentStateIdx))
				{
					SMObj->SetStringField(TEXT("currentState"), BakedSM->States[CurrentStateIdx].StateName.ToString());
				}
				SMObj->SetNumberField(TEXT("stateCount"), BakedSM->States.Num());
				StateMachines.Add(MakeShared<FJsonValueObject>(SMObj));
			}
			if (StateMachines.Num() > 0)
			{
				InOutDetail->SetArrayField(TEXT("stateMachines"), StateMachines);
			}
		}
	}
	else if (SectionName == TEXT("slots"))
	{
		TArray<TSharedPtr<FJsonValue>> Slots;
		for (int32 i = 0; i < AnimInst->MontageInstances.Num() && i < 50; ++i)
		{
			FAnimMontageInstance* MI = AnimInst->MontageInstances[i];
			if (!MI || !MI->Montage) continue;

			const FString SlotName = MI->Montage->GetName();
			if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(SlotName, NameFilter)) continue;

			TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetStringField(TEXT("montage"),   MI->Montage->GetName());
			SlotObj->SetBoolField(TEXT("isPlaying"),   MI->IsPlaying());
			SlotObj->SetNumberField(TEXT("position"),  MI->GetPosition());
			SlotObj->SetNumberField(TEXT("weight"),    MI->GetWeight());
			Slots.Add(MakeShared<FJsonValueObject>(SlotObj));
		}
		InOutDetail->SetArrayField(TEXT("slots"), Slots);
		InOutDetail->SetNumberField(TEXT("count"), Slots.Num());
	}
	else if (SectionName == TEXT("variables"))
	{
		TArray<TSharedPtr<FJsonValue>> Vars;
		for (TFieldIterator<FProperty> It(AnimInst->GetClass()); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Edit)) continue;
			if (It->GetOwnerClass() == UAnimInstance::StaticClass()) continue;

			const FString PropName = It->GetName();
			if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(PropName, NameFilter)) continue;

			TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
			VarObj->SetStringField(TEXT("name"), PropName);
			VarObj->SetStringField(TEXT("type"), It->GetCPPType());

			FString Value;
			const void* Ptr = It->ContainerPtrToValuePtr<void>(AnimInst);
#if NX_UE_HAS_EXPORT_TEXT_ITEM_DIR
			It->ExportTextItem_Direct(Value, Ptr, nullptr, nullptr, PPF_None);
#else
			It->ExportTextItem(Value, Ptr, nullptr, nullptr, PPF_None);
#endif
			VarObj->SetStringField(TEXT("value"), Value);

			Vars.Add(MakeShared<FJsonValueObject>(VarObj));
			if (Vars.Num() >= 200) break;
		}
		InOutDetail->SetArrayField(TEXT("variables"), Vars);
	}
	else
	{
		OutError = FString::Printf(TEXT("未处理的 section '%s'"), *SectionName);
	}
}

TArray<TSharedPtr<FJsonObject>> FGetRuntimeActorAnimationCapability::ExpandPerEntry(
	const TSharedPtr<FJsonObject>& Args) const
{
	const TArray<TSharedPtr<FJsonValue>>* NamesArr = nullptr;
	if (!Args.IsValid() || !Args->TryGetArrayField(TEXT("actorNames"), NamesArr) || !NamesArr || NamesArr->Num() == 0)
	{
		return {};
	}

	TArray<TSharedPtr<FJsonObject>> Result;
	for (const TSharedPtr<FJsonValue>& V : *NamesArr)
	{
		FString Name;
		if (!V.IsValid() || !V->TryGetString(Name) || Name.IsEmpty()) { continue; }

		TSharedPtr<FJsonObject> EntryArgs = MakeShared<FJsonObject>();
		for (const auto& Pair : Args->Values)
		{
			if (Pair.Key != TEXT("actorNames")) { EntryArgs->SetField(Pair.Key, Pair.Value); }
		}
		EntryArgs->SetStringField(TEXT("actorName"), Name);
		Result.Add(EntryArgs);
	}
	return Result;
}

REGISTER_MCP_CAPABILITY(FGetRuntimeActorAnimationCapability)

