// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/AI/NexusGetRuntimeActorBehaviorTreeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusStringMatchUtils.h"
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

void FGetRuntimeActorBehaviorTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_actor_behavior_tree");
	Out.Description = TEXT("查运行中 AI 的 BT 节点与 BB 键值。写黑板/重启用 interact。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("actorName"), FNexusSchema::Str(TEXT("Controller 或 Pawn 名（可选；省略则用首个 AIController）")))
		.Prop(TEXT("nameFilter"), FNexusSchema::Str(TEXT("黑板键名过滤")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("blackboard"), TEXT("bt"), TEXT("aicontroller"), TEXT("pawn"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("interact_runtime_actor_behavior_tree"), TEXT("get_asset_behavior_tree") };
	Out.Prerequisites = { TEXT("pie") };
}

FCapabilityResult FGetRuntimeActorBehaviorTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FCapabilityResult R;

	UWorld* World = FNexusRuntimeUtils::GetActiveWorld();
	if (!World)
	{
		EmitError(R.Entries, {}, TEXT("无活动 World"));
		return R;
	}

	FString ActorName, NameFilter;
	Arguments->TryGetStringField(TEXT("actorName"), ActorName);
	Arguments->TryGetStringField(TEXT("nameFilter"), NameFilter);

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
		EmitError(R.Entries, {{TEXT("actorName"), ActorName}}, TEXT("AIController 未找到"));
		return R;
	}

	UBehaviorTreeComponent* BTComp = AICtrl->FindComponentByClass<UBehaviorTreeComponent>();
	if (!BTComp)
	{
		EmitError(R.Entries, {{TEXT("actorName"), ActorName}}, TEXT("No BehaviorTreeComponent on AIController"));
		return R;
	}

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	if (!ActorName.IsEmpty())
		Entry->SetStringField(TEXT("actorName"), ActorName);
	Entry->SetStringField(TEXT("controller"), AICtrl->GetName());

	if (const UBehaviorTree* BTAsset = BTComp->GetCurrentTree())
		Entry->SetStringField(TEXT("behaviorTree"), BTAsset->GetName());
	if (const UBTNode* ActiveNode = BTComp->GetActiveNode())
		Entry->SetStringField(TEXT("activeNode"), ActiveNode->GetNodeName());

	if (UBlackboardComponent* BBComp = AICtrl->GetBlackboardComponent())
	{
		if (const UBlackboardData* BBData = BBComp->GetBlackboardAsset())
		{
			TArray<TSharedPtr<FJsonValue>> Keys;
			for (const FBlackboardEntry& E : BBData->Keys)
			{
				if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(E.EntryName.ToString(), NameFilter))
					continue;
				const uint16 KeyID = BBComp->GetKeyID(E.EntryName);
				TSharedPtr<FJsonObject> KObj = MakeShared<FJsonObject>();
				KObj->SetStringField(TEXT("name"), E.EntryName.ToString());
				if (E.KeyType)
					KObj->SetStringField(TEXT("type"), E.KeyType->GetClass()->GetName());
				KObj->SetStringField(TEXT("value"), BBComp->DescribeKeyValue(KeyID, EBlackboardDescription::OnlyValue));
				Keys.Add(MakeShared<FJsonValueObject>(KObj));
			}
			Entry->SetArrayField(TEXT("blackboardKeys"), Keys);
		}
	}

	R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
	return R;
}

REGISTER_MCP_CAPABILITY(FGetRuntimeActorBehaviorTreeCapability)

