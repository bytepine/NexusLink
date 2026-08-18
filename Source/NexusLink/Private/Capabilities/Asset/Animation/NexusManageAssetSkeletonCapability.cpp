// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetSkeletonCapability.h"
#include "Utils/NexusArgs.h"
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
	Out.Description = TEXT("Batch edit Skeleton Socket. action=add_socket|remove_socket|modify_socket.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Socket operation"),
			{ TEXT("add_socket"), TEXT("remove_socket"), TEXT("modify_socket") }))
		.Prop(TEXT("socketName"),   FNexusSchema::Str(TEXT("Socket name")))
		.Prop(TEXT("boneName"),     FNexusSchema::Str(TEXT("Attach bone name (add/modify)")))
		.Prop(TEXT("location"),     FNexusSchema::Str(TEXT("Position X,Y,Z (add/modify)")))
		.Prop(TEXT("rotation"),     FNexusSchema::Str(TEXT("Rotation P,Y,R (add/modify)")))
		.Prop(TEXT("scale"),        FNexusSchema::Str(TEXT("Scale X,Y,Z (add/modify)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Skeleton asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch socket ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("socket"), TEXT("bone"), TEXT("rig"), TEXT("attach") };
	Out.RelatedCapabilities = { TEXT("get_asset_skeleton"), TEXT("get_asset_skeletal_mesh") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("add/remove Modify Skeleton Socket; persist with save_asset after changes");
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

struct FSkeletonActionState
{
	USkeleton* Skeleton = nullptr;
	bool bDirty = false;
	TSharedPtr<FJsonObject> OutTop;
};

static FSkeletonActionState* SkelState(FNexusActionContext& Ctx)
{
	return static_cast<FSkeletonActionState*>(Ctx.Target);
}

static USkeleton* SkelFrom(FNexusActionContext& Ctx)
{
	FSkeletonActionState* S = SkelState(Ctx);
	return S ? S->Skeleton : nullptr;
}

static void MarkSkelDirty(FNexusActionContext& Ctx)
{
	if (FSkeletonActionState* S = SkelState(Ctx))
	{
		S->bDirty = true;
	}
}

static USkeletalMeshSocket* FindSkeletonSocket(USkeleton* Skeleton, const FName& Name)
{
	for (USkeletalMeshSocket* Sock : Skeleton->Sockets)
	{
		if (Sock && Sock->SocketName == Name) return Sock;
	}
	return nullptr;
}

static void HandleSkel_AddSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeleton* Skeleton = SkelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString SocketName = A.Str(TEXT("socketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_socket requires socketName"));
		return;
	}
	if (FindSkeletonSocket(Skeleton, FName(*SocketName)))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket already exists: %s"), *SocketName));
		return;
	}

	const FString BoneName = A.Str(TEXT("boneName"));
	const FString LocStr = A.Str(TEXT("location"));
	const FString RotStr = A.Str(TEXT("rotation"));
	const FString ScaleStr = A.Str(TEXT("scale"));

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
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("socketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("boneName"), NewSocket->BoneName.ToString());
}

static void HandleSkel_RemoveSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeleton* Skeleton = SkelFrom(Ctx);
	const FString SocketName = FNexusArgs(Op).Str(TEXT("socketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_socket requires socketName"));
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
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket not found: %s"), *SocketName));
		return;
	}
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("socketName"), SocketName);
	Ctx.Entry->SetBoolField(TEXT("removed"), true);
}

static void HandleSkel_ModifySocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeleton* Skeleton = SkelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString SocketName = A.Str(TEXT("socketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("modify_socket requires socketName"));
		return;
	}
	USkeletalMeshSocket* TargetSocket = FindSkeletonSocket(Skeleton, FName(*SocketName));
	if (!TargetSocket)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket not found: %s"), *SocketName));
		return;
	}
	const FString BoneName = A.Str(TEXT("boneName"));
	if (!BoneName.IsEmpty()) TargetSocket->BoneName = FName(*BoneName);
	FVector Loc; if (ParseVec3(A.Str(TEXT("location")), Loc)) TargetSocket->RelativeLocation = Loc;
	FRotator Rot; if (NxParseRotatorStr(A.Str(TEXT("rotation")), Rot)) TargetSocket->RelativeRotation = Rot;
	FVector Scale; if (ParseVec3(A.Str(TEXT("scale")), Scale)) TargetSocket->RelativeScale = Scale;
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("socketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("boneName"), TargetSocket->BoneName.ToString());
	Ctx.Entry->SetBoolField(TEXT("modified"), true);
}

bool FManageAssetSkeletonCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	USkeleton* Skeleton = FNexusAssetUtils::LoadAssetWithFallback<USkeleton>(AssetPath);
	if (!Skeleton)
	{
		OutError = FString::Printf(TEXT("Skeleton not found: %s"), *AssetPath);
		return false;
	}
	FSkeletonActionState* State = new FSkeletonActionState();
	State->Skeleton = Skeleton;
	OutTarget = State;
	return true;
}

void FManageAssetSkeletonCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	(void)Args;
	if (FSkeletonActionState* State = static_cast<FSkeletonActionState*>(Target))
	{
		State->OutTop = OutTop;
	}
}

void FManageAssetSkeletonCapability::FinalizeTarget(void* Target) const
{
	FSkeletonActionState* State = static_cast<FSkeletonActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Skeleton)
	{
		State->Skeleton->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("note"), TEXT("Modified; persist with save_asset"));
		}
	}
	delete State;
}

void FManageAssetSkeletonCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add_socket"),    &HandleSkel_AddSocket);
	OutHandlers.Add(TEXT("remove_socket"), &HandleSkel_RemoveSocket);
	OutHandlers.Add(TEXT("modify_socket"), &HandleSkel_ModifySocket);
}

REGISTER_MCP_CAPABILITY(FManageAssetSkeletonCapability)
