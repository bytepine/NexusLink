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

void FNexusInteractRuntimeActorAnimationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractRuntimeActorAnimation");
	Out.Description = TEXT("Command runtime animation. play/stop/JumpToSection/SetAnimClass/SetAnimVariable. No stable Slot API.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),       FNexusSchema::Enum(TEXT("Animation command"),
			{ TEXT("PlayMontage"), TEXT("StopMontage"), TEXT("StopAll"), TEXT("SetAnimVariable"),
			  TEXT("JumpToSection"), TEXT("SetAnimClass") }))
		.Prop(TEXT("ActorName"),    FNexusSchema::Str(TEXT("Actor name")))
		.Prop(TEXT("MontagePath"),  FNexusSchema::Str(TEXT("Montage asset path (play/stop/jump)")))
		.Prop(TEXT("PlayRate"),     FNexusSchema::Num(TEXT("Play rate"), 1.0))
		.Prop(TEXT("StartSection"), FNexusSchema::Str(TEXT("Section name (PlayMontage/JumpToSection)")))
		.Prop(TEXT("VariableName"), FNexusSchema::Str(TEXT("AnimInstance variable name (SetAnimVariable)")))
		.Prop(TEXT("Value"),        FNexusSchema::Str(TEXT("New variable value string (SetAnimVariable)")))
		.Prop(TEXT("AnimClassPath"), FNexusSchema::Str(TEXT("AnimBlueprint GeneratedClass path (SetAnimClass)")))
		.Required({ TEXT("Action"), TEXT("ActorName") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("play"), TEXT("Montage"), TEXT("Stop"), TEXT("anim"), TEXT("slot") };
	Out.RelatedCapabilities = { TEXT("GetRuntimeActorAnimation"), TEXT("GetAssetAnimMontage") };
	Out.Prerequisites = { TEXT("PIE") };
}

FCapabilityResult FNexusInteractRuntimeActorAnimationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Action = A.Str(TEXT("Action"));

		const FString ActorName = A.Str(TEXT("ActorName"));

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		FString MontagePath, StartSection, VarName, VarValue, AnimClassPath;
		double PlayRate = 1.0;
		MontagePath = FNexusArgs(Arguments).Str(TEXT("MontagePath"), MontagePath);
		StartSection = FNexusArgs(Arguments).Str(TEXT("StartSection"), StartSection);
		VarName = FNexusArgs(Arguments).Str(TEXT("VariableName"), VarName);
		VarValue = FNexusArgs(Arguments).Str(TEXT("Value"), VarValue);
		AnimClassPath = FNexusArgs(Arguments).Str(TEXT("AnimClassPath"), AnimClassPath);
		Arguments->TryGetNumberField(TEXT("PlayRate"), PlayRate);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("ActorName"), ActorName);
		Entry->SetStringField(TEXT("Action"), Action);

		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
		if (!Actor)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		UAnimInstance* AnimInst = FindAnimInstanceForInteract(Actor);
		if (!AnimInst)
		{
			Entry->SetStringField(TEXT("Error"), TEXT("no AnimInstance"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		if (Action.Equals(TEXT("PlayMontage"), ESearchCase::IgnoreCase))
		{
			if (MontagePath.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("PlayMontage requires MontagePath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
			if (!Montage)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to load montage: %s"), *MontagePath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			const float Len = AnimInst->Montage_Play(Montage, static_cast<float>(PlayRate));
			if (!StartSection.IsEmpty())
			{
				AnimInst->Montage_JumpToSection(FName(*StartSection), Montage);
			}
			Entry->SetStringField(TEXT("Montage"), Montage->GetName());
			Entry->SetNumberField(TEXT("Length"), Len);
			Entry->SetBoolField(TEXT("bPlaying"), AnimInst->Montage_IsPlaying(Montage));
		}
		else if (Action.Equals(TEXT("StopMontage"), ESearchCase::IgnoreCase))
		{
			if (MontagePath.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("StopMontage requires MontagePath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
			if (!Montage)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to load montage: %s"), *MontagePath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			AnimInst->Montage_Stop(0.f, Montage);
			Entry->SetStringField(TEXT("Montage"), Montage->GetName());
			Entry->SetBoolField(TEXT("bStopped"), true);
		}
		else if (Action.Equals(TEXT("StopAll"), ESearchCase::IgnoreCase))
		{
			AnimInst->StopAllMontages(0.f);
			Entry->SetBoolField(TEXT("bStoppedAll"), true);
		}
		else if (Action.Equals(TEXT("SetAnimVariable"), ESearchCase::IgnoreCase))
		{
			if (VarName.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("SetAnimVariable requires VariableName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			FProperty* Prop = AnimInst->GetClass()->FindPropertyByName(FName(*VarName));
			if (!Prop)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Variable not found: %s"), *VarName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			FString OldVal, ActualVal, Err;
			if (!FNexusPropertyUtils::WritePropertyAndEcho(AnimInst, { VarName }, 0, VarValue, OldVal, ActualVal, Err))
			{
				Entry->SetStringField(TEXT("Error"), Err);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			Entry->SetStringField(TEXT("VariableName"), VarName);
			if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("OldValue"), OldVal);
			if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("NewValue"), ActualVal);
		}
		else if (Action.Equals(TEXT("JumpToSection"), ESearchCase::IgnoreCase))
		{
			if (MontagePath.IsEmpty() || StartSection.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("JumpToSection requires MontagePath and StartSection"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
			if (!Montage)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to load montage: %s"), *MontagePath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			AnimInst->Montage_JumpToSection(FName(*StartSection), Montage);
			Entry->SetStringField(TEXT("Section"), StartSection);
		}
		else if (Action.Equals(TEXT("SetAnimClass"), ESearchCase::IgnoreCase))
		{
			if (AnimClassPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("SetAnimClass requires AnimClassPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UClass* AnimClass = LoadObject<UClass>(nullptr, *AnimClassPath);
			if (!AnimClass) AnimClass = LoadObject<UClass>(nullptr, *(AnimClassPath + TEXT("_C")));
			if (!AnimClass || !AnimClass->IsChildOf(UAnimInstance::StaticClass()))
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("AnimInstance class not found: %s"), *AnimClassPath));
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
			Entry->SetNumberField(TEXT("AppliedCount"), Applied);
			Entry->SetStringField(TEXT("AnimClass"), AnimClass->GetName());
		}
		else
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusInteractRuntimeActorAnimationCapability)
