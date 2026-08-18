// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetStaticMeshCapability.h"
#include "Utils/NexusArgs.h"
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

static bool ParseStaticMeshVec3(const TSharedPtr<FJsonObject>& Op, const TCHAR* XKey, const TCHAR* YKey, const TCHAR* ZKey,
		FVector& Out, FVector Default)
{
	Out = Default;
	bool bAny = false;
	if (Op->HasField(XKey)) { Out.X = Op->GetNumberField(XKey); bAny = true; }
	if (Op->HasField(YKey)) { Out.Y = Op->GetNumberField(YKey); bAny = true; }
	if (Op->HasField(ZKey)) { Out.Z = Op->GetNumberField(ZKey); bAny = true; }
	return bAny;
}

static bool ParseCollisionTraceFlag(const FString& Text, ECollisionTraceFlag& OutFlag, FString& OutError)
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

static UBodySetup* EnsureBodySetup(UStaticMesh* Mesh)
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

static void InvalidateCollision(UStaticMesh* Mesh, UBodySetup* Body)
{
	if (!Mesh || !Body) return;
	Body->InvalidatePhysicsData();
	Body->CreatePhysicsMeshes();
#if WITH_EDITORONLY_DATA
	Mesh->bCustomizedCollision = true;
#endif
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

static UStaticMesh* MeshFrom(FNexusActionContext& Ctx)
{
	return static_cast<UStaticMesh*>(Ctx.Target);
}

static void MarkMeshDirty(FNexusActionContext& Ctx)
{
	if (UStaticMesh* Mesh = MeshFrom(Ctx))
	{
		Mesh->MarkPackageDirty();
	}
}

static void HandleSM_SetMaterialSlot(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FNexusArgs A(Op);
	const int32 SlotIndex = static_cast<int32>(A.Num(TEXT("slotIndex")));
	const FString MaterialPath = A.Str(TEXT("materialPath"));
	if (MaterialPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_material_slot requires materialPath"));
		return;
	}
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Material)
	{
		Material = FNexusAssetUtils::LoadAssetWithFallback<UMaterialInterface>(MaterialPath);
	}
	const TArray<FStaticMaterial>& Materials = FNexusAssetUtils::GetStaticMeshMaterials(*Mesh);
	if (!Materials.IsValidIndex(SlotIndex))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Material slot index %d out of range [0, %d)"), SlotIndex, Materials.Num()));
		return;
	}
#if WITH_EDITOR
	Mesh->SetMaterial(SlotIndex, Material);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("slotIndex"), SlotIndex);
	Ctx.Entry->SetStringField(TEXT("materialClass"), Material ? Material->GetClass()->GetName() : TEXT("None"));
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_material_slot only available in editor builds"));
#endif
}

static void HandleSM_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("propertyPath"));
	const FString Value = A.Str(TEXT("value"));
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Mesh, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("error"), Err);
		return;
	}
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSM_SetCollisionTraceFlag(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FNexusArgs A(Op);
	FString FlagText = A.Str(TEXT("collisionTraceFlag"));
	if (FlagText.IsEmpty()) FlagText = A.Str(TEXT("value"));
	ECollisionTraceFlag Flag;
	FString FlagErr;
	if (!ParseCollisionTraceFlag(FlagText, Flag, FlagErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FlagErr);
		return;
	}
	UBodySetup* Body = EnsureBodySetup(Mesh);
	if (!Body)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to create BodySetup"));
		return;
	}
	Body->CollisionTraceFlag = Flag;
	InvalidateCollision(Mesh, Body);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("collisionTraceFlag"), FlagText.IsEmpty() ? TEXT("UseDefault") : FlagText);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSM_AddBoxCollision(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	UBodySetup* Body = EnsureBodySetup(Mesh);
	if (!Body)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to create BodySetup"));
		return;
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
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("boxElemCount"), Body->AggGeom.BoxElems.Num());
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSM_AddSphereCollision(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	UBodySetup* Body = EnsureBodySetup(Mesh);
	if (!Body)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to create BodySetup"));
		return;
	}
	FVector Center(0.f);
	ParseStaticMeshVec3(Op, TEXT("locX"), TEXT("locY"), TEXT("locZ"), Center, FVector::ZeroVector);
	const float Radius = static_cast<float>(FNexusArgs(Op).Num(TEXT("radius"), 50.0));
	FKSphereElem Sphere;
	Sphere.Center = Center;
	Sphere.Radius = FMath::Max(1.f, Radius);
	Body->AggGeom.SphereElems.Add(Sphere);
	InvalidateCollision(Mesh, Body);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("sphereElemCount"), Body->AggGeom.SphereElems.Num());
	Ctx.Entry->SetNumberField(TEXT("radius"), Sphere.Radius);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSM_ClearSimpleCollision(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	(void)Op;
	UStaticMesh* Mesh = MeshFrom(Ctx);
	UBodySetup* Body = EnsureBodySetup(Mesh);
	if (!Body)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to create BodySetup"));
		return;
	}
	Body->RemoveSimpleCollision();
	InvalidateCollision(Mesh, Body);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void ApplySocketFields(UStaticMeshSocket* Socket, const TSharedPtr<FJsonObject>& Op)
{
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
}

