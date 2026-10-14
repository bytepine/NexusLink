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
	if (Lower == TEXT("Vector"))  return NewObject<UBlackboardKeyType_Vector>(Outer);
	if (Lower == TEXT("Rotator")) return NewObject<UBlackboardKeyType_Rotator>(Outer);
	if (Lower == TEXT("object"))  return NewObject<UBlackboardKeyType_Object>(Outer);
	if (Lower == TEXT("Class"))   return NewObject<UBlackboardKeyType_Class>(Outer);
	if (Lower == TEXT("enum"))    return NewObject<UBlackboardKeyType_Enum>(Outer);
	return nullptr;
}

void FNexusManageAssetBlackboardCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetBlackboard");
	Out.SearchAssetTypes = {TEXT("Blackboard")};
	Out.Description = TEXT("Batch edit BB keys: add/remove/rename/set parent; persist with SaveAsset.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),  FNexusSchema::Enum(TEXT("Key operation"), { TEXT("Add"), TEXT("Remove"), TEXT("Rename"), TEXT("SetParent") }))
		.Prop(TEXT("KeyName"), FNexusSchema::Str(TEXT("Key name (not needed for SetParent)")))
		.Prop(TEXT("KeyType"), FNexusSchema::Enum(TEXT("Key type (add only)"),
		{ TEXT("bool"), TEXT("float"), TEXT("int"), TEXT("string"), TEXT("name"),
		  TEXT("Vector"), TEXT("Rotator"), TEXT("object"), TEXT("Class"), TEXT("enum") }))
		.Prop(TEXT("NewName"), FNexusSchema::Str(TEXT("New key name (rename only)")))
		.Prop(TEXT("ParentPath"), FNexusSchema::Str(TEXT("Parent BlackboardData path (SetParent only; empty clears)")))
		.Required({ TEXT("Action") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("BlackboardData or BehaviorTree asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch key ops"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("Blackboard"), TEXT("bb"), TEXT("Key"), TEXT("ai"), TEXT("Parent")
	};
	Out.RelatedCapabilities = { TEXT("GetAssetBlackboard"), TEXT("CreateAssetBlackboard"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Write ops: add/remove/rename BB keys, change parent BB");
}

static UBlackboardData* BBFrom(FNexusActionContext& Ctx)
{
	return static_cast<UBlackboardData*>(Ctx.Target);
}

static bool RequireBBKeyName(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx, FString& OutKeyName)
{
	OutKeyName = FNexusArgs(Op).Str(TEXT("KeyName"));
	Ctx.Entry->SetStringField(TEXT("KeyName"), OutKeyName);
	if (OutKeyName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("This operation requires KeyName"));
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
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Key '%s' already exists"), *KeyName));
		return;
	}

	const FString TypeStr = FNexusArgs(Op).Str(TEXT("KeyType"));
	if (TypeStr.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("KeyType is required when Action=add"));
		return;
	}
	UBlackboardKeyType* KeyType = CreateBBKeyType(TypeStr, BB);
	if (!KeyType)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unknown KeyType: '%s'"), *TypeStr));
		return;
	}
	FBlackboardEntry NewEntry;
	NewEntry.EntryName = FName(*KeyName);
	NewEntry.KeyType   = KeyType;
	BB->Keys.Add(NewEntry);
	BB->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("KeyType"), TypeStr);
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
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Key '%s' not found in user-defined keys"), *KeyName));
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

	const FString NewName = FNexusArgs(Op).Str(TEXT("NewName"));
	if (NewName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("NewName is required when Action=rename"));
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
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Key '%s' not found in user-defined keys"), *KeyName));
		return;
	}
	BB->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("NewName"), NewName);
}

static void HandleBB_SetParent(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBlackboardData* BB = BBFrom(Ctx);
	Ctx.Entry->SetStringField(TEXT("KeyName"), FNexusArgs(Op).Str(TEXT("KeyName")));
	const FString ParentPath = FNexusArgs(Op).Str(TEXT("ParentPath"));
	if (ParentPath.IsEmpty())
	{
		BB->Parent = nullptr;
		BB->MarkPackageDirty();
		Ctx.Entry->SetStringField(TEXT("ParentPath"), TEXT("(cleared)"));
		return;
	}
	UBlackboardData* ParentBB = FNexusAssetUtils::LoadAssetWithFallback<UBlackboardData>(ParentPath);
	if (!ParentBB)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Parent BlackboardData not found: %s"), *ParentPath));
		return;
	}
	if (ParentBB == BB)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Cannot set self as parent"));
		return;
	}
	BB->Parent = ParentBB;
	BB->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("ParentPath"), ParentPath);
}

bool FNexusManageAssetBlackboardCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UBlackboardData* BB = LoadBlackboardFromPath(AssetPath);
	if (!BB)
	{
		OutError = FString::Printf(TEXT("BlackboardData not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = BB;
	return true;
}

void FNexusManageAssetBlackboardCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("Add"),        &HandleBB_Add);
	OutHandlers.Add(TEXT("Remove"),     &HandleBB_Remove);
	OutHandlers.Add(TEXT("Rename"),     &HandleBB_Rename);
	OutHandlers.Add(TEXT("SetParent"), &HandleBB_SetParent);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetBlackboardCapability)
