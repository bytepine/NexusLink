// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetStaticMeshCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/SphereElem.h"
#include "NexusMcpTool.h"

namespace
{
	bool ParseStaticMeshVec3(const TSharedPtr<FJsonObject>& Op, const TCHAR* XKey, const TCHAR* YKey, const TCHAR* ZKey,
		FVector& Out, FVector Default)
	{
		Out = Default;
		bool bAny = false;
		if (Op->HasField(XKey)) { Out.X = Op->GetNumberField(XKey); bAny = true; }
		if (Op->HasField(YKey)) { Out.Y = Op->GetNumberField(YKey); bAny = true; }
		if (Op->HasField(ZKey)) { Out.Z = Op->GetNumberField(ZKey); bAny = true; }
		return bAny;
	}

	bool ParseCollisionTraceFlag(const FString& Text, ECollisionTraceFlag& OutFlag, FString& OutError)
	{
		if (Text.IsEmpty()
			|| Text.Equals(TEXT("UseDefault"), ESearchCase::IgnoreCase)
			|| Text.Equals(TEXT("Default"), ESearchCase::IgnoreCase)
			|| Text == TEXT("0"))
		{
			OutFlag = CTF_UseDefault;
			return true;
		}
		if (Text.Equals(TEXT("UseSimpleAndComplex"), ESearchCase::IgnoreCase)
			|| Text.Equals(TEXT("SimpleAndComplex"), ESearchCase::IgnoreCase)
			|| Text == TEXT("1"))
		{
			OutFlag = CTF_UseSimpleAndComplex;
			return true;
		}
		if (Text.Equals(TEXT("UseSimpleAsComplex"), ESearchCase::IgnoreCase)
			|| Text.Equals(TEXT("SimpleAsComplex"), ESearchCase::IgnoreCase)
			|| Text == TEXT("2"))
		{
			OutFlag = CTF_UseSimpleAsComplex;
			return true;
		}
		if (Text.Equals(TEXT("UseComplexAsSimple"), ESearchCase::IgnoreCase)
			|| Text.Equals(TEXT("ComplexAsSimple"), ESearchCase::IgnoreCase)
			|| Text == TEXT("3"))
		{
			OutFlag = CTF_UseComplexAsSimple;
			return true;
		}
		OutError = TEXT("collisionTraceFlag must be UseDefault/UseSimpleAndComplex/UseSimpleAsComplex/UseComplexAsSimple");
		return false;
	}

	UBodySetup* EnsureBodySetup(UStaticMesh* Mesh)
	{
		if (!Mesh) return nullptr;
		UBodySetup* Body = FNexusAssetUtils::GetStaticMeshBodySetup(Mesh);
		if (!Body)
		{
			Mesh->CreateBodySetup();
			Body = FNexusAssetUtils::GetStaticMeshBodySetup(Mesh);
		}
		return Body;
	}

	void InvalidateCollision(UStaticMesh* Mesh, UBodySetup* Body)
	{
		if (!Mesh || !Body) return;
		Body->InvalidatePhysicsData();
		Body->CreatePhysicsMeshes();
#if WITH_EDITORONLY_DATA
		Mesh->bCustomizedCollision = true;
#endif
	}
}

void FManageAssetStaticMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_static_mesh");
	Out.SearchAssetTypes = {TEXT("StaticMesh")};
	Out.Description = TEXT("Batch edit StaticMesh. Material/collision/Socket/LOD ScreenSize.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), {
			TEXT("set_material_slot"), TEXT("set_property"),
			TEXT("set_collision_trace_flag"), TEXT("add_box_collision"), TEXT("add_sphere_collision"),
			TEXT("clear_simple_collision"),
			TEXT("add_socket"), TEXT("remove_socket"), TEXT("set_socket"),
			TEXT("set_lod_screen_size")
		}))
		.Prop(TEXT("slotIndex"),      FNexusSchema::Int(TEXT("Material slot index (set_material_slot)")))
		.Prop(TEXT("materialPath"),   FNexusSchema::Str(TEXT("Material asset path (set_material_slot)")))
		.Prop(TEXT("propertyPath"),   FNexusSchema::Str(TEXT("Property path (set_property)")))
		.Prop(TEXT("value"),          FNexusSchema::Str(TEXT("New property value (set_property)")))
		.Prop(TEXT("collisionTraceFlag"), FNexusSchema::Enum(TEXT("Collision complexity"), {
			TEXT("UseDefault"), TEXT("UseSimpleAndComplex"), TEXT("UseSimpleAsComplex"), TEXT("UseComplexAsSimple")
		}))
		.Prop(TEXT("x"), FNexusSchema::Num(TEXT("Position/half-axis X (collision center or Box half-axis)")))
		.Prop(TEXT("y"), FNexusSchema::Num(TEXT("Position/half-axis Y")))
		.Prop(TEXT("z"), FNexusSchema::Num(TEXT("Position/half-axis Z")))
		.Prop(TEXT("extentX"), FNexusSchema::Num(TEXT("Box half-extent X (defaults to x)")))
		.Prop(TEXT("extentY"), FNexusSchema::Num(TEXT("Box half-extent Y (defaults to y)")))
		.Prop(TEXT("extentZ"), FNexusSchema::Num(TEXT("Box half-extent Z (defaults to z)")))
		.Prop(TEXT("radius"), FNexusSchema::Num(TEXT("Sphere radius (add_sphere_collision)")))
		.Prop(TEXT("socketName"), FNexusSchema::Str(TEXT("Socket name")))
		.Prop(TEXT("locX"), FNexusSchema::Num(TEXT("Socket/collision center X")))
		.Prop(TEXT("locY"), FNexusSchema::Num(TEXT("Socket/collision center Y")))
		.Prop(TEXT("locZ"), FNexusSchema::Num(TEXT("Socket/collision center Z")))
		.Prop(TEXT("pitch"), FNexusSchema::Num(TEXT("Socket rotation Pitch")))
		.Prop(TEXT("yaw"), FNexusSchema::Num(TEXT("Socket rotation Yaw")))
		.Prop(TEXT("roll"), FNexusSchema::Num(TEXT("Socket rotation Roll")))
		.Prop(TEXT("scaleX"), FNexusSchema::Num(TEXT("Socket rotation Scale X")))
		.Prop(TEXT("scaleY"), FNexusSchema::Num(TEXT("Socket rotation Scale Y")))
		.Prop(TEXT("scaleZ"), FNexusSchema::Num(TEXT("Socket rotation Scale Z")))
		.Prop(TEXT("lodIndex"), FNexusSchema::Int(TEXT("LOD index (set_lod_screen_size)")))
		.Prop(TEXT("screenSize"), FNexusSchema::Num(TEXT("LOD ScreenSize（0–1）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("StaticMesh asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mesh"), TEXT("material"), TEXT("collision"), TEXT("socket"), TEXT("lod"), TEXT("static") };
	Out.RelatedCapabilities = { TEXT("get_asset_static_mesh"), TEXT("search_asset"), TEXT("save_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("Edit StaticMesh material/collision/Socket/LOD; persist with save_asset");
}

FCapabilityResult FManageAssetStaticMeshCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(AssetPath);
		if (!Mesh)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("StaticMesh not found: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Missing or empty operations"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);

			if (Action.Equals(TEXT("set_material_slot"), ESearchCase::IgnoreCase))
			{
				int32 SlotIndex = 0;
				FString MaterialPath;
				if (Op->HasField(TEXT("slotIndex")))
					SlotIndex = static_cast<int32>(Op->GetNumberField(TEXT("slotIndex")));
				Op->TryGetStringField(TEXT("materialPath"), MaterialPath);
				if (MaterialPath.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_material_slot requires materialPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
				if (!Material)
				{
					Material = FNexusAssetUtils::LoadAssetWithFallback<UMaterialInterface>(MaterialPath);
				}
				const TArray<FStaticMaterial>& Materials = FNexusAssetUtils::GetStaticMeshMaterials(*Mesh);
				if (!Materials.IsValidIndex(SlotIndex))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Material slot index %d out of range [0, %d)"), SlotIndex, Materials.Num()));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
#if WITH_EDITOR
				Mesh->SetMaterial(SlotIndex, Material);
				bDirty = true;
				Entry->SetNumberField(TEXT("slotIndex"), SlotIndex);
				Entry->SetStringField(TEXT("materialClass"), Material ? Material->GetClass()->GetName() : TEXT("None"));
				Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
				Entry->SetStringField(TEXT("error"), TEXT("set_material_slot only available in editor builds"));
#endif
			}
			else if (Action.Equals(TEXT("set_property"), ESearchCase::IgnoreCase))
			{
				FString PropPath, Value;
				Op->TryGetStringField(TEXT("propertyPath"), PropPath);
				Op->TryGetStringField(TEXT("value"), Value);
				if (PropPath.IsEmpty() || Value.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FString OldVal, ActualVal, Err;
				if (!FNexusPropertyUtils::WritePropertyAndEcho(Mesh, { PropPath }, 0, Value, OldVal, ActualVal, Err))
				{
					Entry->SetStringField(TEXT("error"), Err);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				bDirty = true;
				Entry->SetStringField(TEXT("propertyPath"), PropPath);
				if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
				if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
				Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
			}
			else if (Action.Equals(TEXT("set_collision_trace_flag"), ESearchCase::IgnoreCase))
			{
				FString FlagText;
				Op->TryGetStringField(TEXT("collisionTraceFlag"), FlagText);
				if (FlagText.IsEmpty()) Op->TryGetStringField(TEXT("value"), FlagText);
				ECollisionTraceFlag Flag;
				FString FlagErr;
				if (!ParseCollisionTraceFlag(FlagText, Flag, FlagErr))
				{
					Entry->SetStringField(TEXT("error"), FlagErr);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UBodySetup* Body = EnsureBodySetup(Mesh);
				if (!Body)
				{
					Entry->SetStringField(TEXT("error"), TEXT("Unable to create BodySetup"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Body->CollisionTraceFlag = Flag;
				InvalidateCollision(Mesh, Body);
				bDirty = true;
				Entry->SetStringField(TEXT("collisionTraceFlag"), FlagText.IsEmpty() ? TEXT("UseDefault") : FlagText);
				Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
			}
			else if (Action.Equals(TEXT("add_box_collision"), ESearchCase::IgnoreCase))
			{
				UBodySetup* Body = EnsureBodySetup(Mesh);
				if (!Body)
				{
					Entry->SetStringField(TEXT("error"), TEXT("Unable to create BodySetup"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FVector Center(0.f), Extent(50.f, 50.f, 50.f);
				ParseStaticMeshVec3(Op, TEXT("locX"), TEXT("locY"), TEXT("locZ"), Center, FVector::ZeroVector);
				if (!ParseStaticMeshVec3(Op, TEXT("extentX"), TEXT("extentY"), TEXT("extentZ"), Extent, Extent))
				{
					ParseStaticMeshVec3(Op, TEXT("x"), TEXT("y"), TEXT("z"), Extent, Extent);
				}
				FKBoxElem Box;
				Box.Center = Center;
				Box.X = FMath::Max(1.f, Extent.X);
				Box.Y = FMath::Max(1.f, Extent.Y);
				Box.Z = FMath::Max(1.f, Extent.Z);
				Body->AggGeom.BoxElems.Add(Box);
				InvalidateCollision(Mesh, Body);
				bDirty = true;
				Entry->SetNumberField(TEXT("boxElemCount"), Body->AggGeom.BoxElems.Num());
				Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
			}
			else if (Action.Equals(TEXT("add_sphere_collision"), ESearchCase::IgnoreCase))
			{
				UBodySetup* Body = EnsureBodySetup(Mesh);
				if (!Body)
				{
					Entry->SetStringField(TEXT("error"), TEXT("Unable to create BodySetup"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FVector Center(0.f);
				ParseStaticMeshVec3(Op, TEXT("locX"), TEXT("locY"), TEXT("locZ"), Center, FVector::ZeroVector);
				float Radius = 50.f;
				if (Op->HasField(TEXT("radius"))) Radius = static_cast<float>(Op->GetNumberField(TEXT("radius")));
				FKSphereElem Sphere;
				Sphere.Center = Center;
				Sphere.Radius = FMath::Max(1.f, Radius);
				Body->AggGeom.SphereElems.Add(Sphere);
				InvalidateCollision(Mesh, Body);
				bDirty = true;
				Entry->SetNumberField(TEXT("sphereElemCount"), Body->AggGeom.SphereElems.Num());
				Entry->SetNumberField(TEXT("radius"), Sphere.Radius);
				Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
			}
			else if (Action.Equals(TEXT("clear_simple_collision"), ESearchCase::IgnoreCase))
			{
				UBodySetup* Body = EnsureBodySetup(Mesh);
				if (!Body)
				{
					Entry->SetStringField(TEXT("error"), TEXT("Unable to create BodySetup"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Body->RemoveSimpleCollision();
				InvalidateCollision(Mesh, Body);
				bDirty = true;
				Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
			}
			else if (Action.Equals(TEXT("add_socket"), ESearchCase::IgnoreCase)
				|| Action.Equals(TEXT("set_socket"), ESearchCase::IgnoreCase))
			{
				FString SocketName;
				Op->TryGetStringField(TEXT("socketName"), SocketName);
				if (SocketName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("socketName required"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UStaticMeshSocket* Socket = Mesh->FindSocket(FName(*SocketName));
				const bool bAdd = Action.Equals(TEXT("add_socket"), ESearchCase::IgnoreCase);
				if (bAdd && Socket)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket already exists: %s"), *SocketName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				if (!bAdd && !Socket)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket not found: %s"), *SocketName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				if (!Socket)
				{
					Socket = NewObject<UStaticMeshSocket>(Mesh);
					Socket->SocketName = FName(*SocketName);
					Mesh->AddSocket(Socket);
				}
				FVector Loc = Socket->RelativeLocation;
				FRotator Rot = Socket->RelativeRotation;
				FVector Scale = Socket->RelativeScale;
				ParseStaticMeshVec3(Op, TEXT("locX"), TEXT("locY"), TEXT("locZ"), Loc, Loc);
				if (Op->HasField(TEXT("pitch"))) Rot.Pitch = Op->GetNumberField(TEXT("pitch"));
				if (Op->HasField(TEXT("yaw"))) Rot.Yaw = Op->GetNumberField(TEXT("yaw"));
				if (Op->HasField(TEXT("roll"))) Rot.Roll = Op->GetNumberField(TEXT("roll"));
				ParseStaticMeshVec3(Op, TEXT("scaleX"), TEXT("scaleY"), TEXT("scaleZ"), Scale, Scale);
				Socket->RelativeLocation = Loc;
				Socket->RelativeRotation = Rot;
				Socket->RelativeScale = Scale;
				bDirty = true;
				Entry->SetStringField(TEXT("socketName"), SocketName);
				Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
			}
			else if (Action.Equals(TEXT("remove_socket"), ESearchCase::IgnoreCase))
			{
				FString SocketName;
				Op->TryGetStringField(TEXT("socketName"), SocketName);
				if (SocketName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("remove_socket requires socketName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UStaticMeshSocket* Socket = Mesh->FindSocket(FName(*SocketName));
				if (!Socket)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket not found: %s"), *SocketName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Mesh->RemoveSocket(Socket);
				bDirty = true;
				Entry->SetStringField(TEXT("removed"), SocketName);
				Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
			}
			else if (Action.Equals(TEXT("set_lod_screen_size"), ESearchCase::IgnoreCase))
			{
#if WITH_EDITOR
				int32 LodIndex = 0;
				if (Op->HasField(TEXT("lodIndex"))) LodIndex = static_cast<int32>(Op->GetNumberField(TEXT("lodIndex")));
				if (!Op->HasField(TEXT("screenSize")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_lod_screen_size requires screenSize"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				if (!Mesh->IsSourceModelValid(LodIndex))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("LOD %d does not exist (sourceModels=%d)"), LodIndex, Mesh->GetNumSourceModels()));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const float ScreenSize = static_cast<float>(Op->GetNumberField(TEXT("screenSize")));
				Mesh->GetSourceModel(LodIndex).ScreenSize.Default = ScreenSize;
				Mesh->bAutoComputeLODScreenSize = false;
				bDirty = true;
				Entry->SetNumberField(TEXT("lodIndex"), LodIndex);
				Entry->SetNumberField(TEXT("screenSize"), ScreenSize);
				Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
				Entry->SetStringField(TEXT("error"), TEXT("set_lod_screen_size editor only"));
#endif
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		if (bDirty) Mesh->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetStaticMeshCapability)
