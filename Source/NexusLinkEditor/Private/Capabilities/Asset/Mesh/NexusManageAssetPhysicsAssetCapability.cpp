// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetPhysicsAssetCapability.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsAssetUtils.h"
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
				TEXT("RemoveConstraint"), TEXT("AddBody"), TEXT("RemoveBody"), TEXT("UpdateShape"), TEXT("RemoveShape"), TEXT("SetMassScale"), TEXT("SetConstraintLimits"),
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
		.Prop(TEXT("ShapeType"), FNexusSchema::Enum(TEXT("UpdateShape/RemoveShape target"), {TEXT("Sphere"), TEXT("Capsule"), TEXT("Box")}))
        .Prop(TEXT("ShapeIndex"), FNexusSchema::Int(TEXT("Index within the selected shape type from GetAssetPhysicsAsset"), 0, 0))
        .Prop(TEXT("CenterX"), FNexusSchema::Num(TEXT("Shape local center X")))
        .Prop(TEXT("CenterY"), FNexusSchema::Num(TEXT("Shape local center Y")))
        .Prop(TEXT("CenterZ"), FNexusSchema::Num(TEXT("Shape local center Z")))
        .Prop(TEXT("RotationPitch"), FNexusSchema::Num(TEXT("Box/capsule local rotation pitch")))
        .Prop(TEXT("RotationYaw"), FNexusSchema::Num(TEXT("Box/capsule local rotation yaw")))
        .Prop(TEXT("RotationRoll"), FNexusSchema::Num(TEXT("Box/capsule local rotation roll")))
        .Prop(TEXT("MassScale"), FNexusSchema::Num(TEXT("Positive body mass multiplier")))
        .Prop(TEXT("Swing1Motion"), FNexusSchema::Enum(TEXT("Angular swing 1"), {TEXT("Free"), TEXT("Limited"), TEXT("Locked")}))
        .Prop(TEXT("Swing2Motion"), FNexusSchema::Enum(TEXT("Angular swing 2"), {TEXT("Free"), TEXT("Limited"), TEXT("Locked")}))
        .Prop(TEXT("TwistMotion"), FNexusSchema::Enum(TEXT("Angular twist"), {TEXT("Free"), TEXT("Limited"), TEXT("Locked")}))
        .Prop(TEXT("Swing1Limit"), FNexusSchema::Num(TEXT("Swing 1 limit in degrees, 0-180")))
        .Prop(TEXT("Swing2Limit"), FNexusSchema::Num(TEXT("Swing 2 limit in degrees, 0-180")))
        .Prop(TEXT("TwistLimit"), FNexusSchema::Num(TEXT("Twist limit in degrees, 0-180")))
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

