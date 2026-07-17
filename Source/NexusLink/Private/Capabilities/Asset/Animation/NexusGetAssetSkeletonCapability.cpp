// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetSkeletonCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "NexusMcpTool.h"

void FGetAssetSkeletonCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_skeleton");
	Out.SearchAssetTypes = {TEXT("Skeleton")};
	Out.Description = TEXT("检查 Skeleton 快照。骨骼树/Socket 分页。写用 manage_asset_skeleton。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Skeleton 资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个 Skeleton 路径（批量）")))
		.Prop(TEXT("offset"),     FNexusSchema::Int(TEXT("骨骼列表分页偏移"), 0, 0))
		.Prop(TEXT("limit"),      FNexusSchema::Int(TEXT("骨骼列表每页条数"), 100, 1, 500))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("bone"), TEXT("socket"), TEXT("rig"), TEXT("ref skeleton") };
	Out.RelatedCapabilities = { TEXT("manage_asset_skeleton"), TEXT("search_asset"), TEXT("get_asset_anim_blueprint"), TEXT("get_asset_skeletal_mesh"), TEXT("get_asset_refs") };
	Out.WhenToUse = TEXT("读骨骼层级；Socket 写用 manage_asset_skeleton");
}

static void CollectSkeletonPaths(const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutPaths)
{
	OutPaths.Reset();
	if (!Args.IsValid()) return;

	FString Single;
	if (Args->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
	{
		OutPaths.Add(Single);
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(TEXT("assetPaths"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString P;
			if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty())
			{
				OutPaths.AddUnique(P);
			}
		}
	}
}

FCapabilityResult FGetAssetSkeletonCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TArray<FString> Paths;
		CollectSkeletonPaths(Arguments, Paths);
		if (Paths.Num() == 0)
		{
			OutError = TEXT("需要 assetPath 或 assetPaths");
			return;
		}

		int32 Offset = 0;
		int32 Limit = 100;
		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("offset")))
			{
				Offset = FMath::Max(0, static_cast<int32>(Arguments->GetNumberField(TEXT("offset"))));
			}
			if (Arguments->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("limit"))), 1, 500);
			}
		}

		for (const FString& Path : Paths)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("assetPath"), Path);

			USkeleton* Skeleton = FNexusAssetUtils::LoadAssetWithFallback<USkeleton>(Path);
			if (!Skeleton)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Skeleton 未找到: %s"), *Path));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			Entry->SetStringField(TEXT("name"), Skeleton->GetName());
			Entry->SetStringField(TEXT("assetType"), TEXT("Skeleton"));

			const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
			const int32 BoneCount = RefSkel.GetNum();
			Entry->SetNumberField(TEXT("boneCount"), BoneCount);

			TArray<TSharedPtr<FJsonValue>> Bones;
			const int32 End = FMath::Min(BoneCount, Offset + Limit);
			for (int32 i = Offset; i < End; ++i)
			{
				TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
				BoneObj->SetNumberField(TEXT("index"), i);
				BoneObj->SetStringField(TEXT("name"), RefSkel.GetBoneName(i).ToString());
				BoneObj->SetNumberField(TEXT("parentIndex"), RefSkel.GetParentIndex(i));
				Bones.Add(MakeShared<FJsonValueObject>(BoneObj));
			}
			Entry->SetArrayField(TEXT("bones"), Bones);
			Entry->SetNumberField(TEXT("offset"), Offset);
			Entry->SetNumberField(TEXT("limit"), Limit);
			Entry->SetNumberField(TEXT("returnedBoneCount"), Bones.Num());

			TArray<TSharedPtr<FJsonValue>> Sockets;
			const TArray<USkeletalMeshSocket*>& SocketList = Skeleton->Sockets;
			for (int32 s = 0; s < SocketList.Num() && s < 64; ++s)
			{
				const USkeletalMeshSocket* Sock = SocketList[s];
				if (!Sock) continue;
				TSharedPtr<FJsonObject> SockObj = MakeShared<FJsonObject>();
				SockObj->SetStringField(TEXT("name"), Sock->SocketName.ToString());
				SockObj->SetStringField(TEXT("boneName"), Sock->BoneName.ToString());
				Sockets.Add(MakeShared<FJsonValueObject>(SockObj));
			}
			Entry->SetNumberField(TEXT("socketCount"), SocketList.Num());
			Entry->SetArrayField(TEXT("sockets"), Sockets);

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetSkeletonCapability)
