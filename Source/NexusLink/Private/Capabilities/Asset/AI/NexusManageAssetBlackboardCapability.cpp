// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusManageAssetBlackboardCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "NexusMcpTool.h"

/** BT 路径或 BB 路径均可加载到 BlackboardData */
static UBlackboardData* LoadBlackboardFromPath(const FString& AssetPath)
{
	if (UBehaviorTree* BT = FNexusAssetUtils::LoadAssetWithFallback<UBehaviorTree>(AssetPath))
	{
		return BT->BlackboardAsset;
	}
	return FNexusAssetUtils::LoadAssetWithFallback<UBlackboardData>(AssetPath);
}

/** 根据类型字符串构造对应的 BlackboardKeyType 实例 */
static UBlackboardKeyType* CreateBBKeyType(const FString& TypeStr, UBlackboardData* Outer)
{
	const FString Lower = TypeStr.ToLower();
	if (Lower == TEXT("bool"))    return NewObject<UBlackboardKeyType_Bool>(Outer);
	if (Lower == TEXT("float"))   return NewObject<UBlackboardKeyType_Float>(Outer);
	if (Lower == TEXT("int"))     return NewObject<UBlackboardKeyType_Int>(Outer);
	if (Lower == TEXT("string"))  return NewObject<UBlackboardKeyType_String>(Outer);
	if (Lower == TEXT("name"))    return NewObject<UBlackboardKeyType_Name>(Outer);
	if (Lower == TEXT("vector"))  return NewObject<UBlackboardKeyType_Vector>(Outer);
	if (Lower == TEXT("rotator")) return NewObject<UBlackboardKeyType_Rotator>(Outer);
	if (Lower == TEXT("object"))  return NewObject<UBlackboardKeyType_Object>(Outer);
	if (Lower == TEXT("class"))   return NewObject<UBlackboardKeyType_Class>(Outer);
	if (Lower == TEXT("enum"))    return NewObject<UBlackboardKeyType_Enum>(Outer);
	return nullptr;
}

void FManageAssetBlackboardCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_blackboard");
	Out.Description = TEXT("批量编辑 BB 键：增删/重命名/改父 BB；须 save_asset。");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),  FNexusSchema::Enum(TEXT("键操作"), { TEXT("add"), TEXT("remove"), TEXT("rename"), TEXT("set_parent") }))
		.Prop(TEXT("keyName"), FNexusSchema::Str(TEXT("键名（set_parent 不需要）")))
		.Prop(TEXT("keyType"), FNexusSchema::Enum(TEXT("键类型（仅 add）"),
		{ TEXT("bool"), TEXT("float"), TEXT("int"), TEXT("string"), TEXT("name"),
		  TEXT("vector"), TEXT("rotator"), TEXT("object"), TEXT("class"), TEXT("enum") }))
		.Prop(TEXT("newName"), FNexusSchema::Str(TEXT("新键名（仅 rename）")))
		.Prop(TEXT("parentPath"), FNexusSchema::Str(TEXT("父 BlackboardData 路径（仅 set_parent，空则清除）")))
		.Required({ TEXT("action") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("BlackboardData 或 BehaviorTree 资产路径")))
		.Prop(TEXT("keys"),      FNexusSchema::ArrayOf(TEXT("批量键操作"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("keys") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("blackboard"), TEXT("bb"), TEXT("key"), TEXT("ai"), TEXT("parent")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_blackboard"), TEXT("create_asset_blackboard"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("写操作：增删/重命名 BB 键、改父 BB");
}

FCapabilityResult FManageAssetBlackboardCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		const FString AssetPath = Arguments->HasField(TEXT("assetPath")) ? Arguments->GetStringField(TEXT("assetPath")) : TEXT("");
		if (AssetPath.IsEmpty()) { OutError = TEXT("assetPath 为必填项"); return; }

		UBlackboardData* BB = LoadBlackboardFromPath(AssetPath);
		if (!BB) { OutError = FString::Printf(TEXT("BlackboardData 未找到: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>>* KeysArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("keys"), KeysArr) || !KeysArr)
		{
			OutError = TEXT("缺少 keys");
			return;
		}

		for (const TSharedPtr<FJsonValue>& Val : *KeysArr)
		{
			TSharedPtr<FJsonObject> Item    = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("无效的 key 项"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FString Action  = Item->HasField(TEXT("action"))  ? Item->GetStringField(TEXT("action")).ToLower() : TEXT("");
			const FString KeyName = Item->HasField(TEXT("keyName")) ? Item->GetStringField(TEXT("keyName"))          : TEXT("");
		OutEntry->SetStringField(TEXT("action"),  Action);
		OutEntry->SetStringField(TEXT("keyName"), KeyName);

			if (Action.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("缺少 action"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			if (Action != TEXT("set_parent") && KeyName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("此操作需要 keyName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			if (Action == TEXT("add"))
			{
				// 检查同名 key 是否已存在（含父 BB 的继承 keys）
				bool bExists = false;
				for (const FBlackboardEntry& E : BB->Keys)
				{
					if (E.EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase)) { bExists = true; break; }
				}
				// 迭代父 BB 链中的 keys
				if (!bExists && BB->Parent)
				{
					for (const UBlackboardData* Cur = BB->Parent; Cur; Cur = Cur->Parent)
					{
						for (const FBlackboardEntry& E : Cur->Keys)
						{
							if (E.EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase)) { bExists = true; break; }
						}
						if (bExists) break;
					}
				}

				if (bExists)
				{
					OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Key '%s' already exists"), *KeyName));
				}
				else
				{
					const FString TypeStr = Item->HasField(TEXT("keyType")) ? Item->GetStringField(TEXT("keyType")) : TEXT("");
					if (TypeStr.IsEmpty())
					{
						OutEntry->SetStringField(TEXT("error"), TEXT("action=add 时 keyType 必填"));
					}
					else
					{
						UBlackboardKeyType* KeyType = CreateBBKeyType(TypeStr, BB);
						if (!KeyType)
						{
							OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown keyType: '%s'"), *TypeStr));
						}
						else
						{
							FBlackboardEntry NewEntry;
							NewEntry.EntryName = FName(*KeyName);
							NewEntry.KeyType   = KeyType;
							BB->Keys.Add(NewEntry);
							BB->MarkPackageDirty();
							OutEntry->SetStringField(TEXT("keyType"), TypeStr);
						}
					}
				}
			}
			else if (Action == TEXT("remove"))
			{
				const int32 Removed = BB->Keys.RemoveAll([&KeyName](const FBlackboardEntry& E)
				{
					return E.EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase);
				});
				if (Removed == 0)
				{
					OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("用户定义键中未找到 '%s'"), *KeyName));
				}
				else
				{
					BB->MarkPackageDirty();
				}
			}
			else if (Action == TEXT("rename"))
			{
				const FString NewName = Item->HasField(TEXT("newName")) ? Item->GetStringField(TEXT("newName")) : TEXT("");
				if (NewName.IsEmpty())
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("action=rename 时 newName 必填"));
				}
				else
				{
					bool bFound = false;
					for (FBlackboardEntry& E : BB->Keys)
					{
						if (E.EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase))
						{
							E.EntryName = FName(*NewName);
							bFound      = true;
							break;
						}
					}
					if (!bFound)
					{
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("用户定义键中未找到 '%s'"), *KeyName));
					}
					else
					{
						BB->MarkPackageDirty();
						OutEntry->SetStringField(TEXT("newName"), NewName);
					}
				}
			}
			else if (Action == TEXT("set_parent"))
			{
				const FString ParentPath = Item->HasField(TEXT("parentPath")) ? Item->GetStringField(TEXT("parentPath")) : TEXT("");
				if (ParentPath.IsEmpty())
				{
					// 清除 parent
					BB->Parent = nullptr;
					BB->MarkPackageDirty();
					OutEntry->SetStringField(TEXT("parentPath"), TEXT("(cleared)"));
				}
				else
				{
					UBlackboardData* ParentBB = FNexusAssetUtils::LoadAssetWithFallback<UBlackboardData>(ParentPath);
					if (!ParentBB)
					{
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("父 BlackboardData 未找到: %s"), *ParentPath));
					}
					else if (ParentBB == BB)
					{
						OutEntry->SetStringField(TEXT("error"), TEXT("不能将自身设为父级"));
					}
					else
					{
						BB->Parent = ParentBB;
						BB->MarkPackageDirty();
						OutEntry->SetStringField(TEXT("parentPath"), ParentPath);
					}
				}
			}
			else
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}
	
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetBlackboardCapability)