// 精细操作只接受已读回的 Body/形状索引；验证完全部输入再开始修改。
static void HandlePA_EditBody(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
    UPhysicsAsset* Asset = PAFrom(Ctx);
    if (!Asset) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PhysicsAsset is null")); return; }
    const FNexusArgs Args(Op); const FString BoneName = Args.Str(TEXT("BoneName"));
    if (BoneName.IsEmpty()) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("BoneName is required")); return; }
    USkeletalBodySetup* Body = FindBodyByBone(Asset, BoneName);
    if (Ctx.Action == TEXT("AddBody")) {
        if (Body) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Body already exists")); return; }
        if (USkeletalMesh* Mesh = Asset->GetPreviewMesh()) if (Mesh->GetRefSkeleton().FindBoneIndex(FName(*BoneName)) == INDEX_NONE) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("BoneName is absent from the preview mesh skeleton")); return; }
        Asset->Modify(); Body = GetOrCreateBody(Asset, BoneName);
    } else {
        if (!Body) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Body not found")); return; }
        if (Ctx.Action == TEXT("RemoveBody")) {
            Asset->Modify(); Body->Modify();
            FPhysicsAssetUtils::DestroyBody(Asset, Asset->SkeletalBodySetups.IndexOfByKey(Body));
        } else if (Ctx.Action == TEXT("SetMassScale")) {
            const double Scale = Args.Num(TEXT("MassScale"), 0);
            if (!FMath::IsFinite(Scale) || Scale <= 0 || Scale > MAX_flt) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("MassScale must be finite and positive")); return; }
            Body->Modify(); Body->DefaultInstance.MassScale = static_cast<float>(Scale);
        } else {
            const FString Type = Args.Str(TEXT("ShapeType")); const int32 ShapeIndex = static_cast<int32>(Args.Num(TEXT("ShapeIndex")));
            const int32 Count = Type == TEXT("Sphere") ? Body->AggGeom.SphereElems.Num() : Type == TEXT("Box") ? Body->AggGeom.BoxElems.Num() : Type == TEXT("Capsule") ? Body->AggGeom.SphylElems.Num() : 0;
            if (ShapeIndex < 0 || ShapeIndex >= Count) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ShapeType/ShapeIndex does not identify an existing shape")); return; }
            for (const FString& Key : {TEXT("Radius"), TEXT("HalfHeight"), TEXT("ExtentX"), TEXT("ExtentY"), TEXT("ExtentZ")})
                if (Op->HasField(Key) && (!FMath::IsFinite(Args.Num(*Key)) || Args.Num(*Key) <= 0 || Args.Num(*Key) > MAX_flt / 2)) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Shape dimensions must be finite and positive")); return; }
            for (const FString& Key : {TEXT("CenterX"), TEXT("CenterY"), TEXT("CenterZ"), TEXT("RotationPitch"), TEXT("RotationYaw"), TEXT("RotationRoll")})
                if (Op->HasField(Key) && !FMath::IsFinite(Args.Num(*Key))) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Shape transform must be finite")); return; }
            Body->Modify();
            if (Ctx.Action == TEXT("RemoveShape")) {
                if (Type == TEXT("Sphere")) Body->AggGeom.SphereElems.RemoveAt(ShapeIndex);
                else if (Type == TEXT("Box")) Body->AggGeom.BoxElems.RemoveAt(ShapeIndex);
                else Body->AggGeom.SphylElems.RemoveAt(ShapeIndex);
            } else {
                FVector* Center = nullptr; FRotator* Rotation = nullptr;
                if (Type == TEXT("Sphere")) { auto& Shape = Body->AggGeom.SphereElems[ShapeIndex]; Center = &Shape.Center; if (Op->HasField(TEXT("Radius"))) Shape.Radius = Args.Num(TEXT("Radius")); }
                else if (Type == TEXT("Box")) { auto& Shape = Body->AggGeom.BoxElems[ShapeIndex]; Center = &Shape.Center; Rotation = &Shape.Rotation;
                    if (Op->HasField(TEXT("ExtentX"))) Shape.X = Args.Num(TEXT("ExtentX")) * 2; if (Op->HasField(TEXT("ExtentY"))) Shape.Y = Args.Num(TEXT("ExtentY")) * 2; if (Op->HasField(TEXT("ExtentZ"))) Shape.Z = Args.Num(TEXT("ExtentZ")) * 2;
                } else { auto& Shape = Body->AggGeom.SphylElems[ShapeIndex]; Center = &Shape.Center; Rotation = &Shape.Rotation;
                    if (Op->HasField(TEXT("Radius"))) Shape.Radius = Args.Num(TEXT("Radius")); if (Op->HasField(TEXT("HalfHeight"))) Shape.Length = Args.Num(TEXT("HalfHeight")) * 2;
                }
                if (Center) { if (Op->HasField(TEXT("CenterX"))) Center->X = Args.Num(TEXT("CenterX")); if (Op->HasField(TEXT("CenterY"))) Center->Y = Args.Num(TEXT("CenterY")); if (Op->HasField(TEXT("CenterZ"))) Center->Z = Args.Num(TEXT("CenterZ")); }
                if (Rotation) { if (Op->HasField(TEXT("RotationPitch"))) Rotation->Pitch = Args.Num(TEXT("RotationPitch")); if (Op->HasField(TEXT("RotationYaw"))) Rotation->Yaw = Args.Num(TEXT("RotationYaw")); if (Op->HasField(TEXT("RotationRoll"))) Rotation->Roll = Args.Num(TEXT("RotationRoll")); }
            }
            Body->InvalidatePhysicsData();
        }
    }
    Ctx.Entry->SetStringField(TEXT("BoneName"), BoneName); MarkPADirty(Ctx);
}
static void HandlePA_SetConstraintLimits(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
    UPhysicsAsset* Asset = PAFrom(Ctx);
    if (!Asset) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PhysicsAsset is null")); return; }
    const FNexusArgs Args(Op); UPhysicsConstraintTemplate* Target = nullptr;
    for (UPhysicsConstraintTemplate* Constraint : Asset->ConstraintSetup) if (Constraint && Constraint->DefaultInstance.JointName.ToString() == Args.Str(TEXT("JointName"))) Target = Constraint;
    if (!Target) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("JointName not found")); return; }
    for (const FString& Key : {TEXT("Swing1Limit"), TEXT("Swing2Limit"), TEXT("TwistLimit")}) if (Op->HasField(Key) && (!FMath::IsFinite(Args.Num(*Key)) || Args.Num(*Key) < 0 || Args.Num(*Key) > 180)) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Angular limits must be finite, 0-180 degrees")); return; }
    auto Motion = [](const FString& Value) { return Value == TEXT("Locked") ? ACM_Locked : Value == TEXT("Limited") ? ACM_Limited : ACM_Free; };
    Target->Modify(); auto& Instance = Target->DefaultInstance;
    if (Op->HasField(TEXT("Swing1Motion")) || Op->HasField(TEXT("Swing1Limit"))) Instance.SetAngularSwing1Limit(Op->HasField(TEXT("Swing1Motion")) ? Motion(Args.Str(TEXT("Swing1Motion"))) : Instance.GetAngularSwing1Motion(), Args.Num(TEXT("Swing1Limit"), Instance.GetAngularSwing1Limit()));
    if (Op->HasField(TEXT("Swing2Motion")) || Op->HasField(TEXT("Swing2Limit"))) Instance.SetAngularSwing2Limit(Op->HasField(TEXT("Swing2Motion")) ? Motion(Args.Str(TEXT("Swing2Motion"))) : Instance.GetAngularSwing2Motion(), Args.Num(TEXT("Swing2Limit"), Instance.GetAngularSwing2Limit()));
    if (Op->HasField(TEXT("TwistMotion")) || Op->HasField(TEXT("TwistLimit"))) Instance.SetAngularTwistLimit(Op->HasField(TEXT("TwistMotion")) ? Motion(Args.Str(TEXT("TwistMotion"))) : Instance.GetAngularTwistMotion(), Args.Num(TEXT("TwistLimit"), Instance.GetAngularTwistLimit()));
    MarkPADirty(Ctx);
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
		State->PA->UpdateBodySetupIndexMap();
        State->PA->UpdateBoundsBodiesArray();
        State->PA->RefreshPhysicsAssetChange();
        State->PA->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetPhysicsAssetCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("AddBody"), &HandlePA_EditBody);
    OutHandlers.Add(TEXT("RemoveBody"), &HandlePA_EditBody);
    OutHandlers.Add(TEXT("UpdateShape"), &HandlePA_EditBody);
    OutHandlers.Add(TEXT("RemoveShape"), &HandlePA_EditBody);
    OutHandlers.Add(TEXT("SetMassScale"), &HandlePA_EditBody);
    OutHandlers.Add(TEXT("SetConstraintLimits"), &HandlePA_SetConstraintLimits);
    OutHandlers.Add(TEXT("SetPhysicsType"),  &HandlePA_SetPhysicsType);
	OutHandlers.Add(TEXT("AddSphere"),        &HandlePA_AddSphere);
	OutHandlers.Add(TEXT("AddCapsule"),       &HandlePA_AddCapsule);
	OutHandlers.Add(TEXT("AddBox"),           &HandlePA_AddBox);
	OutHandlers.Add(TEXT("ClearShapes"),      &HandlePA_ClearShapes);
	OutHandlers.Add(TEXT("AddConstraint"),    &HandlePA_AddConstraint);
	OutHandlers.Add(TEXT("RemoveConstraint"), &HandlePA_RemoveConstraint);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetPhysicsAssetCapability)
