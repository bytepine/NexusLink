// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetSkeletonCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "NexusMcpTool.h"

void FNexusGetAssetSkeletonCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetSkeleton");
	Out.SearchAssetTypes = {TEXT("Skeleton")};
	Out.Description = TEXT("Inspect Skeleton snapshot. Writes via ManageAssetSkeleton.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Skeleton asset path")))
		.Prop(TEXT("Offset"),     FNexusSchema::Int(TEXT("Skeleton list pagination offset"), 0, 0))
		.Prop(TEXT("Limit"),      FNexusSchema::Int(TEXT("Skeleton list page size"), 100, 1, 500))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("Bone"), TEXT("socket"), TEXT("rig"), TEXT("ref skeleton") };
	Out.RelatedCapabilities = { TEXT("ManageAssetSkeleton"), TEXT("SearchAsset"), TEXT("GetAssetAnimBlueprint"), TEXT("GetAssetSkeletalMesh"), TEXT("GetAssetRefs") };
	Out.WhenToUse = TEXT("Read bone hierarchy; use ManageAssetSkeleton for Socket writes");
}

FCapabilityResult FNexusGetAssetSkeletonCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("AssetPath"));

		int32 Offset = 0;
		int32 Limit = 100;
		if (Arguments->HasField(TEXT("Offset")))
		{
			Offset = FMath::Max(0, static_cast<int32>(A.Num(TEXT("Offset"))));
		}
		if (Arguments->HasField(TEXT("Limit")))
		{
			Limit = FMath::Clamp(static_cast<int32>(A.Num(TEXT("Limit"))), 1, 500);
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Path);

		USkeleton* Skeleton = FNexusAssetUtils::LoadAssetWithFallback<USkeleton>(Path);
		if (!Skeleton)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Skeleton not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), Skeleton->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("Skeleton"));

		const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
		const int32 BoneCount = RefSkel.GetNum();
		Entry->SetNumberField(TEXT("BoneCount"), BoneCount);

		TArray<TSharedPtr<FJsonValue>> Bones;
		const int32 End = FMath::Min(BoneCount, Offset + Limit);
		for (int32 i = Offset; i < End; ++i)
		{
			TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
			BoneObj->SetNumberField(TEXT("Index"), i);
			BoneObj->SetStringField(TEXT("name"), RefSkel.GetBoneName(i).ToString());
			BoneObj->SetNumberField(TEXT("ParentIndex"), RefSkel.GetParentIndex(i));
			Bones.Add(MakeShared<FJsonValueObject>(BoneObj));
		}
		Entry->SetArrayField(TEXT("Bones"), Bones);
		Entry->SetNumberField(TEXT("Offset"), Offset);
		Entry->SetNumberField(TEXT("Limit"), Limit);
		Entry->SetNumberField(TEXT("ReturnedBoneCount"), Bones.Num());

		TArray<TSharedPtr<FJsonValue>> Sockets;
		const TArray<USkeletalMeshSocket*>& SocketList = Skeleton->Sockets;
		for (int32 SocketListIndex = 0; SocketListIndex < SocketList.Num() && SocketListIndex < 64; ++SocketListIndex)
		{
			const USkeletalMeshSocket* Sock = SocketList[SocketListIndex];
			if (!Sock) continue;
			TSharedPtr<FJsonObject> SockObj = MakeShared<FJsonObject>();
			SockObj->SetStringField(TEXT("name"), Sock->SocketName.ToString());
			SockObj->SetStringField(TEXT("BoneName"), Sock->BoneName.ToString());
			Sockets.Add(MakeShared<FJsonValueObject>(SockObj));
		}
		Entry->SetNumberField(TEXT("SocketCount"), SocketList.Num());
		Entry->SetArrayField(TEXT("Sockets"), Sockets);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetSkeletonCapability)
