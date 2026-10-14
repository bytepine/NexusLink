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

void FNexusManageAssetSkeletalMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetSkeletalMesh");
	Out.SearchAssetTypes = {TEXT("SkeletalMesh")};
	Out.Description = TEXT("Batch edit SkeletalMesh. Collision via ManageAssetPhysicsAsset.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), {
			TEXT("SetMaterialSlot"), TEXT("SetProperty"),
			TEXT("AddSocket"), TEXT("RemoveSocket"), TEXT("SetSocket"),
			TEXT("SetLODScreenSize")
		}))
		.Prop(TEXT("SlotIndex"),      FNexusSchema::Int(TEXT("Material slot index (SetMaterialSlot)")))
		.Prop(TEXT("MaterialPath"),   FNexusSchema::Str(TEXT("Material asset path (SetMaterialSlot)")))
		.Prop(TEXT("PropertyPath"),   FNexusSchema::Str(TEXT("Property path (SetProperty)")))
		.Prop(TEXT("Value"),          FNexusSchema::Str(TEXT("New property value (SetProperty)")))
		.Prop(TEXT("SocketName"), FNexusSchema::Str(TEXT("Socket name")))
		.Prop(TEXT("BoneName"), FNexusSchema::Str(TEXT("Attach bone name (add/SetSocket)")))
		.Prop(TEXT("LocX"), FNexusSchema::Num(TEXT("Socket relative position X")))
		.Prop(TEXT("LocY"), FNexusSchema::Num(TEXT("Socket relative position Y")))
		.Prop(TEXT("LocZ"), FNexusSchema::Num(TEXT("Socket relative position Z")))
		.Prop(TEXT("Pitch"), FNexusSchema::Num(TEXT("Socket rotation Pitch")))
		.Prop(TEXT("Yaw"), FNexusSchema::Num(TEXT("Socket rotation Yaw")))
		.Prop(TEXT("Roll"), FNexusSchema::Num(TEXT("Socket rotation Roll")))
		.Prop(TEXT("ScaleX"), FNexusSchema::Num(TEXT("Socket rotation Scale X")))
		.Prop(TEXT("ScaleY"), FNexusSchema::Num(TEXT("Socket rotation Scale Y")))
		.Prop(TEXT("ScaleZ"), FNexusSchema::Num(TEXT("Socket rotation Scale Z")))
		.Prop(TEXT("LODIndex"), FNexusSchema::Int(TEXT("LOD index (SetLODScreenSize)")))
		.Prop(TEXT("ScreenSize"), FNexusSchema::Num(TEXT("LOD ScreenSize（0–1）")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("SkeletalMesh asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mesh"), TEXT("Material"), TEXT("skeletal"), TEXT("socket"), TEXT("lod"), TEXT("Bone") };
	Out.RelatedCapabilities = { TEXT("GetAssetSkeletalMesh"), TEXT("GetAssetSkeleton"), TEXT("ManageAssetPhysicsAsset"), TEXT("SaveAsset") };
	Out.Prerequisites = { TEXT("EditorOnly") };
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
	ParseSkeletalMeshVec3(Op, TEXT("LocX"), TEXT("LocY"), TEXT("LocZ"), Loc, Loc);
	if (Op->HasField(TEXT("Pitch"))) Rot.Pitch = Op->GetNumberField(TEXT("Pitch"));
	if (Op->HasField(TEXT("Yaw"))) Rot.Yaw = Op->GetNumberField(TEXT("Yaw"));
	if (Op->HasField(TEXT("Roll"))) Rot.Roll = Op->GetNumberField(TEXT("Roll"));
	ParseSkeletalMeshVec3(Op, TEXT("ScaleX"), TEXT("ScaleY"), TEXT("ScaleZ"), Scale, Scale);
	Socket->RelativeLocation = Loc;
	Socket->RelativeRotation = Rot;
	Socket->RelativeScale = Scale;
}

