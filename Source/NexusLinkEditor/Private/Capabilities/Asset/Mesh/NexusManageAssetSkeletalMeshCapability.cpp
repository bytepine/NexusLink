// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetSkeletalMeshCapability.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Materials/MaterialInterface.h"
#if NX_UE_HAS_SKELETAL_MATERIAL_COMMON_HEADER
#include "Engine/SkinnedAssetCommon.h"
#endif
#include "NexusMcpTool.h"

static bool ParseSkeletalMeshVec3(const TSharedPtr<FJsonObject>& Op, const TCHAR* XKey, const TCHAR* YKey, const TCHAR* ZKey,
		FVector& Out, FVector Default)
{
	Out = Default;
	bool bAny = false;
	if (Op->HasField(XKey)) { Out.X = Op->GetNumberField(XKey); bAny = true; }
	if (Op->HasField(YKey)) { Out.Y = Op->GetNumberField(YKey); bAny = true; }
	if (Op->HasField(ZKey)) { Out.Z = Op->GetNumberField(ZKey); bAny = true; }
	return bAny;
}

static USkeletalMeshSocket* FindMeshOnlySocket(USkeletalMesh* Mesh, FName SocketName)
{
	if (!Mesh) return nullptr;
	for (USkeletalMeshSocket* S : Mesh->GetMeshOnlySocketList())
	{
		if (S && S->SocketName == SocketName) return S;
	}
	return nullptr;
}

void FManageAssetSkeletalMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_skeletal_mesh");
	Out.SearchAssetTypes = {TEXT("SkeletalMesh")};
	Out.Description = TEXT("Batch edit SkeletalMesh. Collision via manage_asset_physics_asset.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), {
			TEXT("set_material_slot"), TEXT("set_property"),
			TEXT("add_socket"), TEXT("remove_socket"), TEXT("set_socket"),
			TEXT("set_lod_screen_size")
		}))
		.Prop(TEXT("slotIndex"),      FNexusSchema::Int(TEXT("Material slot index (set_material_slot)")))
		.Prop(TEXT("materialPath"),   FNexusSchema::Str(TEXT("Material asset path (set_material_slot)")))
		.Prop(TEXT("propertyPath"),   FNexusSchema::Str(TEXT("Property path (set_property)")))
		.Prop(TEXT("value"),          FNexusSchema::Str(TEXT("New property value (set_property)")))
		.Prop(TEXT("socketName"), FNexusSchema::Str(TEXT("Socket name")))
		.Prop(TEXT("boneName"), FNexusSchema::Str(TEXT("Attach bone name (add/set_socket)")))
		.Prop(TEXT("locX"), FNexusSchema::Num(TEXT("Socket relative position X")))
		.Prop(TEXT("locY"), FNexusSchema::Num(TEXT("Socket relative position Y")))
		.Prop(TEXT("locZ"), FNexusSchema::Num(TEXT("Socket relative position Z")))
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
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SkeletalMesh asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mesh"), TEXT("material"), TEXT("skeletal"), TEXT("socket"), TEXT("lod"), TEXT("bone") };
	Out.RelatedCapabilities = { TEXT("get_asset_skeletal_mesh"), TEXT("get_asset_skeleton"), TEXT("manage_asset_physics_asset"), TEXT("save_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("Edit SkeletalMesh material/Socket/LOD; physics via PhysicsAsset");
}

static USkeletalMesh* SkelFrom(FNexusActionContext& Ctx)
{
	return static_cast<USkeletalMesh*>(Ctx.Target);
}

static void MarkSkelDirty(FNexusActionContext& Ctx)
{
	if (USkeletalMesh* Mesh = SkelFrom(Ctx))
	{
		Mesh->MarkPackageDirty();
	}
}

static void ApplySkelSocketFields(USkeletalMeshSocket* Socket, const TSharedPtr<FJsonObject>& Op)
{
	FVector Loc = Socket->RelativeLocation;
	FRotator Rot = Socket->RelativeRotation;
	FVector Scale = Socket->RelativeScale;
	ParseSkeletalMeshVec3(Op, TEXT("locX"), TEXT("locY"), TEXT("locZ"), Loc, Loc);
	if (Op->HasField(TEXT("pitch"))) Rot.Pitch = Op->GetNumberField(TEXT("pitch"));
	if (Op->HasField(TEXT("yaw"))) Rot.Yaw = Op->GetNumberField(TEXT("yaw"));
	if (Op->HasField(TEXT("roll"))) Rot.Roll = Op->GetNumberField(TEXT("roll"));
	ParseSkeletalMeshVec3(Op, TEXT("scaleX"), TEXT("scaleY"), TEXT("scaleZ"), Scale, Scale);
	Socket->RelativeLocation = Loc;
	Socket->RelativeRotation = Rot;
	Socket->RelativeScale = Scale;
}

static void HandleSkel_SetMaterialSlot(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeletalMesh* Mesh = SkelFrom(Ctx);
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
	const TArray<FSkeletalMaterial>& Materials = FNexusAssetUtils::GetSkeletalMeshMaterials(*Mesh);
	if (!Materials.IsValidIndex(SlotIndex))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Material slot index %d out of range [0, %d)"), SlotIndex, Materials.Num()));
		return;
	}
	FSkeletalMaterial& MatEntry = const_cast<FSkeletalMaterial&>(Materials[SlotIndex]);
	MatEntry.MaterialInterface = Material;
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("slotIndex"), SlotIndex);
	Ctx.Entry->SetStringField(TEXT("materialClass"), Material ? Material->GetClass()->GetName() : TEXT("None"));
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSkel_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeletalMesh* Mesh = SkelFrom(Ctx);
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
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSkel_AddSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeletalMesh* Mesh = SkelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString SocketName = A.Str(TEXT("socketName"));
	const FString BoneName = A.Str(TEXT("boneName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("socketName required"));
		return;
	}
	if (FindMeshOnlySocket(Mesh, FName(*SocketName)))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Mesh Socket already exists: %s"), *SocketName));
		return;
	}
	if (BoneName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_socket requires boneName"));
		return;
	}
	USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Mesh);
	Socket->SocketName = FName(*SocketName);
	Socket->BoneName = FName(*BoneName);
	Mesh->GetMeshOnlySocketList().Add(Socket);
	ApplySkelSocketFields(Socket, Op);
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("socketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("boneName"), Socket->BoneName.ToString());
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSkel_SetSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeletalMesh* Mesh = SkelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString SocketName = A.Str(TEXT("socketName"));
	const FString BoneName = A.Str(TEXT("boneName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("socketName required"));
		return;
	}
	USkeletalMeshSocket* Socket = FindMeshOnlySocket(Mesh, FName(*SocketName));
	if (!Socket)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Mesh Socket not found: %s (mesh-only; not Skeleton socket)"), *SocketName));
		return;
	}
	if (!BoneName.IsEmpty())
	{
		Socket->BoneName = FName(*BoneName);
	}
	ApplySkelSocketFields(Socket, Op);
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("socketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("boneName"), Socket->BoneName.ToString());
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSkel_RemoveSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeletalMesh* Mesh = SkelFrom(Ctx);
	const FString SocketName = FNexusArgs(Op).Str(TEXT("socketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_socket requires socketName"));
		return;
	}
	auto& List = Mesh->GetMeshOnlySocketList();
	const FName Target(*SocketName);
	int32 Idx = INDEX_NONE;
	for (int32 i = 0; i < List.Num(); ++i)
	{
		if (List[i] && List[i]->SocketName == Target)
		{
			Idx = i;
			break;
		}
	}
	if (Idx == INDEX_NONE)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Mesh Socket not found: %s"), *SocketName));
		return;
	}
	List.RemoveAt(Idx);
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("removed"), SocketName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleSkel_SetLodScreenSize(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	USkeletalMesh* Mesh = SkelFrom(Ctx);
	const FNexusArgs A(Op);
	const int32 LodIndex = static_cast<int32>(A.Num(TEXT("lodIndex")));
	if (!Op->HasField(TEXT("screenSize")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_lod_screen_size requires screenSize"));
		return;
	}
	FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(LodIndex);
	if (!Info)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("LOD %d does not exist"), LodIndex));
		return;
	}
	const float ScreenSize = static_cast<float>(A.Num(TEXT("screenSize")));
	Info->ScreenSize.Default = ScreenSize;
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("lodIndex"), LodIndex);
	Ctx.Entry->SetNumberField(TEXT("screenSize"), ScreenSize);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_lod_screen_size editor only"));
#endif
}

bool FManageAssetSkeletalMeshCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(AssetPath);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Mesh;
	return true;
}

void FManageAssetSkeletalMeshCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_material_slot"),   &HandleSkel_SetMaterialSlot);
	OutHandlers.Add(TEXT("set_property"),        &HandleSkel_SetProperty);
	OutHandlers.Add(TEXT("add_socket"),          &HandleSkel_AddSocket);
	OutHandlers.Add(TEXT("set_socket"),          &HandleSkel_SetSocket);
	OutHandlers.Add(TEXT("remove_socket"),       &HandleSkel_RemoveSocket);
	OutHandlers.Add(TEXT("set_lod_screen_size"), &HandleSkel_SetLodScreenSize);
}

REGISTER_MCP_CAPABILITY(FManageAssetSkeletalMeshCapability)
