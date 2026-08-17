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
		OutError = TEXT("collisionTraceFlag 须为 UseDefault/UseSimpleAndComplex/UseSimpleAsComplex/UseComplexAsSimple");
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
	Out.Description = TEXT("批量编辑 StaticMesh。材质槽/属性；碰撞复杂度与简易 Box/Sphere；Socket；LOD ScreenSize。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"), {
			TEXT("set_material_slot"), TEXT("set_property"),
			TEXT("set_collision_trace_flag"), TEXT("add_box_collision"), TEXT("add_sphere_collision"),
			TEXT("clear_simple_collision"),
			TEXT("add_socket"), TEXT("remove_socket"), TEXT("set_socket"),
			TEXT("set_lod_screen_size")
		}))
		.Prop(TEXT("slotIndex"),      FNexusSchema::Int(TEXT("材质槽索引（set_material_slot）")))
		.Prop(TEXT("materialPath"),   FNexusSchema::Str(TEXT("材质资产路径（set_material_slot）")))
		.Prop(TEXT("propertyPath"),   FNexusSchema::Str(TEXT("属性路径（set_property）")))
		.Prop(TEXT("value"),          FNexusSchema::Str(TEXT("属性新值（set_property）")))
		.Prop(TEXT("collisionTraceFlag"), FNexusSchema::Enum(TEXT("碰撞复杂度"), {
			TEXT("UseDefault"), TEXT("UseSimpleAndComplex"), TEXT("UseSimpleAsComplex"), TEXT("UseComplexAsSimple")
		}))
		.Prop(TEXT("x"), FNexusSchema::Num(TEXT("位置/半轴 X（碰撞中心或 Box 半轴）")))
		.Prop(TEXT("y"), FNexusSchema::Num(TEXT("位置/半轴 Y")))
		.Prop(TEXT("z"), FNexusSchema::Num(TEXT("位置/半轴 Z")))
		.Prop(TEXT("extentX"), FNexusSchema::Num(TEXT("Box 半轴 X（默认用 x）")))
		.Prop(TEXT("extentY"), FNexusSchema::Num(TEXT("Box 半轴 Y（默认用 y）")))
		.Prop(TEXT("extentZ"), FNexusSchema::Num(TEXT("Box 半轴 Z（默认用 z）")))
		.Prop(TEXT("radius"), FNexusSchema::Num(TEXT("Sphere 半径（add_sphere_collision）")))
		.Prop(TEXT("socketName"), FNexusSchema::Str(TEXT("Socket 名")))
		.Prop(TEXT("locX"), FNexusSchema::Num(TEXT("Socket/碰撞中心 X")))
		.Prop(TEXT("locY"), FNexusSchema::Num(TEXT("Socket/碰撞中心 Y")))
		.Prop(TEXT("locZ"), FNexusSchema::Num(TEXT("Socket/碰撞中心 Z")))
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
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("StaticMesh 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mesh"), TEXT("material"), TEXT("collision"), TEXT("socket"), TEXT("lod"), TEXT("static") };
	Out.RelatedCapabilities = { TEXT("get_asset_static_mesh"), TEXT("search_asset"), TEXT("save_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("改 StaticMesh 材质/碰撞/Socket/LOD；修改后需 save_asset 落盘");
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
				FString::Printf(TEXT("StaticMesh 未找到: %s"), *AssetPath));
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
				const TArray<FStaticMaterial>& Materials = FNexusAssetUtils::GetStaticMeshMaterials(*Mesh);
				if (!Materials.IsValidIndex(SlotIndex))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("材质槽索引 %d 超出范围 [0, %d)"), SlotIndex, Materials.Num()));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
#if WITH_EDITOR
				Mesh->SetMaterial(SlotIndex, Material);
				bDirty = true;
				Entry->SetNumberField(TEXT("slotIndex"), SlotIndex);
				Entry->SetStringField(TEXT("materialClass"), Material ? Material->GetClass()->GetName() : TEXT("None"));
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
#else
				Entry->SetStringField(TEXT("error"), TEXT("set_material_slot 仅在编辑器构建可用"));
#endif
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
					Entry->SetStringField(TEXT("error"), TEXT("无法创建 BodySetup"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Body->CollisionTraceFlag = Flag;
				InvalidateCollision(Mesh, Body);
				bDirty = true;
				Entry->SetStringField(TEXT("collisionTraceFlag"), FlagText.IsEmpty() ? TEXT("UseDefault") : FlagText);
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
			else if (Action.Equals(TEXT("add_box_collision"), ESearchCase::IgnoreCase))
			{
				UBodySetup* Body = EnsureBodySetup(Mesh);
				if (!Body)
				{
					Entry->SetStringField(TEXT("error"), TEXT("无法创建 BodySetup"));
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
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
			else if (Action.Equals(TEXT("add_sphere_collision"), ESearchCase::IgnoreCase))
			{
				UBodySetup* Body = EnsureBodySetup(Mesh);
				if (!Body)
				{
					Entry->SetStringField(TEXT("error"), TEXT("无法创建 BodySetup"));
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
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
			else if (Action.Equals(TEXT("clear_simple_collision"), ESearchCase::IgnoreCase))
			{
				UBodySetup* Body = EnsureBodySetup(Mesh);
				if (!Body)
				{
					Entry->SetStringField(TEXT("error"), TEXT("无法创建 BodySetup"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Body->RemoveSimpleCollision();
				InvalidateCollision(Mesh, Body);
				bDirty = true;
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
			else if (Action.Equals(TEXT("add_socket"), ESearchCase::IgnoreCase)
				|| Action.Equals(TEXT("set_socket"), ESearchCase::IgnoreCase))
			{
				FString SocketName;
				Op->TryGetStringField(TEXT("socketName"), SocketName);
				if (SocketName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("需要 socketName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UStaticMeshSocket* Socket = Mesh->FindSocket(FName(*SocketName));
				const bool bAdd = Action.Equals(TEXT("add_socket"), ESearchCase::IgnoreCase);
				if (bAdd && Socket)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket 已存在: %s"), *SocketName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				if (!bAdd && !Socket)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket 未找到: %s"), *SocketName));
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
				UStaticMeshSocket* Socket = Mesh->FindSocket(FName(*SocketName));
				if (!Socket)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Socket 未找到: %s"), *SocketName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Mesh->RemoveSocket(Socket);
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
				if (!Mesh->IsSourceModelValid(LodIndex))
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("LOD %d 不存在（sourceModels=%d）"), LodIndex, Mesh->GetNumSourceModels()));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const float ScreenSize = static_cast<float>(Op->GetNumberField(TEXT("screenSize")));
				Mesh->GetSourceModel(LodIndex).ScreenSize.Default = ScreenSize;
				Mesh->bAutoComputeLODScreenSize = false;
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

REGISTER_MCP_CAPABILITY(FManageAssetStaticMeshCapability)
