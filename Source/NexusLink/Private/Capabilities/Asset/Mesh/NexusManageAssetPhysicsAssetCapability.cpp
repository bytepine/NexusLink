// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetPhysicsAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "PhysicsEngine/PhysicsAsset.h"
#if NX_UE_HAS_SKELETAL_BODY_SETUP_HEADER
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

// ── 辅助：按骨骼名查找 BodySetup ─────────────────────────────────────────────

static USkeletalBodySetup* FindBodyByBone(UPhysicsAsset* PA, const FString& BoneName)
{
	for (USkeletalBodySetup* BS : PA->SkeletalBodySetups)
	{
		if (BS && BS->BoneName.ToString().Equals(BoneName, ESearchCase::IgnoreCase))
		{
			return BS;
		}
	}
	return nullptr;
}

// ── Capability ────────────────────────────────────────────────────────────────

void FManageAssetPhysicsAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_physics_asset");
	Out.SearchAssetTypes = {TEXT("PhysicsAsset")};
	Out.Description = TEXT("编辑 PhysicsAsset 形状与约束。见 operations[].action。");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("操作类型"),
			{
				TEXT("set_physics_type"),
				TEXT("add_sphere"),
				TEXT("add_capsule"),
				TEXT("add_box"),
				TEXT("clear_shapes"),
				TEXT("add_constraint"),
				TEXT("remove_constraint"),
			}))
		.Prop(TEXT("boneName"),    FNexusSchema::Str(TEXT("目标骨骼名（大多数操作必填）")))
		.Prop(TEXT("physicsType"), FNexusSchema::Enum(TEXT("set_physics_type"), { TEXT("Simulated"), TEXT("Kinematic"), TEXT("Default") }, TEXT("Simulated")))
		.Prop(TEXT("radius"),      FNexusSchema::Num(TEXT("球/胶囊半径")))
		.Prop(TEXT("halfHeight"),  FNexusSchema::Num(TEXT("胶囊半高")))
		.Prop(TEXT("extentX"),     FNexusSchema::Num(TEXT("Box X 半边长")))
		.Prop(TEXT("extentY"),     FNexusSchema::Num(TEXT("Box Y 半边长")))
		.Prop(TEXT("extentZ"),     FNexusSchema::Num(TEXT("Box Z 半边长")))
		.Prop(TEXT("bone1"),       FNexusSchema::Str(TEXT("add_constraint：骨骼1")))
		.Prop(TEXT("bone2"),       FNexusSchema::Str(TEXT("add_constraint：骨骼2")))
		.Prop(TEXT("jointName"),   FNexusSchema::Str(TEXT("remove_constraint：关节名")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("PhysicsAsset 资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("physics"), TEXT("ragdoll"), TEXT("body"), TEXT("collision"), TEXT("constraint"), TEXT("capsule"), TEXT("sphere") };
	Out.RelatedCapabilities = { TEXT("get_asset_physics_asset"), TEXT("get_asset_skeletal_mesh") };
	Out.WhenToUse = TEXT("给 PhysicsAsset 的骨骼添加碰撞形状、设置 PhysicsType、添加/移除关节约束");
}

FCapabilityResult FManageAssetPhysicsAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UPhysicsAsset* PA = FNexusAssetUtils::LoadAssetWithFallback<UPhysicsAsset>(AssetPath);
		if (!PA)
		{
			OutError = FString::Printf(TEXT("PhysicsAsset 未找到: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Ops;
		if (!Arguments->TryGetArrayField(TEXT("operations"), Ops) || !Ops)
		{
			OutError = TEXT("operations 为必填数组");
			return;
		}

		bool bDirty = false;

		for (const TSharedPtr<FJsonValue>& OpVal : *Ops)
		{
			TSharedPtr<FJsonObject> Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;

			TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			FString BoneName;
			Op->TryGetStringField(TEXT("boneName"), BoneName);

			if (Action == TEXT("set_physics_type"))
			{
				USkeletalBodySetup* BS = FindBodyByBone(PA, BoneName);
				if (!BS)
				{
					// 找不到时自动创建 Body
					BS = NewObject<USkeletalBodySetup>(PA, NAME_None, RF_Transactional);
					BS->BoneName = *BoneName;
					PA->SkeletalBodySetups.Add(BS);
				}
				FString TypeStr;
				Op->TryGetStringField(TEXT("physicsType"), TypeStr);
				if (TypeStr == TEXT("Simulated"))
					BS->PhysicsType = EPhysicsType::PhysType_Simulated;
				else if (TypeStr == TEXT("Kinematic"))
					BS->PhysicsType = EPhysicsType::PhysType_Kinematic;
				else
					BS->PhysicsType = EPhysicsType::PhysType_Default;
				bDirty = true;
			}
			else if (Action == TEXT("add_sphere"))
			{
				USkeletalBodySetup* BS = FindBodyByBone(PA, BoneName);
				if (!BS)
				{
					BS = NewObject<USkeletalBodySetup>(PA, NAME_None, RF_Transactional);
					BS->BoneName = *BoneName;
					PA->SkeletalBodySetups.Add(BS);
				}
				FKSphereElem Sphere;
				double Radius = 10.0;
				Op->TryGetNumberField(TEXT("radius"), Radius);
				Sphere.Radius = static_cast<float>(Radius);
				BS->AggGeom.SphereElems.Add(Sphere);
				OpResult->SetNumberField(TEXT("radius"), Radius);
				bDirty = true;
			}
			else if (Action == TEXT("add_capsule"))
			{
				USkeletalBodySetup* BS = FindBodyByBone(PA, BoneName);
				if (!BS)
				{
					BS = NewObject<USkeletalBodySetup>(PA, NAME_None, RF_Transactional);
					BS->BoneName = *BoneName;
					PA->SkeletalBodySetups.Add(BS);
				}
				FKSphylElem Capsule;
				double Radius = 10.0, HalfHeight = 20.0;
				Op->TryGetNumberField(TEXT("radius"),     Radius);
				Op->TryGetNumberField(TEXT("halfHeight"), HalfHeight);
				Capsule.Radius     = static_cast<float>(Radius);
				Capsule.Length     = static_cast<float>(HalfHeight * 2.0);
				BS->AggGeom.SphylElems.Add(Capsule);
				bDirty = true;
			}
			else if (Action == TEXT("add_box"))
			{
				USkeletalBodySetup* BS = FindBodyByBone(PA, BoneName);
				if (!BS)
				{
					BS = NewObject<USkeletalBodySetup>(PA, NAME_None, RF_Transactional);
					BS->BoneName = *BoneName;
					PA->SkeletalBodySetups.Add(BS);
				}
				FKBoxElem Box;
				double X = 10.0, Y = 10.0, Z = 10.0;
				Op->TryGetNumberField(TEXT("extentX"), X);
				Op->TryGetNumberField(TEXT("extentY"), Y);
				Op->TryGetNumberField(TEXT("extentZ"), Z);
				Box.X = static_cast<float>(X * 2.0);
				Box.Y = static_cast<float>(Y * 2.0);
				Box.Z = static_cast<float>(Z * 2.0);
				BS->AggGeom.BoxElems.Add(Box);
				bDirty = true;
			}
			else if (Action == TEXT("clear_shapes"))
			{
				USkeletalBodySetup* BS = FindBodyByBone(PA, BoneName);
				if (!BS)
				{
					OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未找到骨骼 '%s' 的 Body"), *BoneName));
				}
				else
				{
					BS->AggGeom.SphereElems.Empty();
					BS->AggGeom.BoxElems.Empty();
					BS->AggGeom.SphylElems.Empty();
					BS->AggGeom.ConvexElems.Empty();
					bDirty = true;
				}
			}
			else if (Action == TEXT("add_constraint"))
			{
				FString Bone1, Bone2;
				if (!Op->TryGetStringField(TEXT("bone1"), Bone1) || !Op->TryGetStringField(TEXT("bone2"), Bone2))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_constraint 需要 bone1 和 bone2"));
				}
				else
				{
					UPhysicsConstraintTemplate* CT = NewObject<UPhysicsConstraintTemplate>(PA, NAME_None, RF_Transactional);
					CT->DefaultInstance.ConstraintBone1 = *Bone1;
					CT->DefaultInstance.ConstraintBone2 = *Bone2;
					CT->DefaultInstance.JointName = *FString::Printf(TEXT("%s_%s"), *Bone1, *Bone2);
					PA->ConstraintSetup.Add(CT);
					OpResult->SetStringField(TEXT("jointName"), CT->DefaultInstance.JointName.ToString());
					bDirty = true;
				}
			}
			else if (Action == TEXT("remove_constraint"))
			{
				FString JointName;
				Op->TryGetStringField(TEXT("jointName"), JointName);
				int32 Before = PA->ConstraintSetup.Num();
				PA->ConstraintSetup.RemoveAll([&](UPhysicsConstraintTemplate* CT)
				{
					return CT && CT->DefaultInstance.JointName.ToString().Equals(JointName, ESearchCase::IgnoreCase);
				});
				int32 Removed = Before - PA->ConstraintSetup.Num();
				OpResult->SetNumberField(TEXT("removedCount"), Removed);
				if (Removed > 0) bDirty = true;
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OpResult));
		}

		if (bDirty)
		{
			PA->MarkPackageDirty();
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetPhysicsAssetCapability)
