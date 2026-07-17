// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusGetAssetSkeletalMeshCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#if NX_UE_HAS_SKELETAL_MATERIAL_COMMON_HEADER
#include "Engine/SkinnedAssetCommon.h"
#endif
#include "Animation/Skeleton.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "NexusMcpTool.h"

void FGetAssetSkeletalMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_skeletal_mesh");
	Out.SearchAssetTypes = {TEXT("SkeletalMesh")};
	Out.Description = TEXT("检查 SkeletalMesh 快照。LOD/材质槽/骨骼。写用 manage_asset_skeletal_mesh。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SkeletalMesh 资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个 SkeletalMesh 路径（批量）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("skmesh"), TEXT("skeletal"), TEXT("lod"), TEXT("skin"), TEXT("physics") };
	Out.RelatedCapabilities = { TEXT("manage_asset_skeletal_mesh"), TEXT("search_asset"), TEXT("get_asset_skeleton"), TEXT("get_asset_refs"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("读骨骼网格元数据；写用 manage_asset_skeletal_mesh");
}

static void CollectSkeletalMeshPaths(const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutPaths)
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

FCapabilityResult FGetAssetSkeletalMeshCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TArray<FString> Paths;
		CollectSkeletalMeshPaths(Arguments, Paths);
		if (Paths.Num() == 0)
		{
			OutError = TEXT("需要 assetPath 或 assetPaths");
			return;
		}

		for (const FString& Path : Paths)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("assetPath"), Path);

			USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(Path);
			if (!Mesh)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("SkeletalMesh 未找到: %s"), *Path));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			Entry->SetStringField(TEXT("name"), Mesh->GetName());
			Entry->SetStringField(TEXT("assetType"), TEXT("SkeletalMesh"));

			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
			BoundsObj->SetNumberField(TEXT("extentX"), Bounds.BoxExtent.X);
			BoundsObj->SetNumberField(TEXT("extentY"), Bounds.BoxExtent.Y);
			BoundsObj->SetNumberField(TEXT("extentZ"), Bounds.BoxExtent.Z);
			Entry->SetObjectField(TEXT("bounds"), BoundsObj);

			Entry->SetNumberField(TEXT("lodCount"), Mesh->GetLODNum());

#if NX_UE_HAS_SKELETAL_MESH_SKELETON_ACCESSOR
			if (const USkeleton* Skel = Mesh->GetSkeleton())
#else
			if (USkeleton* Skel = Mesh->Skeleton)
#endif
			{
				Entry->SetStringField(TEXT("skeleton"), Skel->GetPathName());
			}

			TArray<TSharedPtr<FJsonValue>> Slots;
			const TArray<FSkeletalMaterial>& Materials = FNexusAssetUtils::GetSkeletalMeshMaterials(*Mesh);
			for (int32 i = 0; i < Materials.Num(); ++i)
			{
				const FSkeletalMaterial& SM = Materials[i];
				TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
				SlotObj->SetNumberField(TEXT("index"), i);
				SlotObj->SetStringField(TEXT("slotName"), SM.MaterialSlotName.ToString());
				if (SM.MaterialInterface)
				{
					SlotObj->SetStringField(TEXT("material"), SM.MaterialInterface->GetPathName());
				}
				Slots.Add(MakeShared<FJsonValueObject>(SlotObj));
			}
			Entry->SetArrayField(TEXT("materialSlots"), Slots);

			if (UPhysicsAsset* PhysAsset = FNexusAssetUtils::GetSkeletalMeshPhysicsAsset(Mesh))
			{
				Entry->SetStringField(TEXT("physicsAsset"), PhysAsset->GetPathName());
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetSkeletalMeshCapability)
