// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetSkeletalMeshCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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

namespace
{
	bool ParseSkeletalMeshVec3(const TSharedPtr<FJsonObject>& Op, const TCHAR* XKey, const TCHAR* YKey, const TCHAR* ZKey,
		FVector& Out, FVector Default)
	{
		Out = Default;
		bool bAny = false;
		if (Op->HasField(XKey)) { Out.X = Op->GetNumberField(XKey); bAny = true; }
		if (Op->HasField(YKey)) { Out.Y = Op->GetNumberField(YKey); bAny = true; }
		if (Op->HasField(ZKey)) { Out.Z = Op->GetNumberField(ZKey); bAny = true; }
		return bAny;
	}

	USkeletalMeshSocket* FindMeshOnlySocket(USkeletalMesh* Mesh, FName SocketName)
	{
		if (!Mesh) return nullptr;
		for (USkeletalMeshSocket* S : Mesh->GetMeshOnlySocketList())
		{
			if (S && S->SocketName == SocketName) return S;
		}
		return nullptr;
	}
}

void FManageAssetSkeletalMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_skeletal_mesh");
	Out.SearchAssetTypes = {TEXT("SkeletalMesh")};
	Out.Description = TEXT("批量编辑 SkeletalMesh。材质槽/属性；网格 Socket；LOD ScreenSize。碰撞体请用 manage_asset_physics_asset。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"), {
			TEXT("set_material_slot"), TEXT("set_property"),
			TEXT("add_socket"), TEXT("remove_socket"), TEXT("set_socket"),
			TEXT("set_lod_screen_size")
		}))
		.Prop(TEXT("slotIndex"),      FNexusSchema::Int(TEXT("材质槽索引（set_material_slot）")))
		.Prop(TEXT("materialPath"),   FNexusSchema::Str(TEXT("材质资产路径（set_material_slot）")))
		.Prop(TEXT("propertyPath"),   FNexusSchema::Str(TEXT("属性路径（set_property）")))
		.Prop(TEXT("value"),          FNexusSchema::Str(TEXT("属性新值（set_property）")))
		.Prop(TEXT("socketName"), FNexusSchema::Str(TEXT("Socket 名")))
		.Prop(TEXT("boneName"), FNexusSchema::Str(TEXT("挂接骨骼名（add/set_socket）")))
		.Prop(TEXT("locX"), FNexusSchema::Num(TEXT("Socket 相对位置 X")))
		.Prop(TEXT("locY"), FNexusSchema::Num(TEXT("Socket 相对位置 Y")))
		.Prop(TEXT("locZ"), FNexusSchema::Num(TEXT("Socket 相对位置 Z")))
		.Prop(TEXT("pitch"), FNexusSchema::Num(TEXT("Socket 旋转 Pitch")))
		.Prop(TEXT("yaw"), FNexusSchema::Num(TEXT("Socket 旋转 Yaw")))
		.Prop(TEXT("roll"), FNexusSchema::Num(TEXT("Socket 旋转 Roll")))
		.Prop(TEXT("scaleX"), FNexusSchema::Num(TEXT("Socket 缩放 X")))
		.Prop(TEXT("scaleY"), FNexusSchema::Num(TEXT("Socket 缩放 Y")))
		.Prop(TEXT("scaleZ"), FNexusSchema::Num(TEXT("Socket 缩放 Z")))
		.Prop(TEXT("lodIndex"), FNexusSchema::Int(TEXT("LOD 索引（set_lod_screen_size）")))
		.Prop(TEXT("screenSize"), FNexusSchema::Num(TEXT("LOD ScreenSize（0–1）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SkeletalMesh 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mesh"), TEXT("material"), TEXT("skeletal"), TEXT("socket"), TEXT("lod"), TEXT("bone") };
	Out.RelatedCapabilities = { TEXT("get_asset_skeletal_mesh"), TEXT("get_asset_skeleton"), TEXT("manage_asset_physics_asset"), TEXT("save_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("改 SkeletalMesh 材质/Socket/LOD；物理体请走 PhysicsAsset");
}

FCapabilityResult FManageAssetSkeletalMeshCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(AssetPath);
		if (!Mesh)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("SkeletalMesh 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
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
					Entry->SetStringField(TEXT("error"), TEXT("set_material_slot 需要 materialPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
				if (!Material)
				{
					Material = FNexusAssetUtils::LoadAssetWithFallback<UMaterialInterface>(MaterialPath);
				}
				const TArray<FSkeletalMaterial>& Materials = FNexusAssetUtils::GetSkeletalMeshMaterials(*Mesh);
				if (!Materials.IsValidIndex(SlotIndex))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("材质槽索引 %d 超出范围 [0, %d)"), SlotIndex, Materials.Num()));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FSkeletalMaterial& MatEntry = const_cast<FSkeletalMaterial&>(Materials[SlotIndex]);
				MatEntry.MaterialInterface = Material;
				bDirty = true;
				Entry->SetNumberField(TEXT("slotIndex"), SlotIndex);
				Entry->SetStringField(TEXT("materialClass"), Material ? Material->GetClass()->GetName() : TEXT("None"));
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
			else if (Action.Equals(TEXT("set_property"), ESearchCase::IgnoreCase))
			{
				FString PropPath, Value;
				Op->TryGetStringField(TEXT("propertyPath"), PropPath);
				Op->TryGetStringField(TEXT("value"), Value);
				if (PropPath.IsEmpty() || Value.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyPath 和 value"));
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
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
			else if (Action.Equals(TEXT("add_socket"), ESearchCase::IgnoreCase)
				|| Action.Equals(TEXT("set_socket"), ESearchCase::IgnoreCase))
			{
				FString SocketName, BoneName;
				Op->TryGetStringField(TEXT("socketName"), SocketName);
				Op->TryGetStringField(TEXT("boneName"), BoneName);
				if (SocketName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("需要 socketName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const bool bAdd = Action.Equals(TEXT("add_socket"), ESearchCase::IgnoreCase);
				USkeletalMeshSocket* Socket = FindMeshOnlySocket(Mesh, FName(*SocketName));
				if (bAdd && Socket)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("网格 Socket 已存在: %s"), *SocketName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				if (!bAdd && !Socket)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("网格 Socket 未找到: %s（仅改 mesh-only，不含 Skeleton 插槽）"), *SocketName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				if (!Socket)
				{
					if (BoneName.IsEmpty())
					{
						Entry->SetStringField(TEXT("error"), TEXT("add_socket 需要 boneName"));
						OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
						continue;
					}
					Socket = NewObject<USkeletalMeshSocket>(Mesh);
					Socket->SocketName = FName(*SocketName);
					Socket->BoneName = FName(*BoneName);
					Mesh->GetMeshOnlySocketList().Add(Socket);
				}
				else if (!BoneName.IsEmpty())
				{
					Socket->BoneName = FName(*BoneName);
				}
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
				bDirty = true;
				Entry->SetStringField(TEXT("socketName"), SocketName);
				Entry->SetStringField(TEXT("boneName"), Socket->BoneName.ToString());
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
			else if (Action.Equals(TEXT("remove_socket"), ESearchCase::IgnoreCase))
			{
				FString SocketName;
				Op->TryGetStringField(TEXT("socketName"), SocketName);
				if (SocketName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("remove_socket 需要 socketName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
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
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("网格 Socket 未找到: %s"), *SocketName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				List.RemoveAt(Idx);
				bDirty = true;
				Entry->SetStringField(TEXT("removed"), SocketName);
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
			else if (Action.Equals(TEXT("set_lod_screen_size"), ESearchCase::IgnoreCase))
			{
#if WITH_EDITOR
				int32 LodIndex = 0;
				if (Op->HasField(TEXT("lodIndex"))) LodIndex = static_cast<int32>(Op->GetNumberField(TEXT("lodIndex")));
				if (!Op->HasField(TEXT("screenSize")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_lod_screen_size 需要 screenSize"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(LodIndex);
				if (!Info)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("LOD %d 不存在"), LodIndex));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const float ScreenSize = static_cast<float>(Op->GetNumberField(TEXT("screenSize")));
				Info->ScreenSize.Default = ScreenSize;
				bDirty = true;
				Entry->SetNumberField(TEXT("lodIndex"), LodIndex);
				Entry->SetNumberField(TEXT("screenSize"), ScreenSize);
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
#else
				Entry->SetStringField(TEXT("error"), TEXT("set_lod_screen_size 仅编辑器可用"));
#endif
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		if (bDirty) Mesh->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSkeletalMeshCapability)
