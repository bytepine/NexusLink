// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Animation/NexusInteractRuntimeActorAnimationCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "NexusMcpTool.h"

static UAnimInstance* FindAnimInstanceForInteract(AActor* Actor)
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

static void CollectActorNames(const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutNames)
{
	OutNames.Reset();
	if (!Args.IsValid()) return;

	FString Single;
	if (Args->TryGetStringField(TEXT("actorName"), Single) && !Single.IsEmpty())
	{
		OutNames.Add(Single);
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(TEXT("actorNames"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString N;
			if (V.IsValid() && V->TryGetString(N) && !N.IsEmpty())
			{
				OutNames.AddUnique(N);
			}
		}
	}
}

void FInteractRuntimeActorAnimationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("interact_runtime_actor_animation");
	Out.Description = TEXT("命令式控制运行时动画。action=play_montage|stop_montage|stop_all|set_anim_variable。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("动画命令"),
			{ TEXT("play_montage"), TEXT("stop_montage"), TEXT("stop_all"), TEXT("set_anim_variable") }))
		.Prop(TEXT("actorName"),    FNexusSchema::Str(TEXT("Actor 名")))
		.Prop(TEXT("actorNames"),   FNexusSchema::StrArr(TEXT("多个 Actor 名（批量）")))
		.Prop(TEXT("montagePath"),  FNexusSchema::Str(TEXT("蒙太奇资产路径（play/stop）")))
		.Prop(TEXT("playRate"),     FNexusSchema::Num(TEXT("播放速率"), 1.0))
		.Prop(TEXT("startSection"), FNexusSchema::Str(TEXT("起始 Section 名（play_montage）")))
		.Prop(TEXT("variableName"), FNexusSchema::Str(TEXT("AnimInstance 变量名（set_anim_variable）")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("变量新值字符串（set_anim_variable）")))
		.Required({ TEXT("action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("play"), TEXT("montage"), TEXT("stop"), TEXT("anim"), TEXT("slot") };
	Out.RelatedCapabilities = { TEXT("get_runtime_actor_animation"), TEXT("get_asset_anim_montage") };
	Out.Prerequisites = { TEXT("pie") };
}

FCapabilityResult FInteractRuntimeActorAnimationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString Action;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{
			OutError = TEXT("缺少 action");
			return;
		}

		TArray<FString> ActorNames;
		CollectActorNames(Arguments, ActorNames);
		if (ActorNames.Num() == 0)
		{
			OutError = TEXT("需要 actorName 或 actorNames");
			return;
		}

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		FString MontagePath, StartSection, VarName, VarValue;
		double PlayRate = 1.0;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("montagePath"), MontagePath);
			Arguments->TryGetStringField(TEXT("startSection"), StartSection);
			Arguments->TryGetStringField(TEXT("variableName"), VarName);
			Arguments->TryGetStringField(TEXT("value"), VarValue);
			Arguments->TryGetNumberField(TEXT("playRate"), PlayRate);
		}

		for (const FString& ActorName : ActorNames)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("actorName"), ActorName);
			Entry->SetStringField(TEXT("action"), Action);

			AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
			if (!Actor)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor 未找到: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			UAnimInstance* AnimInst = FindAnimInstanceForInteract(Actor);
			if (!AnimInst)
			{
				Entry->SetStringField(TEXT("error"), TEXT("无 AnimInstance"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			if (Action.Equals(TEXT("play_montage"), ESearchCase::IgnoreCase))
			{
				if (MontagePath.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("play_montage 需要 montagePath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
				if (!Montage)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("蒙太奇加载失败: %s"), *MontagePath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const float Len = AnimInst->Montage_Play(Montage, static_cast<float>(PlayRate));
				if (!StartSection.IsEmpty())
				{
					AnimInst->Montage_JumpToSection(FName(*StartSection), Montage);
				}
				Entry->SetStringField(TEXT("montage"), Montage->GetName());
				Entry->SetNumberField(TEXT("length"), Len);
				Entry->SetBoolField(TEXT("playing"), AnimInst->Montage_IsPlaying(Montage));
			}
			else if (Action.Equals(TEXT("stop_montage"), ESearchCase::IgnoreCase))
			{
				if (MontagePath.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("stop_montage 需要 montagePath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
				if (!Montage)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("蒙太奇加载失败: %s"), *MontagePath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				AnimInst->Montage_Stop(0.f, Montage);
				Entry->SetStringField(TEXT("montage"), Montage->GetName());
				Entry->SetBoolField(TEXT("stopped"), true);
			}
			else if (Action.Equals(TEXT("stop_all"), ESearchCase::IgnoreCase))
			{
				AnimInst->StopAllMontages(0.f);
				Entry->SetBoolField(TEXT("stoppedAll"), true);
			}
			else if (Action.Equals(TEXT("set_anim_variable"), ESearchCase::IgnoreCase))
			{
				if (VarName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_anim_variable 需要 variableName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FProperty* Prop = AnimInst->GetClass()->FindPropertyByName(FName(*VarName));
				if (!Prop)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("变量未找到: %s"), *VarName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FString OldVal, ActualVal, Err;
				if (!FNexusPropertyUtils::WritePropertyAndEcho(AnimInst, { VarName }, 0, VarValue, OldVal, ActualVal, Err))
				{
					Entry->SetStringField(TEXT("error"), Err);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Entry->SetStringField(TEXT("variableName"), VarName);
				if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
				if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FInteractRuntimeActorAnimationCapability)
