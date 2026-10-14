// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/AI/NexusInteractRuntimeActorBehaviorTreeCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
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

void FNexusInteractRuntimeActorBehaviorTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractRuntimeActorBehaviorTree");
	Out.Description = TEXT("Write runtime BT. Action=SetBlackboard|RestartTree|StopTree. Locate by AIController.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),     FNexusSchema::Enum(TEXT("Write operation"),
			{ TEXT("SetBlackboard"), TEXT("RestartTree"), TEXT("StopTree") }))
		.Prop(TEXT("ActorName"),  FNexusSchema::Str(TEXT("Controller or Pawn name (optional; first AIController if omitted)")))
		.Prop(TEXT("KeyName"),    FNexusSchema::Str(TEXT("Blackboard key name (SetBlackboard)")))
		.Prop(TEXT("Value"),      FNexusSchema::Str(TEXT("Key value string (SetBlackboard)")))
		.Prop(TEXT("TreePath"),   FNexusSchema::Str(TEXT("BT asset path (RestartTree optional; restarts current if omitted)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("Blackboard"), TEXT("bt"), TEXT("aicontroller"), TEXT("ai"), TEXT("behavior") };
	Out.RelatedCapabilities = { TEXT("GetRuntimeActorBehaviorTree"), TEXT("GetAssetBehaviorTree") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Modify blackboard, restart/stop BT in PIE");
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

FCapabilityResult FNexusInteractRuntimeActorBehaviorTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Action = A.Str(TEXT("Action"));

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) { return; }

		FString ActorName, KeyName, KeyValue, TreePath;
		if (Arguments.IsValid())
		{
			ActorName = FNexusArgs(Arguments).Str(TEXT("ActorName"), ActorName);
			KeyName = FNexusArgs(Arguments).Str(TEXT("KeyName"), KeyName);
			KeyValue = FNexusArgs(Arguments).Str(TEXT("Value"), KeyValue);
			TreePath = FNexusArgs(Arguments).Str(TEXT("TreePath"), TreePath);
		}

		AAIController* AICtrl = FindAIControllerForInteract(World, ActorName);
		if (!AICtrl)
		{
			OutError = TEXT("AIController not found");
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (!ActorName.IsEmpty()) Entry->SetStringField(TEXT("ActorName"), ActorName);
		Entry->SetStringField(TEXT("Controller"), AICtrl->GetName());
		Entry->SetStringField(TEXT("Action"), Action);

		if (Action.Equals(TEXT("SetBlackboard"), ESearchCase::IgnoreCase))
		{
			if (KeyName.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("SetBlackboard requires KeyName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UBlackboardComponent* BBComp = AICtrl->GetBlackboardComponent();
			if (!BBComp)
			{
				Entry->SetStringField(TEXT("Error"), TEXT("AIController has no BlackboardComponent"));
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
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Blackboard key does not exist: %s"), *KeyName));
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
						Entry->SetStringField(TEXT("TypeName"), TEXT("Object"));
						if (Obj) Entry->SetStringField(TEXT("ObjectClass"), Obj->GetClass()->GetName());
					}
					else if (TypeStr.Contains(TEXT("Enum")))
					{
						BBComp->SetValueAsEnum(FName(*KeyName), static_cast<uint8>(FCString::Atoi(*KeyValue)));
						Entry->SetStringField(TEXT("TypeName"), TEXT("Enum"));
						Entry->SetNumberField(TEXT("EnumValue"), FCString::Atoi(*KeyValue));
					}
					else if (TypeStr.Contains(TEXT("Float")))
					{
						BBComp->SetValueAsFloat(FName(*KeyName), FCString::Atof(*KeyValue));
						Entry->SetStringField(TEXT("TypeName"), TEXT("Float"));
						Entry->SetNumberField(TEXT("FloatValue"), FCString::Atof(*KeyValue));
					}
					else if (TypeStr.Contains(TEXT("Int")))
					{
						BBComp->SetValueAsInt(FName(*KeyName), FCString::Atoi(*KeyValue));
						Entry->SetStringField(TEXT("TypeName"), TEXT("Int"));
						Entry->SetNumberField(TEXT("IntValue"), FCString::Atoi(*KeyValue));
					}
					else if (TypeStr.Contains(TEXT("Bool")))
					{
						const bool bVal = KeyValue.ToBool();
						BBComp->SetValueAsBool(FName(*KeyName), bVal);
						Entry->SetStringField(TEXT("TypeName"), TEXT("Bool"));
						Entry->SetBoolField(TEXT("bBoolValue"), bVal);
					}
					else if (TypeStr.Contains(TEXT("String")))
					{
						BBComp->SetValueAsString(FName(*KeyName), KeyValue);
						Entry->SetStringField(TEXT("TypeName"), TEXT("String"));
						Entry->SetStringField(TEXT("StringValue"), KeyValue);
					}
					else if (TypeStr.Contains(TEXT("Name")))
					{
						BBComp->SetValueAsName(FName(*KeyName), FName(*KeyValue));
						Entry->SetStringField(TEXT("TypeName"), TEXT("Name"));
						Entry->SetStringField(TEXT("NameValue"), KeyValue);
					}
					else if (TypeStr.Contains(TEXT("Vector")))
					{
						FVector Vec = FVector::ZeroVector;
						Vec.InitFromString(KeyValue);
						BBComp->SetValueAsVector(FName(*KeyName), Vec);
						Entry->SetStringField(TEXT("TypeName"), TEXT("Vector"));
						Entry->SetStringField(TEXT("VectorValue"), Vec.ToString());
					}
					else if (TypeStr.Contains(TEXT("Rotator")))
					{
						FRotator Rot = FRotator::ZeroRotator;
						Rot.InitFromString(KeyValue);
						BBComp->SetValueAsRotator(FName(*KeyName), Rot);
						Entry->SetStringField(TEXT("TypeName"), TEXT("Rotator"));
						Entry->SetStringField(TEXT("RotatorValue"), Rot.ToString());
					}
					else
					{
						Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unsupported blackboard key type: %s"), *TypeStr));
					}
					break;
				}
			}
			Entry->SetStringField(TEXT("KeyName"), KeyName);
			Entry->SetBoolField(TEXT("bSuccess"), !Entry->HasField(TEXT("Error")));
		}
		else if (Action.Equals(TEXT("RestartTree"), ESearchCase::IgnoreCase))
		{
			UBehaviorTreeComponent* BTComp = AICtrl->FindComponentByClass<UBehaviorTreeComponent>();
			if (!BTComp)
			{
				Entry->SetStringField(TEXT("Error"), TEXT("AIController has no BehaviorTreeComponent"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (!TreePath.IsEmpty())
			{
				UBehaviorTree* BTAsset = LoadObject<UBehaviorTree>(nullptr, *TreePath);
				if (!BTAsset)
				{
					Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to load BT asset: %s"), *TreePath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}
				BTComp->StartTree(*BTAsset, EBTExecutionMode::Looped);
				Entry->SetStringField(TEXT("BehaviorTree"), BTAsset->GetName());
			}
			else
			{
				if (const UBehaviorTree* CurrentTree = BTComp->GetCurrentTree())
				{
					BTComp->RestartTree();
					Entry->SetStringField(TEXT("BehaviorTree"), CurrentTree->GetName());
				}
				else
				{
					Entry->SetStringField(TEXT("Error"), TEXT("No active behavior tree to restart"));
				}
			}
			Entry->SetBoolField(TEXT("bRestarted"), !Entry->HasField(TEXT("Error")));
		}
		else if (Action.Equals(TEXT("StopTree"), ESearchCase::IgnoreCase))
		{
			UBehaviorTreeComponent* BTComp = AICtrl->FindComponentByClass<UBehaviorTreeComponent>();
			if (!BTComp)
			{
				Entry->SetStringField(TEXT("Error"), TEXT("AIController has no BehaviorTreeComponent"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			if (const UBehaviorTree* CurrentTree = BTComp->GetCurrentTree())
			{
				Entry->SetStringField(TEXT("BehaviorTree"), CurrentTree->GetName());
			}
			BTComp->StopTree(EBTStopMode::Safe);
			Entry->SetBoolField(TEXT("bStopped"), true);
		}
		else
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusInteractRuntimeActorBehaviorTreeCapability)
