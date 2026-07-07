// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusGetAssetPhysicsAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "PhysicsEngine/PhysicsAsset.h"
// UE 5.5+ 将 USkeletalBodySetup 移至独立头文件；5.4 及以下通过 PhysicsAsset.h 已包含
#if NX_UE_HAS_SKELETAL_BODY_SETUP_HEADER
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

// ── 辅助：PhysicsType 枚举转字符串 ───────────────────────────────────────────

static FString PhysicsTypeToStr(EPhysicsType Type)
{
	switch (Type)
	{
		case EPhysicsType::PhysType_Simulated: return TEXT("Simulated");
		case EPhysicsType::PhysType_Kinematic: return TEXT("Kinematic");
		case EPhysicsType::PhysType_Default:   return TEXT("Default");
		default:                               return TEXT("Unknown");
	}
}

// ── Capability ────────────────────────────────────────────────────────────────

void FGetAssetPhysicsAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_physics_asset");
	Out.Description = TEXT("列举 PhysicsAsset 的 Body（骨骼/碰撞形状）和 Constraint（约束关节）概览。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("PhysicsAsset 资产路径")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("physics"), TEXT("physasset"), TEXT("ragdoll"), TEXT("body"), TEXT("constraint"), TEXT("collision"), TEXT("skeleton") };
	Out.RelatedCapabilities = { TEXT("manage_asset_physics_asset"), TEXT("get_asset_skeletal_mesh"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("读 PhysicsAsset 的 Body 骨骼名/碰撞形状数量与 Constraint 列表");
}

FCapabilityResult FGetAssetPhysicsAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

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

		OutEntry->SetStringField(TEXT("assetType"),       TEXT("PhysicsAsset"));
		OutEntry->SetStringField(TEXT("name"),            PA->GetName());
		OutEntry->SetStringField(TEXT("path"),            FNexusAssetUtils::PackagePathOf(PA));
		OutEntry->SetNumberField(TEXT("bodiesCount"),     PA->SkeletalBodySetups.Num());
		OutEntry->SetNumberField(TEXT("constraintsCount"), PA->ConstraintSetup.Num());

		// Body setups
		TArray<TSharedPtr<FJsonValue>> BodiesArr;
		for (USkeletalBodySetup* BS : PA->SkeletalBodySetups)
		{
			if (!BS) continue;
			TSharedPtr<FJsonObject> BObj = MakeShared<FJsonObject>();
			BObj->SetStringField(TEXT("boneName"),    BS->BoneName.ToString());
			BObj->SetStringField(TEXT("physicsType"), PhysicsTypeToStr(BS->PhysicsType));
			BObj->SetNumberField(TEXT("spheresCount"),   BS->AggGeom.SphereElems.Num());
			BObj->SetNumberField(TEXT("boxesCount"),     BS->AggGeom.BoxElems.Num());
			BObj->SetNumberField(TEXT("capsulesCount"),  BS->AggGeom.SphylElems.Num());
			BObj->SetNumberField(TEXT("convexCount"),    BS->AggGeom.ConvexElems.Num());
			BodiesArr.Add(MakeShared<FJsonValueObject>(BObj));
		}
		OutEntry->SetArrayField(TEXT("bodies"), BodiesArr);

		// Constraints
		TArray<TSharedPtr<FJsonValue>> ConstrArr;
		for (UPhysicsConstraintTemplate* CT : PA->ConstraintSetup)
		{
			if (!CT) continue;
			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("jointName"),  CT->DefaultInstance.JointName.ToString());
			CObj->SetStringField(TEXT("bone1"),      CT->DefaultInstance.ConstraintBone1.ToString());
			CObj->SetStringField(TEXT("bone2"),      CT->DefaultInstance.ConstraintBone2.ToString());
			ConstrArr.Add(MakeShared<FJsonValueObject>(CObj));
		}
		OutEntry->SetArrayField(TEXT("constraints"), ConstrArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetPhysicsAssetCapability)
