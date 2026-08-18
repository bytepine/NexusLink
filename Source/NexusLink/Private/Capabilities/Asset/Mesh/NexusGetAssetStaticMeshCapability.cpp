// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusGetAssetStaticMeshCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "NexusMcpTool.h"

void FGetAssetStaticMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_static_mesh");
	Out.SearchAssetTypes = {TEXT("StaticMesh")};
	Out.Description = TEXT("Inspect StaticMesh snapshot. Writes via manage_asset_static_mesh.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("StaticMesh asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mesh"), TEXT("lod"), TEXT("collision"), TEXT("material"), TEXT("slot") };
	Out.RelatedCapabilities = { TEXT("manage_asset_static_mesh"), TEXT("search_asset"), TEXT("get_asset_refs"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("Read mesh metadata; use manage_asset_static_mesh for writes");
}

FCapabilityResult FGetAssetStaticMeshCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("assetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Path);

		UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(Path);
		if (!Mesh)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("StaticMesh not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), Mesh->GetName());
		Entry->SetStringField(TEXT("assetType"), TEXT("StaticMesh"));

		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
		BoundsObj->SetNumberField(TEXT("extentX"), Bounds.BoxExtent.X);
		BoundsObj->SetNumberField(TEXT("extentY"), Bounds.BoxExtent.Y);
		BoundsObj->SetNumberField(TEXT("extentZ"), Bounds.BoxExtent.Z);
		Entry->SetObjectField(TEXT("bounds"), BoundsObj);

		Entry->SetNumberField(TEXT("lodCount"), Mesh->GetNumLODs());

		TArray<TSharedPtr<FJsonValue>> Slots;
		const TArray<FStaticMaterial>& Materials = FNexusAssetUtils::GetStaticMeshMaterials(*Mesh);
		for (int32 i = 0; i < Materials.Num(); ++i)
		{
			const FStaticMaterial& SM = Materials[i];
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

		UBodySetup* BodySetup = FNexusAssetUtils::GetStaticMeshBodySetup(Mesh);
		if (BodySetup)
		{
			TSharedPtr<FJsonObject> ColObj = MakeShared<FJsonObject>();
			ColObj->SetNumberField(TEXT("collisionTraceFlag"),
				static_cast<double>(static_cast<int32>(BodySetup->CollisionTraceFlag)));
			ColObj->SetNumberField(TEXT("convexElemCount"), BodySetup->AggGeom.ConvexElems.Num());
			ColObj->SetNumberField(TEXT("boxElemCount"), BodySetup->AggGeom.BoxElems.Num());
			ColObj->SetNumberField(TEXT("sphereElemCount"), BodySetup->AggGeom.SphereElems.Num());
			Entry->SetObjectField(TEXT("collision"), ColObj);
		}

		TArray<TSharedPtr<FJsonValue>> SocketArr;
		for (UStaticMeshSocket* Socket : Mesh->Sockets)
		{
			if (!Socket) continue;
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("name"), Socket->SocketName.ToString());
			S->SetNumberField(TEXT("locX"), Socket->RelativeLocation.X);
			S->SetNumberField(TEXT("locY"), Socket->RelativeLocation.Y);
			S->SetNumberField(TEXT("locZ"), Socket->RelativeLocation.Z);
			SocketArr.Add(MakeShared<FJsonValueObject>(S));
		}
		Entry->SetArrayField(TEXT("sockets"), SocketArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetStaticMeshCapability)
