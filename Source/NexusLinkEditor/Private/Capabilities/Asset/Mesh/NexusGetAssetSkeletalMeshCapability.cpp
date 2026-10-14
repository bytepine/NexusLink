// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusGetAssetSkeletalMeshCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Materials/MaterialInterface.h"
#if NX_UE_HAS_SKELETAL_MATERIAL_COMMON_HEADER
#include "Engine/SkinnedAssetCommon.h"
#endif
#include "Animation/Skeleton.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "NexusMcpTool.h"

void FNexusGetAssetSkeletalMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetSkeletalMesh");
	Out.SearchAssetTypes = {TEXT("SkeletalMesh")};
	Out.Description = TEXT("Inspect SkeletalMesh snapshot. Writes via ManageAssetSkeletalMesh.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("SkeletalMesh asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("skmesh"), TEXT("skeletal"), TEXT("lod"), TEXT("skin"), TEXT("physics") };
	Out.RelatedCapabilities = { TEXT("ManageAssetSkeletalMesh"), TEXT("SearchAsset"), TEXT("GetAssetSkeleton"), TEXT("GetAssetRefs"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Read skeletal mesh metadata; use ManageAssetSkeletalMesh for writes");
}

FCapabilityResult FNexusGetAssetSkeletalMeshCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("AssetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Path);

		USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(Path);
		if (!Mesh)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("SkeletalMesh not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), Mesh->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("SkeletalMesh"));

		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
		BoundsObj->SetNumberField(TEXT("ExtentX"), Bounds.BoxExtent.X);
		BoundsObj->SetNumberField(TEXT("ExtentY"), Bounds.BoxExtent.Y);
		BoundsObj->SetNumberField(TEXT("ExtentZ"), Bounds.BoxExtent.Z);
		Entry->SetObjectField(TEXT("Bounds"), BoundsObj);

		Entry->SetNumberField(TEXT("LODCount"), Mesh->GetLODNum());

#if NX_UE_HAS_SKELETAL_MESH_SKELETON_ACCESSOR
		if (const USkeleton* Skel = Mesh->GetSkeleton())
#else
		if (USkeleton* Skel = Mesh->Skeleton)
#endif
		{
			Entry->SetStringField(TEXT("Skeleton"), Skel->GetPathName());
		}

		TArray<TSharedPtr<FJsonValue>> Slots;
		const TArray<FSkeletalMaterial>& Materials = FNexusAssetUtils::GetSkeletalMeshMaterials(*Mesh);
		for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
		{
			const FSkeletalMaterial& SM = Materials[MaterialIndex];
			TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetNumberField(TEXT("Index"), MaterialIndex);
			SlotObj->SetStringField(TEXT("SlotName"), SM.MaterialSlotName.ToString());
			if (SM.MaterialInterface)
			{
				SlotObj->SetStringField(TEXT("Material"), SM.MaterialInterface->GetPathName());
			}
			Slots.Add(MakeShared<FJsonValueObject>(SlotObj));
		}
		Entry->SetArrayField(TEXT("MaterialSlots"), Slots);

		if (UPhysicsAsset* PhysAsset = FNexusAssetUtils::GetSkeletalMeshPhysicsAsset(Mesh))
		{
			Entry->SetStringField(TEXT("PhysicsAsset"), PhysAsset->GetPathName());
		}

		TArray<TSharedPtr<FJsonValue>> SocketArr;
		for (USkeletalMeshSocket* Socket : Mesh->GetMeshOnlySocketList())
		{
			if (!Socket) continue;
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("name"), Socket->SocketName.ToString());
			S->SetStringField(TEXT("BoneName"), Socket->BoneName.ToString());
			S->SetNumberField(TEXT("LocX"), Socket->RelativeLocation.X);
			S->SetNumberField(TEXT("LocY"), Socket->RelativeLocation.Y);
			S->SetNumberField(TEXT("LocZ"), Socket->RelativeLocation.Z);
			SocketArr.Add(MakeShared<FJsonValueObject>(S));
		}
		Entry->SetArrayField(TEXT("Sockets"), SocketArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetSkeletalMeshCapability)
