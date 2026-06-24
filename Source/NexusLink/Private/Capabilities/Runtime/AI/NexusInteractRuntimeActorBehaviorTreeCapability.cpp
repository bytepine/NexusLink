// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/AI/NexusInteractRuntimeActorBehaviorTreeCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "NexusMcpTool.h"

void FInteractRuntimeActorBehaviorTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("interact_runtime_actor_behavior_tree");
	Out.Description = TEXT("运行时写 BT。action=set_blackboard|restart_tree|stop_tree。按 AIController 定位。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),     FNexusSchema::Enum(TEXT("写操作"),
			{ TEXT("set_blackboard"), TEXT("restart_tree"), TEXT("stop_tree") }))
		.Prop(TEXT("actorName"),  FNexusSchema::Str(TEXT("Controller 或 Pawn 名（可选；省略取首个 AIController）")))
		.Prop(TEXT("keyName"),    FNexusSchema::Str(TEXT("黑板键名（set_blackboard）")))
		.Prop(TEXT("value"),      FNexusSchema::Str(TEXT("键值字符串（set_blackboard）")))
		.Prop(TEXT("treePath"),   FNexusSchema::Str(TEXT("BT 资产路径（restart_tree 可选；省略则重启当前树）")))
		.Required({ TEXT("action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("blackboard"), TEXT("bt"), TEXT("aicontroller"), TEXT("ai"), TEXT("behavior") };
	Out.RelatedCapabilities = { TEXT("get_runtime_actor_behavior_tree"), TEXT("get_asset_behavior_tree") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("PIE 中修改黑板值、重启/停止行为树");
}

/** 定位 AIController（与 GetRuntimeActorBehaviorTree 相同逻辑）*/
static AAIController* FindAIControllerForInteract(UWorld* World, const FString& ActorName)
{
	if (ActorName.IsEmpty())
	{
		for (TActorIterator<AAIController> It(World); It; ++It) return *It;
		return nullptr;
	}
	AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
	if (!Actor) return nullptr;
	if (AAIController* AIC = Cast<AAIController>(Actor)) return AIC;
	if (APawn* P = Cast<APawn>(Actor)) return Cast<AAIController>(P->GetController());
	return nullptr;
}

/** 用类名字符串判断键类型（避免跨版本 KeyType 头文件路径差异）*/
static FString GetKeyTypeName(const UBlackboardKeyType* KeyType)
{
	if (!KeyType) return TEXT("");
	return KeyType->GetClass()->GetName();
}

FCapabilityResult FInteractRuntimeActorBehaviorTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString Action;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{
			OutError = TEXT("缺少 action");
			return;
		}

		UWorld* World = FNexusRuntimeUtils::GetActiveWorld();
		if (!World) { OutError = TEXT("无活动 World"); return; }

		FString ActorName, KeyName, KeyValue, TreePath;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("actorName"), ActorName);
			Arguments->TryGetStringField(TEXT("keyName"),   KeyName);
			Arguments->TryGetStringField(TEXT("value"),     KeyValue);
			Arguments->TryGetStringField(TEXT("treePath"),  TreePath);
		}

		AAIController* AICtrl = FindAIControllerForInteract(World, ActorName);
		if (!AICtrl)
		{
			OutError = TEXT("AIController 未找到");
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (!ActorName.IsEmpty()) Entry->SetStringField(TEXT("actorName"), ActorName);
		Entry->SetStringField(TEXT("controller"), AICtrl->GetName());
		Entry->SetStringField(TEXT("action"), Action);

		if (Action.Equals(TEXT("set_blackboard"), ESearchCase::IgnoreCase))
		{
			if (KeyName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_blackboard 需要 keyName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UBlackboardComponent* BBComp = AICtrl->GetBlackboardComponent();
			if (!BBComp)
			{
				Entry->SetStringField(TEXT("error"), TEXT("AIController 无 BlackboardComponent"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			// 检查键是否存在（遍历 BBData->Keys，避免 GetKeyID 跨版本 API 差异）
			bool bKeyFound = false;
			if (const UBlackboardData* BBData = BBComp->GetBlackboardAsset())
			{
				for (const FBlackboardEntry& E : BBData->Keys)
				{
					if (E.EntryName == FName(*KeyName)) { bKeyFound = true; break; }
				}
			}
			if (!bKeyFound)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("黑板键不存在: %s"), *KeyName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}

			// 获取键类型信息以推断值类型
			if (const UBlackboardData* BBData = BBComp->GetBlackboardAsset())
			{
				for (const FBlackboardEntry& E : BBData->Keys)
				{
					if (E.EntryName != FName(*KeyName) || !E.KeyType) continue;

					const FString TypeStr = GetKeyTypeName(E.KeyType);

					// 用类名字符串判断，避免引入跨版本 KeyType 头文件
					if (TypeStr.Contains(TEXT("Object")))
					{
						// 对象类型：按路径加载
						UObject* Obj = !KeyValue.IsEmpty() ? LoadObject<UObject>(nullptr, *KeyValue) : nullptr;
						BBComp->SetValueAsObject(FName(*KeyName), Obj);
						Entry->SetStringField(TEXT("typeName"), TEXT("Object"));
						if (Obj) Entry->SetStringField(TEXT("objectClass"), Obj->GetClass()->GetName());
					}
					else if (TypeStr.Contains(TEXT("Enum")))
					{
						BBComp->SetValueAsEnum(FName(*KeyName), static_cast<uint8>(FCString::Atoi(*KeyValue)));
						Entry->SetStringField(TEXT("typeName"), TEXT("Enum"));
						Entry->SetNumberField(TEXT("enumValue"), FCString::Atoi(*KeyValue));
					}
					else if (TypeStr.Contains(TEXT("Float")))
					{
						BBComp->SetValueAsFloat(FName(*KeyName), FCString::Atof(*KeyValue));
						Entry->SetStringField(TEXT("typeName"), TEXT("Float"));
						Entry->SetNumberField(TEXT("floatValue"), FCString::Atof(*KeyValue));
					}
					else if (TypeStr.Contains(TEXT("Int")))
					{
						BBComp->SetValueAsInt(FName(*KeyName), FCString::Atoi(*KeyValue));
						Entry->SetStringField(TEXT("typeName"), TEXT("Int"));
						Entry->SetNumberField(TEXT("intValue"), FCString::Atoi(*KeyValue));
					}
					else if (TypeStr.Contains(TEXT("Bool")))
					{
						const bool bVal = KeyValue.ToBool();
						BBComp->SetValueAsBool(FName(*KeyName), bVal);
						Entry->SetStringField(TEXT("typeName"), TEXT("Bool"));
						Entry->SetBoolField(TEXT("boolValue"), bVal);
					}
					else if (TypeStr.Contains(TEXT("String")))
					{
						BBComp->SetValueAsString(FName(*KeyName), KeyValue);
						Entry->SetStringField(TEXT("typeName"), TEXT("String"));
						Entry->SetStringField(TEXT("stringValue"), KeyValue);
					}
					else if (TypeStr.Contains(TEXT("Name")))
					{
						BBComp->SetValueAsName(FName(*KeyName), FName(*KeyValue));
						Entry->SetStringField(TEXT("typeName"), TEXT("Name"));
						Entry->SetStringField(TEXT("nameValue"), KeyValue);
					}
					else if (TypeStr.Contains(TEXT("Vector")))
					{
						FVector Vec = FVector::ZeroVector;
						Vec.InitFromString(KeyValue);
						BBComp->SetValueAsVector(FName(*KeyName), Vec);
						Entry->SetStringField(TEXT("typeName"), TEXT("Vector"));
						Entry->SetStringField(TEXT("vectorValue"), Vec.ToString());
					}
					else if (TypeStr.Contains(TEXT("Rotator")))
					{
						FRotator Rot = FRotator::ZeroRotator;
						Rot.InitFromString(KeyValue);
						BBComp->SetValueAsRotator(FName(*KeyName), Rot);
						Entry->SetStringField(TEXT("typeName"), TEXT("Rotator"));
						Entry->SetStringField(TEXT("rotatorValue"), Rot.ToString());
					}
					else
					{
						Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的黑板键类型: %s"), *TypeStr));
					}
					break;
				}
			}
			Entry->SetStringField(TEXT("keyName"), KeyName);
			Entry->SetBoolField(TEXT("success"), !Entry->HasField(TEXT("error")));
		}
		else if (Action.Equals(TEXT("restart_tree"), ESearchCase::IgnoreCase))
		{
			UBehaviorTreeComponent* BTComp = AICtrl->FindComponentByClass<UBehaviorTreeComponent>();
			if (!BTComp)
			{
				Entry->SetStringField(TEXT("error"), TEXT("AIController 无 BehaviorTreeComponent"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (!TreePath.IsEmpty())
			{
				UBehaviorTree* BTAsset = LoadObject<UBehaviorTree>(nullptr, *TreePath);
				if (!BTAsset)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("BT 资产加载失败: %s"), *TreePath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}
				BTComp->StartTree(*BTAsset, EBTExecutionMode::Looped);
				Entry->SetStringField(TEXT("behaviorTree"), BTAsset->GetName());
			}
			else
			{
				if (const UBehaviorTree* CurrentTree = BTComp->GetCurrentTree())
				{
					BTComp->RestartTree();
					Entry->SetStringField(TEXT("behaviorTree"), CurrentTree->GetName());
				}
				else
				{
					Entry->SetStringField(TEXT("error"), TEXT("无活动行为树可重启"));
				}
			}
			Entry->SetBoolField(TEXT("restarted"), !Entry->HasField(TEXT("error")));
		}
		else if (Action.Equals(TEXT("stop_tree"), ESearchCase::IgnoreCase))
		{
			UBehaviorTreeComponent* BTComp = AICtrl->FindComponentByClass<UBehaviorTreeComponent>();
			if (!BTComp)
			{
				Entry->SetStringField(TEXT("error"), TEXT("AIController 无 BehaviorTreeComponent"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (const UBehaviorTree* CurrentTree = BTComp->GetCurrentTree())
			{
				Entry->SetStringField(TEXT("behaviorTree"), CurrentTree->GetName());
			}
			BTComp->StopTree(EBTStopMode::Safe);
			Entry->SetBoolField(TEXT("stopped"), true);
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FInteractRuntimeActorBehaviorTreeCapability)
