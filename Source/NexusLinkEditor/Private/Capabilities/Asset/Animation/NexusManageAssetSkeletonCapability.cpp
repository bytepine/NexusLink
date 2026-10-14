// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusManageAssetSkeletonCapability.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "NexusMcpTool.h"

void FNexusManageAssetSkeletonCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetSkeleton");
	Out.SearchAssetTypes = {TEXT("Skeleton")};
	Out.Description = TEXT("Batch edit Skeleton Socket. Action=AddSocket|RemoveSocket|ModifySocket.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),       FNexusSchema::Enum(TEXT("Socket operation"),
			{ TEXT("AddSocket"), TEXT("RemoveSocket"), TEXT("ModifySocket") }))
		.Prop(TEXT("SocketName"),   FNexusSchema::Str(TEXT("Socket name")))
		.Prop(TEXT("BoneName"),     FNexusSchema::Str(TEXT("Attach bone name (add/modify)")))
		.Prop(TEXT("Location"),     FNexusSchema::Str(TEXT("Position X,Y,Z (add/modify)")))
		.Prop(TEXT("Rotation"),     FNexusSchema::Str(TEXT("Rotation P,Y,R (add/modify)")))
		.Prop(TEXT("Scale"),        FNexusSchema::Str(TEXT("Scale X,Y,Z (add/modify)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Skeleton asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch socket ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("socket"), TEXT("Bone"), TEXT("rig"), TEXT("attach") };
	Out.RelatedCapabilities = { TEXT("GetAssetSkeleton"), TEXT("GetAssetSkeletalMesh") };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.WhenToUse = TEXT("add/remove Modify Skeleton Socket; persist with SaveAsset after changes");
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
	if (!Skeleton)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Skeleton is null"));
		return;
	}
	const FNexusArgs A(Op);
	const FString SocketName = A.Str(TEXT("SocketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddSocket requires SocketName"));
		return;
	}
	if (FindSkeletonSocket(Skeleton, FName(*SocketName)))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Socket already exists: %s"), *SocketName));
		return;
	}

	const FString BoneName = A.Str(TEXT("BoneName"));
	const FString LocStr = A.Str(TEXT("Location"));
	const FString RotStr = A.Str(TEXT("Rotation"));
	const FString ScaleStr = A.Str(TEXT("Scale"));

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
	Ctx.Entry->SetStringField(TEXT("SocketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("BoneName"), NewSocket->BoneName.ToString());
}

static void HandleSkel_RemoveSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeleton* Skeleton = SkelFrom(Ctx);
	if (!Skeleton)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Skeleton is null"));
		return;
	}
	const FString SocketName = FNexusArgs(Op).Str(TEXT("SocketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveSocket requires SocketName"));
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
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Socket not found: %s"), *SocketName));
		return;
	}
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("SocketName"), SocketName);
	Ctx.Entry->SetBoolField(TEXT("bRemoved"), true);
}

static void HandleSkel_ModifySocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeleton* Skeleton = SkelFrom(Ctx);
	if (!Skeleton)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Skeleton is null"));
		return;
	}
	const FNexusArgs A(Op);
	const FString SocketName = A.Str(TEXT("SocketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ModifySocket requires SocketName"));
		return;
	}
	USkeletalMeshSocket* TargetSocket = FindSkeletonSocket(Skeleton, FName(*SocketName));
	if (!TargetSocket)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Socket not found: %s"), *SocketName));
		return;
	}
	const FString BoneName = A.Str(TEXT("BoneName"));
	if (!BoneName.IsEmpty()) TargetSocket->BoneName = FName(*BoneName);
	FVector Loc; if (ParseVec3(A.Str(TEXT("Location")), Loc)) TargetSocket->RelativeLocation = Loc;
	FRotator Rot; if (NxParseRotatorStr(A.Str(TEXT("Rotation")), Rot)) TargetSocket->RelativeRotation = Rot;
	FVector Scale; if (ParseVec3(A.Str(TEXT("Scale")), Scale)) TargetSocket->RelativeScale = Scale;
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("SocketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("BoneName"), TargetSocket->BoneName.ToString());
	Ctx.Entry->SetBoolField(TEXT("bModified"), true);
}

bool FNexusManageAssetSkeletonCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetSkeletonCapability::AfterPrepareTarget(
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

void FNexusManageAssetSkeletonCapability::FinalizeTarget(void* Target) const
{
	FSkeletonActionState* State = static_cast<FSkeletonActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Skeleton)
	{
		State->Skeleton->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("Note"), TEXT("Modified; persist with SaveAsset"));
		}
	}
	delete State;
}

void FNexusManageAssetSkeletonCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("AddSocket"),    &HandleSkel_AddSocket);
	OutHandlers.Add(TEXT("RemoveSocket"), &HandleSkel_RemoveSocket);
	OutHandlers.Add(TEXT("ModifySocket"), &HandleSkel_ModifySocket);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetSkeletonCapability)
