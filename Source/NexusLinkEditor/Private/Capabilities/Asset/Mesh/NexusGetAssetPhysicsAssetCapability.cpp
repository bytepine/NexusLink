// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusGetAssetPhysicsAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
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

void FNexusGetAssetPhysicsAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetPhysicsAsset");
	Out.SearchAssetTypes = {TEXT("PhysicsAsset")};
	Out.Description = TEXT("List PhysicsAsset Bodies and Constraints overview.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("PhysicsAsset asset path")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("physics"), TEXT("physasset"), TEXT("ragdoll"), TEXT("body"), TEXT("constraint"), TEXT("Collision"), TEXT("Skeleton") };
	Out.RelatedCapabilities = { TEXT("ManageAssetPhysicsAsset"), TEXT("GetAssetSkeletalMesh"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Read PhysicsAsset Body bone names/shape counts and Constraint list");
}

FCapabilityResult FNexusGetAssetPhysicsAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		UPhysicsAsset* PA = FNexusAssetUtils::LoadAssetWithFallback<UPhysicsAsset>(AssetPath);
		if (!PA)
		{
			OutError = FString::Printf(TEXT("PhysicsAsset not found: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("AssetType"),       TEXT("PhysicsAsset"));
		OutEntry->SetStringField(TEXT("name"),            PA->GetName());
		OutEntry->SetStringField(TEXT("Path"),            FNexusAssetUtils::PackagePathOf(PA));
		OutEntry->SetNumberField(TEXT("BodiesCount"),     PA->SkeletalBodySetups.Num());
		OutEntry->SetNumberField(TEXT("ConstraintsCount"), PA->ConstraintSetup.Num());

		// Body setups
		TArray<TSharedPtr<FJsonValue>> BodiesArr;
		for (USkeletalBodySetup* BS : PA->SkeletalBodySetups)
		{
			if (!BS) continue;
			TSharedPtr<FJsonObject> BObj = MakeShared<FJsonObject>();
			BObj->SetStringField(TEXT("BoneName"),    BS->BoneName.ToString());
			BObj->SetStringField(TEXT("PhysicsType"), PhysicsTypeToStr(BS->PhysicsType));
			BObj->SetNumberField(TEXT("SpheresCount"),   BS->AggGeom.SphereElems.Num());
			BObj->SetNumberField(TEXT("BoxesCount"),     BS->AggGeom.BoxElems.Num());
			BObj->SetNumberField(TEXT("CapsulesCount"),  BS->AggGeom.SphylElems.Num());
			BObj->SetNumberField(TEXT("ConvexCount"),    BS->AggGeom.ConvexElems.Num());
			BodiesArr.Add(MakeShared<FJsonValueObject>(BObj));
		}
		OutEntry->SetArrayField(TEXT("Bodies"), BodiesArr);

		// Constraints
		TArray<TSharedPtr<FJsonValue>> ConstrArr;
		for (UPhysicsConstraintTemplate* CT : PA->ConstraintSetup)
		{
			if (!CT) continue;
			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("JointName"),  CT->DefaultInstance.JointName.ToString());
			CObj->SetStringField(TEXT("Bone1"),      CT->DefaultInstance.ConstraintBone1.ToString());
			CObj->SetStringField(TEXT("Bone2"),      CT->DefaultInstance.ConstraintBone2.ToString());
			ConstrArr.Add(MakeShared<FJsonValueObject>(CObj));
		}
		OutEntry->SetArrayField(TEXT("Constraints"), ConstrArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetPhysicsAssetCapability)
