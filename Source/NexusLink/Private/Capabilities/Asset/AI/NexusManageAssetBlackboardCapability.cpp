// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusManageAssetBlackboardCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
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
	Out.SearchAssetTypes = {TEXT("Blackboard")};
	Out.Description = TEXT("Batch edit BB keys: add/remove/rename/set parent; persist with save_asset.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),  FNexusSchema::Enum(TEXT("Key operation"), { TEXT("add"), TEXT("remove"), TEXT("rename"), TEXT("set_parent") }))
		.Prop(TEXT("keyName"), FNexusSchema::Str(TEXT("Key name (not needed for set_parent)")))
		.Prop(TEXT("keyType"), FNexusSchema::Enum(TEXT("Key type (add only)"),
		{ TEXT("bool"), TEXT("float"), TEXT("int"), TEXT("string"), TEXT("name"),
		  TEXT("vector"), TEXT("rotator"), TEXT("object"), TEXT("class"), TEXT("enum") }))
		.Prop(TEXT("newName"), FNexusSchema::Str(TEXT("New key name (rename only)")))
		.Prop(TEXT("parentPath"), FNexusSchema::Str(TEXT("Parent BlackboardData path (set_parent only; empty clears)")))
		.Required({ TEXT("action") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("BlackboardData or BehaviorTree asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch key ops"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("blackboard"), TEXT("bb"), TEXT("key"), TEXT("ai"), TEXT("parent")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_blackboard"), TEXT("create_asset_blackboard"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("Write ops: add/remove/rename BB keys, change parent BB");
}

FCapabilityResult FManageAssetBlackboardCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));

		UBlackboardData* BB = LoadBlackboardFromPath(AssetPath);
		if (!BB) { OutError = FString::Printf(TEXT("BlackboardData not found: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("Missing or empty operations");
			return;
		}

		for (const TSharedPtr<FJsonValue>& Val : Ops)
		{
			TSharedPtr<FJsonObject> Item    = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Invalid key item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FString Action  = Item->HasField(TEXT("action"))  ? Item->GetStringField(TEXT("action")).ToLower() : TEXT("");
			const FString KeyName = Item->HasField(TEXT("keyName")) ? Item->GetStringField(TEXT("keyName"))          : TEXT("");
		OutEntry->SetStringField(TEXT("action"),  Action);
		OutEntry->SetStringField(TEXT("keyName"), KeyName);

			if (Action.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Missing action"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			if (Action != TEXT("set_parent") && KeyName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("This operation requires keyName"));
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
						OutEntry->SetStringField(TEXT("error"), TEXT("keyType is required when action=add"));
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
					OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Key '%s' not found in user-defined keys"), *KeyName));
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
					OutEntry->SetStringField(TEXT("error"), TEXT("newName is required when action=rename"));
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
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Key '%s' not found in user-defined keys"), *KeyName));
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
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent BlackboardData not found: %s"), *ParentPath));
					}
					else if (ParentBB == BB)
					{
						OutEntry->SetStringField(TEXT("error"), TEXT("Cannot set self as parent"));
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
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s'"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}
	
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetBlackboardCapability)
