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

void FNexusGetAssetStaticMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetStaticMesh");
	Out.SearchAssetTypes = {TEXT("StaticMesh")};
	Out.Description = TEXT("Inspect StaticMesh snapshot. Writes via ManageAssetStaticMesh.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("StaticMesh asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mesh"), TEXT("lod"), TEXT("Collision"), TEXT("Material"), TEXT("slot") };
	Out.RelatedCapabilities = { TEXT("ManageAssetStaticMesh"), TEXT("SearchAsset"), TEXT("GetAssetRefs"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Read mesh metadata; use ManageAssetStaticMesh for writes");
}

FCapabilityResult FNexusGetAssetStaticMeshCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("AssetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Path);

		UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(Path);
		if (!Mesh)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("StaticMesh not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), Mesh->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("StaticMesh"));

		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
		BoundsObj->SetNumberField(TEXT("ExtentX"), Bounds.BoxExtent.X);
		BoundsObj->SetNumberField(TEXT("ExtentY"), Bounds.BoxExtent.Y);
		BoundsObj->SetNumberField(TEXT("ExtentZ"), Bounds.BoxExtent.Z);
		Entry->SetObjectField(TEXT("Bounds"), BoundsObj);

		Entry->SetNumberField(TEXT("LODCount"), Mesh->GetNumLODs());

		TArray<TSharedPtr<FJsonValue>> Slots;
		const TArray<FStaticMaterial>& Materials = FNexusAssetUtils::GetStaticMeshMaterials(*Mesh);
		for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
		{
			const FStaticMaterial& SM = Materials[MaterialIndex];
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

		UBodySetup* BodySetup = FNexusAssetUtils::GetStaticMeshBodySetup(Mesh);
		if (BodySetup)
		{
			TSharedPtr<FJsonObject> ColObj = MakeShared<FJsonObject>();
			ColObj->SetNumberField(TEXT("CollisionTraceFlag"),
				static_cast<double>(static_cast<int32>(BodySetup->CollisionTraceFlag)));
			ColObj->SetNumberField(TEXT("ConvexElemCount"), BodySetup->AggGeom.ConvexElems.Num());
			ColObj->SetNumberField(TEXT("BoxElemCount"), BodySetup->AggGeom.BoxElems.Num());
			ColObj->SetNumberField(TEXT("SphereElemCount"), BodySetup->AggGeom.SphereElems.Num());
			Entry->SetObjectField(TEXT("Collision"), ColObj);
		}

		TArray<TSharedPtr<FJsonValue>> SocketArr;
		for (UStaticMeshSocket* Socket : Mesh->Sockets)
		{
			if (!Socket) continue;
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("name"), Socket->SocketName.ToString());
			S->SetNumberField(TEXT("LocX"), Socket->RelativeLocation.X);
			S->SetNumberField(TEXT("LocY"), Socket->RelativeLocation.Y);
			S->SetNumberField(TEXT("LocZ"), Socket->RelativeLocation.Z);
			SocketArr.Add(MakeShared<FJsonValueObject>(S));
		}
		Entry->SetArrayField(TEXT("Sockets"), SocketArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetStaticMeshCapability)
