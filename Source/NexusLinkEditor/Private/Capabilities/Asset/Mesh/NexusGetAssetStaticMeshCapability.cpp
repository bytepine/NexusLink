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
#include "Utils/NexusVersionCompat.h"
#include "StaticMeshResources.h"
#include "NavCollision.h"
#include "NavAreas/NavArea.h"

void FNexusGetAssetStaticMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetStaticMesh");
	Out.SearchAssetTypes = {TEXT("StaticMesh")};
	Out.Description = TEXT("Inspect StaticMesh snapshot. Writes via ManageAssetStaticMesh.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("StaticMesh asset path")))
		.Prop(TEXT("LODIndex"),FNexusSchema::Int(TEXT("Render LOD for bounded triangle samples"),0,0,7))
		.Prop(TEXT("TriangleOffset"),FNexusSchema::Int(TEXT("First render triangle for samples"),0,0,MAX_int32))
		.Prop(TEXT("MaxTriangles"),FNexusSchema::Int(TEXT("Optional local-space render triangle samples, not proof of cooked/live collision"),0,0,128))
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

		Entry->SetStringField(TEXT("Name"), Mesh->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("StaticMesh"));

		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
		BoundsObj->SetNumberField(TEXT("ExtentX"), Bounds.BoxExtent.X);
		BoundsObj->SetNumberField(TEXT("ExtentY"), Bounds.BoxExtent.Y);
		BoundsObj->SetNumberField(TEXT("ExtentZ"), Bounds.BoxExtent.Z);
		Entry->SetObjectField(TEXT("Bounds"), BoundsObj);

		Entry->SetNumberField(TEXT("LODCount"), Mesh->GetNumLODs());
		Entry->SetNumberField(TEXT("LODForCollision"),Mesh->LODForCollision);
		Entry->SetBoolField(TEXT("bContainsPhysicsTriMeshData"),Mesh->ContainsPhysicsTriMeshData(false));
		auto Navigation=MakeShared<FJsonObject>(); Navigation->SetBoolField(TEXT("bHasNavigationData"),Mesh->bHasNavigationData);
		const UNavCollision* NavCollision=Cast<UNavCollision>(Mesh->GetNavCollision()); Navigation->SetBoolField(TEXT("bHasNavCollision"),NavCollision!=nullptr);
		if (NavCollision) {
			Navigation->SetStringField(TEXT("Path"),NavCollision->GetPathName()); Navigation->SetBoolField(TEXT("bIsDynamicObstacle"),NavCollision->IsDynamicObstacle());
			Navigation->SetBoolField(TEXT("bGatherConvexGeometry"),NavCollision->bGatherConvexGeometry);
			Navigation->SetNumberField(TEXT("BoxCount"),NavCollision->BoxCollision.Num()); Navigation->SetNumberField(TEXT("CylinderCount"),NavCollision->CylinderCollision.Num());
			if (NavCollision->AreaClass) Navigation->SetStringField(TEXT("AreaClass"),NavCollision->AreaClass->GetPathName());
		}
		Entry->SetObjectField(TEXT("Navigation"),Navigation);
#if NX_UE_HAS_STATIC_MESH_ACCESSORS
		const FStaticMeshRenderData* RenderData=Mesh->GetRenderData();
#else
		const FStaticMeshRenderData* RenderData=Mesh->RenderData.Get();
#endif
		TArray<TSharedPtr<FJsonValue>> Sections,Triangles;
		if (RenderData) {
			for (int32 LodIndex=0; LodIndex<RenderData->LODResources.Num(); ++LodIndex) {
				const auto& Lod=RenderData->LODResources[LodIndex];
				for (int32 SectionIndex=0; SectionIndex<Lod.Sections.Num() && Sections.Num()<512; ++SectionIndex) {
					const auto& Section=Lod.Sections[SectionIndex]; const auto Info=Mesh->GetSectionInfoMap().Get(LodIndex,SectionIndex);
					auto Item=MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("LODIndex"),LodIndex); Item->SetNumberField(TEXT("SectionIndex"),SectionIndex);
					Item->SetNumberField(TEXT("MaterialIndex"),Section.MaterialIndex); Item->SetNumberField(TEXT("FirstIndex"),Section.FirstIndex); Item->SetNumberField(TEXT("TriangleCount"),Section.NumTriangles);
					Item->SetBoolField(TEXT("bEnableCollision"),Info.bEnableCollision); Item->SetBoolField(TEXT("bRenderEnableCollision"),Section.bEnableCollision); Sections.Add(MakeShared<FJsonValueObject>(Item));
				}
			}
			const int32 LodIndex=static_cast<int32>(A.Num(TEXT("LODIndex"))), MaxTriangles=static_cast<int32>(A.Num(TEXT("MaxTriangles")));
			if (MaxTriangles>0 && !RenderData->LODResources.IsValidIndex(LodIndex)) { OutError=TEXT("LODIndex does not exist"); return; }
			if (MaxTriangles>0) {
				const auto& Lod=RenderData->LODResources[LodIndex]; const auto& Positions=Lod.VertexBuffers.PositionVertexBuffer;
				const auto Indices=Lod.IndexBuffer.GetArrayView();
				Entry->SetNumberField(TEXT("RenderTriangleCount"),Indices.Num()/3);
				for (int32 Triangle=static_cast<int32>(A.Num(TEXT("TriangleOffset"))); Triangle<Indices.Num()/3 && Triangles.Num()<MaxTriangles; ++Triangle) {
					TArray<TSharedPtr<FJsonValue>> Vertices; FVector Center=FVector::ZeroVector;
					for (int32 Corner=0; Corner<3; ++Corner) {
						const uint32 Vertex=Indices[Triangle*3+Corner]; if (Vertex>=Positions.GetNumVertices()) { OutError=TEXT("Render index has no CPU vertex data"); return; }
						const FVector Position(Positions.VertexPosition(Vertex)); Center+=Position/3;
						auto Point=MakeShared<FJsonObject>(); Point->SetNumberField(TEXT("X"),Position.X); Point->SetNumberField(TEXT("Y"),Position.Y); Point->SetNumberField(TEXT("Z"),Position.Z); Vertices.Add(MakeShared<FJsonValueObject>(Point));
					}
					auto Item=MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("TriangleIndex"),Triangle); Item->SetArrayField(TEXT("Vertices"),Vertices);
					auto Centroid=MakeShared<FJsonObject>(); Centroid->SetNumberField(TEXT("X"),Center.X); Centroid->SetNumberField(TEXT("Y"),Center.Y); Centroid->SetNumberField(TEXT("Z"),Center.Z); Item->SetObjectField(TEXT("Centroid"),Centroid);
					Triangles.Add(MakeShared<FJsonValueObject>(Item));
				}
			}
		}
		Entry->SetArrayField(TEXT("Sections"),Sections); Entry->SetArrayField(TEXT("Triangles"),Triangles);
		Entry->SetBoolField(TEXT("bSectionsTruncated"),Sections.Num()>=512);

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
			S->SetStringField(TEXT("Name"), Socket->SocketName.ToString());
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