static void HandleSM_AddSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FString SocketName = FNexusArgs(Op).Str(TEXT("socketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("socketName required"));
		return;
	}
	if (Mesh->FindSocket(FName(*SocketName)))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket already exists: %s"), *SocketName));
		return;
	}
	UStaticMeshSocket* Socket = NewObject<UStaticMeshSocket>(Mesh);
	Socket->SocketName = FName(*SocketName);
	Mesh->AddSocket(Socket);
	ApplySocketFields(Socket, Op);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("socketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSM_SetSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FString SocketName = FNexusArgs(Op).Str(TEXT("socketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("socketName required"));
		return;
	}
	UStaticMeshSocket* Socket = Mesh->FindSocket(FName(*SocketName));
	if (!Socket)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket not found: %s"), *SocketName));
		return;
	}
	ApplySocketFields(Socket, Op);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("socketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSM_RemoveSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FString SocketName = FNexusArgs(Op).Str(TEXT("socketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_socket requires socketName"));
		return;
	}
	UStaticMeshSocket* Socket = Mesh->FindSocket(FName(*SocketName));
	if (!Socket)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket not found: %s"), *SocketName));
		return;
	}
	Mesh->RemoveSocket(Socket);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("removed"), SocketName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSM_SetLodScreenSize(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FNexusArgs A(Op);
	const int32 LodIndex = static_cast<int32>(A.Num(TEXT("lodIndex")));
	if (!Op->HasField(TEXT("screenSize")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_lod_screen_size requires screenSize"));
		return;
	}
	if (!Mesh->IsSourceModelValid(LodIndex))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("LOD %d does not exist (sourceModels=%d)"), LodIndex, Mesh->GetNumSourceModels()));
		return;
	}
	const float ScreenSize = static_cast<float>(A.Num(TEXT("screenSize")));
	Mesh->GetSourceModel(LodIndex).ScreenSize.Default = ScreenSize;
	Mesh->bAutoComputeLODScreenSize = false;
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("lodIndex"), LodIndex);
	Ctx.Entry->SetNumberField(TEXT("screenSize"), ScreenSize);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_lod_screen_size editor only"));
#endif
}

bool FManageAssetStaticMeshCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(AssetPath);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("StaticMesh not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Mesh;
	return true;
}

void FManageAssetStaticMeshCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_material_slot"),        &HandleSM_SetMaterialSlot);
	OutHandlers.Add(TEXT("set_property"),             &HandleSM_SetProperty);
	OutHandlers.Add(TEXT("set_collision_trace_flag"), &HandleSM_SetCollisionTraceFlag);
	OutHandlers.Add(TEXT("add_box_collision"),        &HandleSM_AddBoxCollision);
	OutHandlers.Add(TEXT("add_sphere_collision"),     &HandleSM_AddSphereCollision);
	OutHandlers.Add(TEXT("clear_simple_collision"),   &HandleSM_ClearSimpleCollision);
	OutHandlers.Add(TEXT("add_socket"),               &HandleSM_AddSocket);
	OutHandlers.Add(TEXT("set_socket"),               &HandleSM_SetSocket);
	OutHandlers.Add(TEXT("remove_socket"),            &HandleSM_RemoveSocket);
	OutHandlers.Add(TEXT("set_lod_screen_size"),      &HandleSM_SetLodScreenSize);
}

REGISTER_MCP_CAPABILITY(FManageAssetStaticMeshCapability)
