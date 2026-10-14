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

void FNexusGetRuntimeActorAnimationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetRuntimeActorAnimation");
	Out.Description = TEXT("Get AnimInstance from running skeletal mesh. Sections=state|slots|variables.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("anim"), TEXT("Montage"), TEXT("State"), TEXT("skeletal"), TEXT("animinstance") };
	Out.RelatedCapabilities = { TEXT("InteractRuntimeActorAnimation"), TEXT("GetAssetAnimMontage") };
	Out.Prerequisites = { TEXT("PIE") };
}

TSharedPtr<FJsonObject> FNexusGetRuntimeActorAnimationCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Required(TEXT("ActorName"), FNexusSchema::Str(TEXT("Actor name")))
		.Prop(TEXT("NameFilter"),  FNexusSchema::Str(TEXT("Variable/slot name filter")))
		.Build();
}

// ── multi-section 钩子 ────────────────────────────────────────────────────────

TArray<FString> FNexusGetRuntimeActorAnimationCapability::GetSectionNames() const
{
	return { TEXT("State"), TEXT("Slots"), TEXT("Variables") };
}

TArray<FString> FNexusGetRuntimeActorAnimationCapability::GetDefaultSectionNames() const
{
	return { TEXT("State") };
}

bool FNexusGetRuntimeActorAnimationCapability::PrepareEntry(const TSharedPtr<FJsonObject>& Args,
                                                TSharedPtr<FJsonObject>&       OutEntry,
                                                void*&                         OutTargetOpaque,
                                                FString&                       OutError) const
{
	FString ActorName;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("ActorName"), ActorName) || ActorName.IsEmpty())
	{
		OutError = TEXT("Missing ActorName");
		return false;
	}

	OutEntry->SetStringField(TEXT("ActorName"), ActorName);

	UWorld* World = FNexusRuntimeUtils::GetActiveWorld();
	if (!World)
	{
		OutError = TEXT("No active World");
		return false;
	}

	AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName);
		return false;
	}

	UAnimInstance* AnimInst = FindAnimInstanceForCap(Actor);
	if (!AnimInst)
	{
		OutError = TEXT("Actor has no AnimInstance (no SkeletalMeshComponent or anim not initialized)");
		return false;
	}

	OutEntry->SetStringField(TEXT("ResolvedName"), Actor->GetName());
	OutEntry->SetStringField(TEXT("AnimClass"),    AnimInst->GetClass()->GetName());

	OutTargetOpaque = static_cast<void*>(AnimInst);
	return true;
}

void FNexusGetRuntimeActorAnimationCapability::ExecuteSection(const FString&                 SectionName,
                                                  const TSharedPtr<FJsonObject>& Args,
                                                  void*                          TargetOpaque,
                                                  TSharedPtr<FJsonObject>&       InOutDetail,
                                                  FString&                       OutError) const
{
	UAnimInstance* AnimInst = static_cast<UAnimInstance*>(TargetOpaque);
	if (!AnimInst) { OutError = TEXT("Invalid AnimInstance target"); return; }

	FString NameFilter;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("NameFilter"), NameFilter);

	if (SectionName == TEXT("State"))
	{
		UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage();
		if (ActiveMontage)
		{
			TSharedPtr<FJsonObject> MontageObj = MakeShared<FJsonObject>();
			MontageObj->SetStringField(TEXT("name"),      ActiveMontage->GetName());
			MontageObj->SetNumberField(TEXT("Position"),  AnimInst->Montage_GetPosition(ActiveMontage));
			MontageObj->SetNumberField(TEXT("PlayRate"),  AnimInst->Montage_GetPlayRate(ActiveMontage));
			MontageObj->SetBoolField(TEXT("bIsPlaying"),   AnimInst->Montage_IsPlaying(ActiveMontage));
			InOutDetail->SetObjectField(TEXT("ActiveMontage"), MontageObj);
		}

		UAnimBlueprintGeneratedClass* AnimBPClass = Cast<UAnimBlueprintGeneratedClass>(AnimInst->GetClass());
		if (AnimBPClass)
		{
			TArray<TSharedPtr<FJsonValue>> StateMachines;
			const TArray<FBakedAnimationStateMachine>& BakedMachines = AnimBPClass->GetBakedStateMachines();
			for (int32 MachineIdx = 0;
			     MachineIdx < BakedMachines.Num() && StateMachines.Num() < 20;
			     ++MachineIdx)
			{
				const FBakedAnimationStateMachine& BakedSM = BakedMachines[MachineIdx];

				TSharedPtr<FJsonObject> SMObj = MakeShared<FJsonObject>();
				SMObj->SetStringField(TEXT("name"), BakedSM.MachineName.ToString());

				// GetStateMachineInstance(idx) 按 AnimNodeProperties 倒序取节点，
				// 第 0 项常常不是状态机 → 返回 nullptr；ThirdPerson 角色上会 AV 读 +0x24。
				if (const FAnimNode_StateMachine* SMInstance = AnimInst->GetStateMachineInstanceFromName(BakedSM.MachineName))
				{
					const int32 CurrentStateIdx = SMInstance->GetCurrentState();
					if (BakedSM.States.IsValidIndex(CurrentStateIdx))
					{
						SMObj->SetStringField(TEXT("CurrentState"), BakedSM.States[CurrentStateIdx].StateName.ToString());
					}
				}
				SMObj->SetNumberField(TEXT("StateCount"), BakedSM.States.Num());
				StateMachines.Add(MakeShared<FJsonValueObject>(SMObj));
			}
			if (StateMachines.Num() > 0)
			{
				InOutDetail->SetArrayField(TEXT("StateMachines"), StateMachines);
			}
		}
	}
	else if (SectionName == TEXT("Slots"))
	{
		TArray<TSharedPtr<FJsonValue>> Slots;
		for (int32 i = 0; i < AnimInst->MontageInstances.Num() && i < 50; ++i)
		{
			FAnimMontageInstance* MI = AnimInst->MontageInstances[i];
			if (!MI || !MI->Montage) continue;

			const FString SlotName = MI->Montage->GetName();
			if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(SlotName, NameFilter)) continue;

			TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetStringField(TEXT("Montage"),   MI->Montage->GetName());
			SlotObj->SetBoolField(TEXT("bIsPlaying"),   MI->IsPlaying());
			SlotObj->SetNumberField(TEXT("Position"),  MI->GetPosition());
			SlotObj->SetNumberField(TEXT("Weight"),    MI->GetWeight());
			Slots.Add(MakeShared<FJsonValueObject>(SlotObj));
		}
		InOutDetail->SetArrayField(TEXT("Slots"), Slots);
		InOutDetail->SetNumberField(TEXT("Count"), Slots.Num());
	}
	else if (SectionName == TEXT("Variables"))
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
			VarObj->SetStringField(TEXT("Value"), Value);

			Vars.Add(MakeShared<FJsonValueObject>(VarObj));
			if (Vars.Num() >= 200) break;
		}
		InOutDetail->SetArrayField(TEXT("Variables"), Vars);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unhandled section '%s'"), *SectionName);
	}
}

REGISTER_MCP_CAPABILITY(FNexusGetRuntimeActorAnimationCapability)

