// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetPhysicsAssetCapability.h"
#include "Utils/NexusArgs.h"
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

void FNexusManageAssetPhysicsAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetPhysicsAsset");
	Out.SearchAssetTypes = {TEXT("PhysicsAsset")};
	Out.Description = TEXT("Edit PhysicsAsset shapes and constraints. See operations[].action.");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(
			TEXT("Operation type"),
			{
				TEXT("SetPhysicsType"),
				TEXT("AddSphere"),
				TEXT("AddCapsule"),
				TEXT("AddBox"),
				TEXT("ClearShapes"),
				TEXT("AddConstraint"),
				TEXT("RemoveConstraint"),
			}))
		.Prop(TEXT("BoneName"),    FNexusSchema::Str(TEXT("Target bone name (required for most ops)")))
		.Prop(TEXT("PhysicsType"), FNexusSchema::Enum(TEXT("SetPhysicsType"), { TEXT("Simulated"), TEXT("Kinematic"), TEXT("Default") }, TEXT("Simulated")))
		.Prop(TEXT("Radius"),      FNexusSchema::Num(TEXT("Sphere/capsule radius")))
		.Prop(TEXT("HalfHeight"),  FNexusSchema::Num(TEXT("Capsule half-height")))
		.Prop(TEXT("ExtentX"),     FNexusSchema::Num(TEXT("Box X half-extent")))
		.Prop(TEXT("ExtentY"),     FNexusSchema::Num(TEXT("Box Y half-extent")))
		.Prop(TEXT("ExtentZ"),     FNexusSchema::Num(TEXT("Box Z half-extent")))
		.Prop(TEXT("Bone1"),       FNexusSchema::Str(TEXT("AddConstraint: skeleton1")))
		.Prop(TEXT("Bone2"),       FNexusSchema::Str(TEXT("AddConstraint: skeleton2")))
		.Prop(TEXT("JointName"),   FNexusSchema::Str(TEXT("RemoveConstraint: joint name")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("PhysicsAsset asset path")))
		.Required(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("physics"), TEXT("ragdoll"), TEXT("body"), TEXT("Collision"), TEXT("constraint"), TEXT("capsule"), TEXT("sphere") };
	Out.RelatedCapabilities = { TEXT("GetAssetPhysicsAsset"), TEXT("GetAssetSkeletalMesh") };
	Out.WhenToUse = TEXT("Add collision shapes, PhysicsType, constraints on PhysicsAsset bones");
}

struct FPhysicsAssetActionState
{
	UPhysicsAsset* PA = nullptr;
	bool bDirty = false;
};

static FPhysicsAssetActionState* PAState(FNexusActionContext& Ctx)
{
	return static_cast<FPhysicsAssetActionState*>(Ctx.Target);
}

static UPhysicsAsset* PAFrom(FNexusActionContext& Ctx)
{
	FPhysicsAssetActionState* S = PAState(Ctx);
	return S ? S->PA : nullptr;
}

static void MarkPADirty(FNexusActionContext& Ctx)
{
	if (FPhysicsAssetActionState* S = PAState(Ctx))
	{
		S->bDirty = true;
	}
}

static USkeletalBodySetup* GetOrCreateBody(UPhysicsAsset* PA, const FString& BoneName)
{
	USkeletalBodySetup* BS = FindBodyByBone(PA, BoneName);
	if (!BS)
	{
		BS = NewObject<USkeletalBodySetup>(PA, NAME_None, RF_Transactional);
		BS->BoneName = *BoneName;
		PA->SkeletalBodySetups.Add(BS);
	}
	return BS;
}

static void HandlePA_SetPhysicsType(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPhysicsAsset* PA = PAFrom(Ctx);
	if (!PA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PhysicsAsset is null"));
		return;
	}
	const FNexusArgs A(Op);
	const FString BoneName = A.Str(TEXT("BoneName"));
	USkeletalBodySetup* BS = GetOrCreateBody(PA, BoneName);
	const FString TypeStr = A.Str(TEXT("PhysicsType"));
	if (TypeStr == TEXT("Simulated"))
		BS->PhysicsType = EPhysicsType::PhysType_Simulated;
	else if (TypeStr == TEXT("Kinematic"))
		BS->PhysicsType = EPhysicsType::PhysType_Kinematic;
	else
		BS->PhysicsType = EPhysicsType::PhysType_Default;
	MarkPADirty(Ctx);
}

static void HandlePA_AddSphere(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPhysicsAsset* PA = PAFrom(Ctx);
	if (!PA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PhysicsAsset is null"));
		return;
	}
	const FNexusArgs A(Op);
	USkeletalBodySetup* BS = GetOrCreateBody(PA, A.Str(TEXT("BoneName")));
	FKSphereElem Sphere;
	const double Radius = A.Num(TEXT("Radius"), 10.0);
	Sphere.Radius = static_cast<float>(Radius);
	BS->AggGeom.SphereElems.Add(Sphere);
	Ctx.Entry->SetNumberField(TEXT("Radius"), Radius);
	MarkPADirty(Ctx);
}

