// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/AI/NexusGetRuntimeActorBehaviorTreeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusBlackboardUtils.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "NexusMcpTool.h"

void FNexusGetRuntimeActorBehaviorTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetRuntimeActorBehaviorTree");
	Out.Description = TEXT("Query runtime BT paths/state and typed inherited Blackboard keys.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Controller or Pawn name (optional; first AIController if omitted)")))
		.Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Blackboard key name filter")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("Blackboard"), TEXT("bt"), TEXT("aicontroller"), TEXT("pawn"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("InteractRuntimeActorBehaviorTree"), TEXT("GetAssetBehaviorTree") };
	Out.Prerequisites = { TEXT("PIE") };
}

FCapabilityResult FNexusGetRuntimeActorBehaviorTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FCapabilityResult R;

	UWorld* World = FNexusRuntimeUtils::GetActiveWorld();
	if (!World)
	{
		EmitError(R.Entries, {}, TEXT("No active World"));
		return R;
	}

	FString ActorName, NameFilter;
	Arguments->TryGetStringField(TEXT("ActorName"), ActorName);
	Arguments->TryGetStringField(TEXT("NameFilter"), NameFilter);

	AAIController* AICtrl = nullptr;
	if (ActorName.IsEmpty())
	{
		for (TActorIterator<AAIController> It(World); It; ++It) { AICtrl = *It; break; }
	}
	else
	{
		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
		if (Actor)
		{
			AICtrl = Cast<AAIController>(Actor);
			if (!AICtrl)
			{
				if (APawn* P = Cast<APawn>(Actor))
					AICtrl = Cast<AAIController>(P->GetController());
			}
		}
	}

	if (!AICtrl)
	{
		EmitError(R.Entries, {{TEXT("ActorName"), ActorName}}, TEXT("AIController not found"));
		return R;
	}

	UBehaviorTreeComponent* BTComp = AICtrl->FindComponentByClass<UBehaviorTreeComponent>();
	if (!BTComp)
	{
		EmitError(R.Entries, {{TEXT("ActorName"), ActorName}}, TEXT("No BehaviorTreeComponent on AIController"));
		return R;
	}

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	if (!ActorName.IsEmpty())
		Entry->SetStringField(TEXT("ActorName"), ActorName);
	Entry->SetStringField(TEXT("Controller"), AICtrl->GetName());
	Entry->SetStringField(TEXT("ControllerId"), FNexusRuntimeUtils::GetObjectIdentity(AICtrl));
	Entry->SetBoolField(TEXT("bRunning"), BTComp->IsRunning());
	Entry->SetStringField(TEXT("TreePath"), BTComp->GetCurrentTree() ? BTComp->GetCurrentTree()->GetPathName() : TEXT(""));
	Entry->SetStringField(TEXT("TreeId"), FNexusRuntimeUtils::GetObjectIdentity(BTComp->GetCurrentTree()));
	Entry->SetStringField(TEXT("RootTreePath"), BTComp->GetRootTree() ? BTComp->GetRootTree()->GetPathName() : TEXT(""));
	Entry->SetStringField(TEXT("RootTreeId"), FNexusRuntimeUtils::GetObjectIdentity(BTComp->GetRootTree()));

	if (const UBehaviorTree* BTAsset = BTComp->GetCurrentTree())
		Entry->SetStringField(TEXT("BehaviorTree"), BTAsset->GetName());
	if (const UBTNode* ActiveNode = BTComp->GetActiveNode())
		Entry->SetStringField(TEXT("ActiveNode"), ActiveNode->GetNodeName());

	if (UBlackboardComponent* BBComp = AICtrl->GetBlackboardComponent())
	{
		Entry->SetStringField(TEXT("BlackboardId"), FNexusRuntimeUtils::GetObjectIdentity(BBComp));
		if (const UBlackboardData* BBData = BBComp->GetBlackboardAsset())
		{
			TArray<TSharedPtr<FJsonValue>> Keys;
			for (int32 Index = 0; Index < BBData->GetNumKeys(); ++Index)
			{
				const FBlackboardEntry* Key = BBData->GetKey(static_cast<FBlackboard::FKey>(Index));
				if (!Key) continue;
				const FBlackboardEntry& E = *Key;
				if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(E.EntryName.ToString(), NameFilter))
					continue;
				const uint16 KeyID = BBComp->GetKeyID(E.EntryName);
				TSharedPtr<FJsonObject> KObj = MakeShared<FJsonObject>();
				KObj->SetStringField(TEXT("name"), E.EntryName.ToString());
				if (E.KeyType)
					KObj->SetStringField(TEXT("type"), E.KeyType->GetClass()->GetName());
				KObj->SetStringField(TEXT("Value"), BBComp->DescribeKeyValue(KeyID, EBlackboardDescription::OnlyValue));
				KObj->SetBoolField(TEXT("bInherited"), Index < static_cast<uint16>(BBData->GetFirstKeyID()));
				FNexusBlackboardUtils::WriteTyped(BBComp, E.EntryName, E.KeyType, *KObj);
				Keys.Add(MakeShared<FJsonValueObject>(KObj));
			}
			Entry->SetArrayField(TEXT("BlackboardKeys"), Keys);
		}
	}

	R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
	return R;
}

REGISTER_MCP_CAPABILITY(FNexusGetRuntimeActorBehaviorTreeCapability)