static void HandleSkel_SetMaterialSlot(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeletalMesh* Mesh = SkelFrom(Ctx);
	const FNexusArgs A(Op);
	const int32 SlotIndex = static_cast<int32>(A.Num(TEXT("SlotIndex")));
	const FString MaterialPath = A.Str(TEXT("MaterialPath"));
	if (MaterialPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetMaterialSlot requires MaterialPath"));
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
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Material slot index %d out of range [0, %d)"), SlotIndex, Materials.Num()));
		return;
	}
	FSkeletalMaterial& MatEntry = const_cast<FSkeletalMaterial&>(Materials[SlotIndex]);
	MatEntry.MaterialInterface = Material;
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("SlotIndex"), SlotIndex);
	Ctx.Entry->SetStringField(TEXT("MaterialClass"), Material ? Material->GetClass()->GetName() : TEXT("None"));
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSkel_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeletalMesh* Mesh = SkelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("PropertyPath"));
	const FString Value = A.Str(TEXT("Value"));
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Mesh, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), Err);
		return;
	}
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSkel_AddSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeletalMesh* Mesh = SkelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString SocketName = A.Str(TEXT("SocketName"));
	const FString BoneName = A.Str(TEXT("BoneName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SocketName required"));
		return;
	}
	if (FindMeshOnlySocket(Mesh, FName(*SocketName)))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Mesh Socket already exists: %s"), *SocketName));
		return;
	}
	if (BoneName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddSocket requires BoneName"));
		return;
	}
	USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Mesh);
	Socket->SocketName = FName(*SocketName);
	Socket->BoneName = FName(*BoneName);
	Mesh->GetMeshOnlySocketList().Add(Socket);
	ApplySkelSocketFields(Socket, Op);
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("SocketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("BoneName"), Socket->BoneName.ToString());
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSkel_SetSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeletalMesh* Mesh = SkelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString SocketName = A.Str(TEXT("SocketName"));
	const FString BoneName = A.Str(TEXT("BoneName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SocketName required"));
		return;
	}
	USkeletalMeshSocket* Socket = FindMeshOnlySocket(Mesh, FName(*SocketName));
	if (!Socket)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Mesh Socket not found: %s (mesh-only; not Skeleton socket)"), *SocketName));
		return;
	}
	if (!BoneName.IsEmpty())
	{
		Socket->BoneName = FName(*BoneName);
	}
	ApplySkelSocketFields(Socket, Op);
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("SocketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("BoneName"), Socket->BoneName.ToString());
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSkel_RemoveSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USkeletalMesh* Mesh = SkelFrom(Ctx);
	const FString SocketName = FNexusArgs(Op).Str(TEXT("SocketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveSocket requires SocketName"));
		return;
	}
	auto& List = Mesh->GetMeshOnlySocketList();
	const FName Target(*SocketName);
	int32 Idx = INDEX_NONE;
	for (int32 ListIndex = 0; ListIndex < List.Num(); ++ListIndex)
	{
		if (List[ListIndex] && List[ListIndex]->SocketName == Target)
		{
			Idx = ListIndex;
			break;
		}
	}
	if (Idx == INDEX_NONE)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Mesh Socket not found: %s"), *SocketName));
		return;
	}
	List.RemoveAt(Idx);
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("bRemoved"), SocketName);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSkel_SetLodScreenSize(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	USkeletalMesh* Mesh = SkelFrom(Ctx);
	const FNexusArgs A(Op);
	const int32 LodIndex = static_cast<int32>(A.Num(TEXT("LODIndex")));
	if (!Op->HasField(TEXT("ScreenSize")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetLODScreenSize requires ScreenSize"));
		return;
	}
	FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(LodIndex);
	if (!Info)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("LOD %d does not exist"), LodIndex));
		return;
	}
	const float ScreenSize = static_cast<float>(A.Num(TEXT("ScreenSize")));
	Info->ScreenSize.Default = ScreenSize;
	MarkSkelDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("LODIndex"), LodIndex);
	Ctx.Entry->SetNumberField(TEXT("ScreenSize"), ScreenSize);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
#else
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetLODScreenSize editor only"));
#endif
}

bool FNexusManageAssetSkeletalMeshCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(AssetPath);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Mesh;
	return true;
}

void FNexusManageAssetSkeletalMeshCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetMaterialSlot"),   &HandleSkel_SetMaterialSlot);
	OutHandlers.Add(TEXT("SetProperty"),        &HandleSkel_SetProperty);
	OutHandlers.Add(TEXT("AddSocket"),          &HandleSkel_AddSocket);
	OutHandlers.Add(TEXT("SetSocket"),          &HandleSkel_SetSocket);
	OutHandlers.Add(TEXT("RemoveSocket"),       &HandleSkel_RemoveSocket);
	OutHandlers.Add(TEXT("SetLODScreenSize"), &HandleSkel_SetLodScreenSize);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetSkeletalMeshCapability)