static void HandlePA_AddCapsule(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPhysicsAsset* PA = PAFrom(Ctx);
	if (!PA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PhysicsAsset is null"));
		return;
	}
	const FNexusArgs A(Op);
	USkeletalBodySetup* BS = GetOrCreateBody(PA, A.Str(TEXT("BoneName")));
	FKSphylElem Capsule;
	const double Radius = A.Num(TEXT("Radius"), 10.0);
	const double HalfHeight = A.Num(TEXT("HalfHeight"), 20.0);
	Capsule.Radius = static_cast<float>(Radius);
	Capsule.Length = static_cast<float>(HalfHeight * 2.0);
	BS->AggGeom.SphylElems.Add(Capsule);
	MarkPADirty(Ctx);
}

static void HandlePA_AddBox(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPhysicsAsset* PA = PAFrom(Ctx);
	if (!PA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PhysicsAsset is null"));
		return;
	}
	const FNexusArgs A(Op);
	USkeletalBodySetup* BS = GetOrCreateBody(PA, A.Str(TEXT("BoneName")));
	FKBoxElem Box;
	const double X = A.Num(TEXT("ExtentX"), 10.0);
	const double Y = A.Num(TEXT("ExtentY"), 10.0);
	const double Z = A.Num(TEXT("ExtentZ"), 10.0);
	Box.X = static_cast<float>(X * 2.0);
	Box.Y = static_cast<float>(Y * 2.0);
	Box.Z = static_cast<float>(Z * 2.0);
	BS->AggGeom.BoxElems.Add(Box);
	MarkPADirty(Ctx);
}

static void HandlePA_ClearShapes(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPhysicsAsset* PA = PAFrom(Ctx);
	if (!PA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PhysicsAsset is null"));
		return;
	}
	const FString BoneName = FNexusArgs(Op).Str(TEXT("BoneName"));
	USkeletalBodySetup* BS = FindBodyByBone(PA, BoneName);
	if (!BS)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Body not found for bone '%s'"), *BoneName));
		return;
	}
	BS->AggGeom.SphereElems.Empty();
	BS->AggGeom.BoxElems.Empty();
	BS->AggGeom.SphylElems.Empty();
	BS->AggGeom.ConvexElems.Empty();
	MarkPADirty(Ctx);
}

static void HandlePA_AddConstraint(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPhysicsAsset* PA = PAFrom(Ctx);
	if (!PA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PhysicsAsset is null"));
		return;
	}
	FString Bone1, Bone2;
	if (!Op->TryGetStringField(TEXT("Bone1"), Bone1) || !Op->TryGetStringField(TEXT("Bone2"), Bone2))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddConstraint requires bone1 and bone2"));
		return;
	}
	UPhysicsConstraintTemplate* CT = NewObject<UPhysicsConstraintTemplate>(PA, NAME_None, RF_Transactional);
	CT->DefaultInstance.ConstraintBone1 = *Bone1;
	CT->DefaultInstance.ConstraintBone2 = *Bone2;
	CT->DefaultInstance.JointName = *FString::Printf(TEXT("%s_%s"), *Bone1, *Bone2);
	PA->ConstraintSetup.Add(CT);
	Ctx.Entry->SetStringField(TEXT("JointName"), CT->DefaultInstance.JointName.ToString());
	MarkPADirty(Ctx);
}

static void HandlePA_RemoveConstraint(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPhysicsAsset* PA = PAFrom(Ctx);
	if (!PA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PhysicsAsset is null"));
		return;
	}
	FString JointName;
	Op->TryGetStringField(TEXT("JointName"), JointName);
	const int32 Before = PA->ConstraintSetup.Num();
	PA->ConstraintSetup.RemoveAll([&](UPhysicsConstraintTemplate* CT)
	{
		return CT && CT->DefaultInstance.JointName.ToString().Equals(JointName, ESearchCase::IgnoreCase);
	});
	const int32 Removed = Before - PA->ConstraintSetup.Num();
	Ctx.Entry->SetNumberField(TEXT("RemovedCount"), Removed);
	if (Removed > 0) MarkPADirty(Ctx);
}

bool FNexusManageAssetPhysicsAssetCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UPhysicsAsset* PA = FNexusAssetUtils::LoadAssetWithFallback<UPhysicsAsset>(AssetPath);
	if (!PA)
	{
		OutError = FString::Printf(TEXT("PhysicsAsset not found: %s"), *AssetPath);
		return false;
	}
	FPhysicsAssetActionState* State = new FPhysicsAssetActionState();
	State->PA = PA;
	OutTarget = State;
	return true;
}

void FNexusManageAssetPhysicsAssetCapability::FinalizeTarget(void* Target) const
{
	FPhysicsAssetActionState* State = static_cast<FPhysicsAssetActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->PA)
	{
		State->PA->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetPhysicsAssetCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetPhysicsType"),  &HandlePA_SetPhysicsType);
	OutHandlers.Add(TEXT("AddSphere"),        &HandlePA_AddSphere);
	OutHandlers.Add(TEXT("AddCapsule"),       &HandlePA_AddCapsule);
	OutHandlers.Add(TEXT("AddBox"),           &HandlePA_AddBox);
	OutHandlers.Add(TEXT("ClearShapes"),      &HandlePA_ClearShapes);
	OutHandlers.Add(TEXT("AddConstraint"),    &HandlePA_AddConstraint);
	OutHandlers.Add(TEXT("RemoveConstraint"), &HandlePA_RemoveConstraint);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetPhysicsAssetCapability)
