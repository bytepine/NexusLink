// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusManageAssetBlackboardCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
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

static UBlackboardData* BBFrom(FNexusActionContext& Ctx)
{
	return static_cast<UBlackboardData*>(Ctx.Target);
}

static bool RequireBBKeyName(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx, FString& OutKeyName)
{
	OutKeyName = FNexusArgs(Op).Str(TEXT("keyName"));
	Ctx.Entry->SetStringField(TEXT("keyName"), OutKeyName);
	if (OutKeyName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("This operation requires keyName"));
		return false;
	}
	return true;
}

static void HandleBB_Add(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBlackboardData* BB = BBFrom(Ctx);
	FString KeyName;
	if (!RequireBBKeyName(Op, Ctx, KeyName)) return;

	bool bExists = false;
	for (const FBlackboardEntry& E : BB->Keys)
	{
		if (E.EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase)) { bExists = true; break; }
	}
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
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Key '%s' already exists"), *KeyName));
		return;
	}

	const FString TypeStr = FNexusArgs(Op).Str(TEXT("keyType"));
	if (TypeStr.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("keyType is required when action=add"));
		return;
	}
	UBlackboardKeyType* KeyType = CreateBBKeyType(TypeStr, BB);
	if (!KeyType)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown keyType: '%s'"), *TypeStr));
		return;
	}
	FBlackboardEntry NewEntry;
	NewEntry.EntryName = FName(*KeyName);
	NewEntry.KeyType   = KeyType;
	BB->Keys.Add(NewEntry);
	BB->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("keyType"), TypeStr);
}

static void HandleBB_Remove(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBlackboardData* BB = BBFrom(Ctx);
	FString KeyName;
	if (!RequireBBKeyName(Op, Ctx, KeyName)) return;

	const int32 Removed = BB->Keys.RemoveAll([&KeyName](const FBlackboardEntry& E)
	{
		return E.EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase);
	});
	if (Removed == 0)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Key '%s' not found in user-defined keys"), *KeyName));
	}
	else
	{
		BB->MarkPackageDirty();
	}
}

static void HandleBB_Rename(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBlackboardData* BB = BBFrom(Ctx);
	FString KeyName;
	if (!RequireBBKeyName(Op, Ctx, KeyName)) return;

	const FString NewName = FNexusArgs(Op).Str(TEXT("newName"));
	if (NewName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("newName is required when action=rename"));
		return;
	}
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
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Key '%s' not found in user-defined keys"), *KeyName));
		return;
	}
	BB->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("newName"), NewName);
}

static void HandleBB_SetParent(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBlackboardData* BB = BBFrom(Ctx);
	Ctx.Entry->SetStringField(TEXT("keyName"), FNexusArgs(Op).Str(TEXT("keyName")));
	const FString ParentPath = FNexusArgs(Op).Str(TEXT("parentPath"));
	if (ParentPath.IsEmpty())
	{
		BB->Parent = nullptr;
		BB->MarkPackageDirty();
		Ctx.Entry->SetStringField(TEXT("parentPath"), TEXT("(cleared)"));
		return;
	}
	UBlackboardData* ParentBB = FNexusAssetUtils::LoadAssetWithFallback<UBlackboardData>(ParentPath);
	if (!ParentBB)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent BlackboardData not found: %s"), *ParentPath));
		return;
	}
	if (ParentBB == BB)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Cannot set self as parent"));
		return;
	}
	BB->Parent = ParentBB;
	BB->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("parentPath"), ParentPath);
}

bool FManageAssetBlackboardCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UBlackboardData* BB = LoadBlackboardFromPath(AssetPath);
	if (!BB)
	{
		OutError = FString::Printf(TEXT("BlackboardData not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = BB;
	return true;
}

void FManageAssetBlackboardCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add"),        &HandleBB_Add);
	OutHandlers.Add(TEXT("remove"),     &HandleBB_Remove);
	OutHandlers.Add(TEXT("rename"),     &HandleBB_Rename);
	OutHandlers.Add(TEXT("set_parent"), &HandleBB_SetParent);
}

REGISTER_MCP_CAPABILITY(FManageAssetBlackboardCapability)
