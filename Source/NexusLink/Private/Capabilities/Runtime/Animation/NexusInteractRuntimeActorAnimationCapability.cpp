// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Animation/NexusInteractRuntimeActorAnimationCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
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

void FInteractRuntimeActorAnimationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("interact_runtime_actor_animation");
	Out.Description = TEXT("Command runtime animation. play/stop/jump_to_section/set_anim_class/set_anim_variable. No stable Slot API.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Animation command"),
			{ TEXT("play_montage"), TEXT("stop_montage"), TEXT("stop_all"), TEXT("set_anim_variable"),
			  TEXT("jump_to_section"), TEXT("set_anim_class") }))
		.Prop(TEXT("actorName"),    FNexusSchema::Str(TEXT("Actor name")))
		.Prop(TEXT("montagePath"),  FNexusSchema::Str(TEXT("Montage asset path (play/stop/jump)")))
		.Prop(TEXT("playRate"),     FNexusSchema::Num(TEXT("Play rate"), 1.0))
		.Prop(TEXT("startSection"), FNexusSchema::Str(TEXT("Section name (play_montage/jump_to_section)")))
		.Prop(TEXT("variableName"), FNexusSchema::Str(TEXT("AnimInstance variable name (set_anim_variable)")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("New variable value string (set_anim_variable)")))
		.Prop(TEXT("animClassPath"), FNexusSchema::Str(TEXT("AnimBlueprint GeneratedClass path (set_anim_class)")))
		.Required({ TEXT("action"), TEXT("actorName") })
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
		const FNexusArgs A(Arguments);
		const FString Action = A.Str(TEXT("action"));

		const FString ActorName = A.Str(TEXT("actorName"));

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		FString MontagePath, StartSection, VarName, VarValue, AnimClassPath;
		double PlayRate = 1.0;
		Arguments->TryGetStringField(TEXT("montagePath"), MontagePath);
		Arguments->TryGetStringField(TEXT("startSection"), StartSection);
		Arguments->TryGetStringField(TEXT("variableName"), VarName);
		Arguments->TryGetStringField(TEXT("value"), VarValue);
		Arguments->TryGetStringField(TEXT("animClassPath"), AnimClassPath);
		Arguments->TryGetNumberField(TEXT("playRate"), PlayRate);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("actorName"), ActorName);
		Entry->SetStringField(TEXT("action"), Action);

		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
		if (!Actor)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		UAnimInstance* AnimInst = FindAnimInstanceForInteract(Actor);
		if (!AnimInst)
		{
			Entry->SetStringField(TEXT("error"), TEXT("no AnimInstance"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		if (Action.Equals(TEXT("play_montage"), ESearchCase::IgnoreCase))
		{
			if (MontagePath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("play_montage requires montagePath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
			if (!Montage)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load montage: %s"), *MontagePath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
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
				Entry->SetStringField(TEXT("error"), TEXT("stop_montage requires montagePath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
			if (!Montage)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load montage: %s"), *MontagePath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
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
				Entry->SetStringField(TEXT("error"), TEXT("set_anim_variable requires variableName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			FProperty* Prop = AnimInst->GetClass()->FindPropertyByName(FName(*VarName));
			if (!Prop)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Variable not found: %s"), *VarName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			FString OldVal, ActualVal, Err;
			if (!FNexusPropertyUtils::WritePropertyAndEcho(AnimInst, { VarName }, 0, VarValue, OldVal, ActualVal, Err))
			{
				Entry->SetStringField(TEXT("error"), Err);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			Entry->SetStringField(TEXT("variableName"), VarName);
			if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
			if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
		}
		else if (Action.Equals(TEXT("jump_to_section"), ESearchCase::IgnoreCase))
		{
			if (MontagePath.IsEmpty() || StartSection.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("jump_to_section requires montagePath and startSection"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
			if (!Montage)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load montage: %s"), *MontagePath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			AnimInst->Montage_JumpToSection(FName(*StartSection), Montage);
			Entry->SetStringField(TEXT("section"), StartSection);
		}
		else if (Action.Equals(TEXT("set_anim_class"), ESearchCase::IgnoreCase))
		{
			if (AnimClassPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_anim_class requires animClassPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UClass* AnimClass = LoadObject<UClass>(nullptr, *AnimClassPath);
			if (!AnimClass) AnimClass = LoadObject<UClass>(nullptr, *(AnimClassPath + TEXT("_C")));
			if (!AnimClass || !AnimClass->IsChildOf(UAnimInstance::StaticClass()))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimInstance class not found: %s"), *AnimClassPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			TArray<UActorComponent*> Comps;
			Actor->GetComponents(USkeletalMeshComponent::StaticClass(), Comps);
			int32 Applied = 0;
			for (UActorComponent* Comp : Comps)
			{
				if (USkeletalMeshComponent* Skel = Cast<USkeletalMeshComponent>(Comp))
				{
					Skel->SetAnimInstanceClass(AnimClass);
					++Applied;
				}
			}
			Entry->SetNumberField(TEXT("appliedCount"), Applied);
			Entry->SetStringField(TEXT("animClass"), AnimClass->GetName());
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FInteractRuntimeActorAnimationCapability)
