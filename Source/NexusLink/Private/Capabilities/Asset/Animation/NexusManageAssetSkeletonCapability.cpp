// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetSkeletonCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "NexusMcpTool.h"

void FManageAssetSkeletonCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_skeleton");
	Out.Description = TEXT("编辑 Skeleton Socket。action=add_socket|remove_socket|modify_socket。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),    FNexusSchema::Str(TEXT("Skeleton 资产路径")))
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Socket 操作"),
			{ TEXT("add_socket"), TEXT("remove_socket"), TEXT("modify_socket") }))
		.Prop(TEXT("socketName"),   FNexusSchema::Str(TEXT("Socket 名")))
		.Prop(TEXT("boneName"),     FNexusSchema::Str(TEXT("挂载骨骼名（add/modify）")))
		.Prop(TEXT("location"),     FNexusSchema::Str(TEXT("位置 X,Y,Z（add/modify）")))
		.Prop(TEXT("rotation"),     FNexusSchema::Str(TEXT("旋转 P,Y,R（add/modify）")))
		.Prop(TEXT("scale"),        FNexusSchema::Str(TEXT("缩放 X,Y,Z（add/modify）")))
		.Required({ TEXT("assetPath"), TEXT("action") })
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
		FString AssetPath, Action;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		if (!FNexusCapability::RequireString(Arguments, TEXT("action"), Action, OutEntries, {{TEXT("assetPath"), AssetPath}})) return;

		USkeleton* Skeleton = FNexusAssetUtils::LoadAssetWithFallback<USkeleton>(AssetPath);
		if (!Skeleton)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("Skeleton 未找到: %s"), *AssetPath));
			return;
		}

		FString SocketName, BoneName, LocStr, RotStr, ScaleStr;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("socketName"), SocketName);
			Arguments->TryGetStringField(TEXT("boneName"),   BoneName);
			Arguments->TryGetStringField(TEXT("location"),   LocStr);
			Arguments->TryGetStringField(TEXT("rotation"),   RotStr);
			Arguments->TryGetStringField(TEXT("scale"),      ScaleStr);
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPath);
		Entry->SetStringField(TEXT("action"), Action);

		if (Action.Equals(TEXT("add_socket"), ESearchCase::IgnoreCase))
		{
			if (SocketName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_socket 需要 socketName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			// 检查重名
			for (const USkeletalMeshSocket* Sock : Skeleton->Sockets)
			{
				if (Sock && Sock->SocketName == FName(*SocketName))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket 已存在: %s"), *SocketName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}
			}
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
			Skeleton->MarkPackageDirty();

			Entry->SetStringField(TEXT("socketName"), SocketName);
			Entry->SetStringField(TEXT("boneName"), NewSocket->BoneName.ToString());
			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else if (Action.Equals(TEXT("remove_socket"), ESearchCase::IgnoreCase))
		{
			if (SocketName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_socket 需要 socketName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
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
				return;
			}
			Skeleton->MarkPackageDirty();
			Entry->SetStringField(TEXT("socketName"), SocketName);
			Entry->SetBoolField(TEXT("removed"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else if (Action.Equals(TEXT("modify_socket"), ESearchCase::IgnoreCase))
		{
			if (SocketName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("modify_socket 需要 socketName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
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
				return;
			}
			// 按需修改字段
			if (!BoneName.IsEmpty()) TargetSocket->BoneName = FName(*BoneName);
			FVector Loc; if (ParseVec3(LocStr, Loc)) TargetSocket->RelativeLocation = Loc;
			FRotator Rot; if (NxParseRotatorStr(RotStr, Rot)) TargetSocket->RelativeRotation = Rot;
			FVector Scale; if (ParseVec3(ScaleStr, Scale)) TargetSocket->RelativeScale = Scale;

			Skeleton->MarkPackageDirty();
			Entry->SetStringField(TEXT("socketName"), SocketName);
			Entry->SetStringField(TEXT("boneName"), TargetSocket->BoneName.ToString());
			Entry->SetBoolField(TEXT("modified"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSkeletonCapability)
