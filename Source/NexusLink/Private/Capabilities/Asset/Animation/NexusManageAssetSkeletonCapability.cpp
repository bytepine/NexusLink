// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetSkeletonCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "NexusMcpTool.h"

void FManageAssetSkeletonCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_skeleton");
	Out.SearchAssetTypes = {TEXT("Skeleton")};
	Out.Description = TEXT("批量编辑 Skeleton Socket。operations[].action=add_socket|remove_socket|modify_socket。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Socket 操作"),
			{ TEXT("add_socket"), TEXT("remove_socket"), TEXT("modify_socket") }))
		.Prop(TEXT("socketName"),   FNexusSchema::Str(TEXT("Socket 名")))
		.Prop(TEXT("boneName"),     FNexusSchema::Str(TEXT("挂载骨骼名（add/modify）")))
		.Prop(TEXT("location"),     FNexusSchema::Str(TEXT("位置 X,Y,Z（add/modify）")))
		.Prop(TEXT("rotation"),     FNexusSchema::Str(TEXT("旋转 P,Y,R（add/modify）")))
		.Prop(TEXT("scale"),        FNexusSchema::Str(TEXT("缩放 X,Y,Z（add/modify）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Skeleton 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量 Socket 操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("socket"), TEXT("bone"), TEXT("rig"), TEXT("attach") };
	Out.RelatedCapabilities = { TEXT("get_asset_skeleton"), TEXT("get_asset_skeletal_mesh") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("增删改 Skeleton Socket；修改后需 save_asset 落盘");
}

/** 解析 "X,Y,Z" 字符串为 FVector */
static bool ParseVec3(const FString& Str, FVector& OutVec)
{
	if (Str.IsEmpty()) return false;
	OutVec.InitFromString(Str);
	return true;
}

/** 解析 "P,Y,R" 字符串为 FRotator */
static bool NxParseRotatorStr(const FString& Str, FRotator& OutRot)
{
	if (Str.IsEmpty()) return false;
	OutRot.InitFromString(Str);
	return true;
}

FCapabilityResult FManageAssetSkeletonCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		USkeleton* Skeleton = FNexusAssetUtils::LoadAssetWithFallback<USkeleton>(AssetPath);
		if (!Skeleton)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("Skeleton 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			FString SocketName, BoneName, LocStr, RotStr, ScaleStr;
			Op->TryGetStringField(TEXT("socketName"), SocketName);
			Op->TryGetStringField(TEXT("boneName"),   BoneName);
			Op->TryGetStringField(TEXT("location"),   LocStr);
			Op->TryGetStringField(TEXT("rotation"),   RotStr);
			Op->TryGetStringField(TEXT("scale"),      ScaleStr);

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);

			if (Action.Equals(TEXT("add_socket"), ESearchCase::IgnoreCase))
			{
				if (SocketName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("add_socket 需要 socketName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				// 检查重名
				bool bDup = false;
				for (const USkeletalMeshSocket* Sock : Skeleton->Sockets)
				{
					if (Sock && Sock->SocketName == FName(*SocketName))
					{
						Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket 已存在: %s"), *SocketName));
						bDup = true;
						break;
					}
				}
				if (bDup) { OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); continue; }

				USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(Skeleton);
				NewSocket->SocketName = FName(*SocketName);
				if (!BoneName.IsEmpty()) NewSocket->BoneName = FName(*BoneName);
				FVector Loc = FVector::ZeroVector;
				if (ParseVec3(LocStr, Loc)) NewSocket->RelativeLocation = Loc;
				FRotator Rot = FRotator::ZeroRotator;
				if (NxParseRotatorStr(RotStr, Rot)) NewSocket->RelativeRotation = Rot;
				FVector Scale = FVector::OneVector;
				if (ParseVec3(ScaleStr, Scale)) NewSocket->RelativeScale = Scale;

				Skeleton->Sockets.Add(NewSocket);
				bDirty = true;

				Entry->SetStringField(TEXT("socketName"), SocketName);
				Entry->SetStringField(TEXT("boneName"), NewSocket->BoneName.ToString());
			}
			else if (Action.Equals(TEXT("remove_socket"), ESearchCase::IgnoreCase))
			{
				if (SocketName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("remove_socket 需要 socketName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const FName TargetName(*SocketName);
				bool bFound = false;
				for (int32 i = Skeleton->Sockets.Num() - 1; i >= 0; --i)
				{
					if (Skeleton->Sockets[i] && Skeleton->Sockets[i]->SocketName == TargetName)
					{
						Skeleton->Sockets.RemoveAt(i);
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket 未找到: %s"), *SocketName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				bDirty = true;
				Entry->SetStringField(TEXT("socketName"), SocketName);
				Entry->SetBoolField(TEXT("removed"), true);
			}
			else if (Action.Equals(TEXT("modify_socket"), ESearchCase::IgnoreCase))
			{
				if (SocketName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("modify_socket 需要 socketName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const FName TargetName(*SocketName);
				USkeletalMeshSocket* TargetSocket = nullptr;
				for (USkeletalMeshSocket* Sock : Skeleton->Sockets)
				{
					if (Sock && Sock->SocketName == TargetName)
					{
						TargetSocket = Sock;
						break;
					}
				}
				if (!TargetSocket)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket 未找到: %s"), *SocketName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				// 按需修改字段
				if (!BoneName.IsEmpty()) TargetSocket->BoneName = FName(*BoneName);
				FVector Loc; if (ParseVec3(LocStr, Loc)) TargetSocket->RelativeLocation = Loc;
				FRotator Rot; if (NxParseRotatorStr(RotStr, Rot)) TargetSocket->RelativeRotation = Rot;
				FVector Scale; if (ParseVec3(ScaleStr, Scale)) TargetSocket->RelativeScale = Scale;

				bDirty = true;
				Entry->SetStringField(TEXT("socketName"), SocketName);
				Entry->SetStringField(TEXT("boneName"), TargetSocket->BoneName.ToString());
				Entry->SetBoolField(TEXT("modified"), true);
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		if (bDirty)
		{
			Skeleton->MarkPackageDirty();
			OutTop->SetStringField(TEXT("note"), TEXT("已修改，用 save_asset 落盘"));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSkeletonCapability)
