// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetStaticMeshCapability.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "Utils/NexusEditorToolsets.h"
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
#include "Editor.h"
#if NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
#include "StaticMeshEditorSubsystem.h"
#else
#include "EditorStaticMeshLibrary.h"
#endif

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
	OutError = TEXT("CollisionTraceFlag must be UseDefault/UseSimpleAndComplex/UseSimpleAsComplex/UseComplexAsSimple");
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
#if NX_UE_HAS_STATIC_MESH_CUSTOM_COLLISION_ACCESSOR
	Mesh->SetCustomizedCollision(true);
#else
	Mesh->bCustomizedCollision = true;
#endif
#endif
}

void FNexusManageAssetStaticMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetStaticMesh");
	Out.SearchAssetTypes = {TEXT("StaticMesh")};
	Out.Description = TEXT("Batch edit StaticMesh. Material/collision/Socket/LOD ScreenSize.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), {
			TEXT("SetMaterialSlot"), TEXT("SetProperty"),
			TEXT("SetCollisionTraceFlag"), TEXT("AddBoxCollision"), TEXT("AddSphereCollision"),
			TEXT("ClearSimpleCollision"),
			TEXT("AddSocket"), TEXT("RemoveSocket"), TEXT("SetSocket"),
			TEXT("SetLODScreenSize"), TEXT("GenerateLODs"), TEXT("RemoveLODs"), TEXT("GenerateConvexCollision"), TEXT("SetNaniteEnabled")
		}))
		.Prop(TEXT("SlotIndex"),      FNexusSchema::Int(TEXT("Material slot index (SetMaterialSlot)")))
		.Prop(TEXT("MaterialPath"),   FNexusSchema::Str(TEXT("Material asset path (SetMaterialSlot)")))
		.Prop(TEXT("PropertyPath"),   FNexusSchema::Str(TEXT("Property path (SetProperty)")))
		.Prop(TEXT("Value"),          FNexusSchema::Str(TEXT("New property value (SetProperty)")))
		.Prop(TEXT("CollisionTraceFlag"), FNexusSchema::Enum(TEXT("Collision complexity"), {
			TEXT("UseDefault"), TEXT("UseSimpleAndComplex"), TEXT("UseSimpleAsComplex"), TEXT("UseComplexAsSimple")
		}))
		.Prop(TEXT("x"), FNexusSchema::Num(TEXT("Position/half-axis X (collision center or Box half-axis)")))
		.Prop(TEXT("y"), FNexusSchema::Num(TEXT("Position/half-axis Y")))
		.Prop(TEXT("z"), FNexusSchema::Num(TEXT("Position/half-axis Z")))
		.Prop(TEXT("ExtentX"), FNexusSchema::Num(TEXT("Box half-extent X (defaults to x)")))
		.Prop(TEXT("ExtentY"), FNexusSchema::Num(TEXT("Box half-extent Y (defaults to y)")))
		.Prop(TEXT("ExtentZ"), FNexusSchema::Num(TEXT("Box half-extent Z (defaults to z)")))
		.Prop(TEXT("Radius"), FNexusSchema::Num(TEXT("Sphere radius (AddSphereCollision)")))
		.Prop(TEXT("SocketName"), FNexusSchema::Str(TEXT("Socket name")))
		.Prop(TEXT("LocX"), FNexusSchema::Num(TEXT("Socket/collision center X")))
		.Prop(TEXT("LocY"), FNexusSchema::Num(TEXT("Socket/collision center Y")))
		.Prop(TEXT("LocZ"), FNexusSchema::Num(TEXT("Socket/collision center Z")))
		.Prop(TEXT("Pitch"), FNexusSchema::Num(TEXT("Socket rotation Pitch")))
		.Prop(TEXT("Yaw"), FNexusSchema::Num(TEXT("Socket rotation Yaw")))
		.Prop(TEXT("Roll"), FNexusSchema::Num(TEXT("Socket rotation Roll")))
		.Prop(TEXT("ScaleX"), FNexusSchema::Num(TEXT("Socket rotation Scale X")))
		.Prop(TEXT("ScaleY"), FNexusSchema::Num(TEXT("Socket rotation Scale Y")))
		.Prop(TEXT("ScaleZ"), FNexusSchema::Num(TEXT("Socket rotation Scale Z")))
		.Prop(TEXT("LODIndex"), FNexusSchema::Int(TEXT("LOD index (SetLODScreenSize)")))
        .Prop(TEXT("TrianglePercents"), FNexusSchema::ArrayOf(TEXT("Generated LOD retention fractions excluding LOD0"), FNexusSchema::Num(TEXT("Triangle retention fraction"))))
        .Prop(TEXT("HullCount"), FNexusSchema::Int(TEXT("Convex decomposition hull count"), 4, 1, 64))
        .Prop(TEXT("MaxHullVertices"), FNexusSchema::Int(TEXT("Maximum vertices per hull"), 16, 4, 64))
        .Prop(TEXT("HullPrecision"), FNexusSchema::Int(TEXT("Convex decomposition precision"), 100000, 10000, 1000000))
        .Prop(TEXT("bEnabled"), FNexusSchema::Bool(TEXT("Explicit Nanite state (UE5+)")))
		.Prop(TEXT("ScreenSize"), FNexusSchema::Num(TEXT("LOD ScreenSize（0–1）")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("StaticMesh asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mesh"), TEXT("Material"), TEXT("Collision"), TEXT("socket"), TEXT("lod"), TEXT("Static") };
	Out.RelatedCapabilities = { TEXT("GetAssetStaticMesh"), TEXT("SearchAsset"), TEXT("SaveAsset") };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.WhenToUse = TEXT("Edit StaticMesh material/collision/Socket/LOD; persist with SaveAsset");
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
	const TArray<FStaticMaterial>& Materials = FNexusAssetUtils::GetStaticMeshMaterials(*Mesh);
	if (!Materials.IsValidIndex(SlotIndex))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Material slot index %d out of range [0, %d)"), SlotIndex, Materials.Num()));
		return;
	}
#if WITH_EDITOR
    // 原生按槽名定位；仅唯一具名槽可与现有索引契约等价。
    const FName SlotName = Materials[SlotIndex].MaterialSlotName;
    int32 SameNameCount = 0; for (const auto& Slot : Materials) if (Slot.MaterialSlotName == SlotName) ++SameNameCount;
    bool bNative = false;
    if (Material && !SlotName.IsNone() && SameNameCount == 1) {
        auto Input = MakeShared<FJsonObject>(); Input->SetObjectField(TEXT("mesh"), FNexusNativeEditorTools::Reference(Mesh->GetPathName())); Input->SetStringField(TEXT("slot_name"), SlotName.ToString()); Input->SetObjectField(TEXT("material"), FNexusNativeEditorTools::Reference(Material->GetPathName()));
        bNative = FNexusNativeEditorTools::EditSync(TEXT("editor_toolset.toolsets.static_mesh.StaticMeshTools"), TEXT("set_material"), Input, Ctx.Entry);
        if (Ctx.Entry->HasField(TEXT("Error"))) return;
    }
    if (!bNative) Mesh->SetMaterial(SlotIndex, Material);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("SlotIndex"), SlotIndex);
	Ctx.Entry->SetStringField(TEXT("MaterialClass"), Material ? Material->GetClass()->GetName() : TEXT("None"));
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
#else
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetMaterialSlot only available in editor builds"));
#endif
}

static void HandleSM_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
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
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSM_SetCollisionTraceFlag(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FNexusArgs A(Op);
	FString FlagText = A.Str(TEXT("CollisionTraceFlag"));
	if (FlagText.IsEmpty()) FlagText = A.Str(TEXT("Value"));
	ECollisionTraceFlag Flag;
	FString FlagErr;
	if (!ParseCollisionTraceFlag(FlagText, Flag, FlagErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FlagErr);
		return;
	}
	UBodySetup* Body = EnsureBodySetup(Mesh);
	if (!Body)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unable to create BodySetup"));
		return;
	}
	Body->CollisionTraceFlag = Flag;
	InvalidateCollision(Mesh, Body);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("CollisionTraceFlag"), FlagText.IsEmpty() ? TEXT("UseDefault") : FlagText);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSM_AddBoxCollision(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	UBodySetup* Body = EnsureBodySetup(Mesh);
	if (!Body)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unable to create BodySetup"));
		return;
	}
	FVector Center(0.f), Extent(50.f, 50.f, 50.f);
	ParseStaticMeshVec3(Op, TEXT("LocX"), TEXT("LocY"), TEXT("LocZ"), Center, FVector::ZeroVector);
	if (!ParseStaticMeshVec3(Op, TEXT("ExtentX"), TEXT("ExtentY"), TEXT("ExtentZ"), Extent, Extent))
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
	Ctx.Entry->SetNumberField(TEXT("BoxElemCount"), Body->AggGeom.BoxElems.Num());
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSM_AddSphereCollision(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	UBodySetup* Body = EnsureBodySetup(Mesh);
	if (!Body)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unable to create BodySetup"));
		return;
	}
	FVector Center(0.f);
	ParseStaticMeshVec3(Op, TEXT("LocX"), TEXT("LocY"), TEXT("LocZ"), Center, FVector::ZeroVector);
	const float Radius = static_cast<float>(FNexusArgs(Op).Num(TEXT("Radius"), 50.0));
	FKSphereElem Sphere;
	Sphere.Center = Center;
	Sphere.Radius = FMath::Max(1.f, Radius);
	Body->AggGeom.SphereElems.Add(Sphere);
	InvalidateCollision(Mesh, Body);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("SphereElemCount"), Body->AggGeom.SphereElems.Num());
	Ctx.Entry->SetNumberField(TEXT("Radius"), Sphere.Radius);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSM_ClearSimpleCollision(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	(void)Op;
	UStaticMesh* Mesh = MeshFrom(Ctx);
	UBodySetup* Body = EnsureBodySetup(Mesh);
	if (!Body)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unable to create BodySetup"));
		return;
	}
	Body->RemoveSimpleCollision();
	InvalidateCollision(Mesh, Body);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void ApplySocketFields(UStaticMeshSocket* Socket, const TSharedPtr<FJsonObject>& Op)
{
	FVector Loc = Socket->RelativeLocation;
	FRotator Rot = Socket->RelativeRotation;
	FVector Scale = Socket->RelativeScale;
	ParseStaticMeshVec3(Op, TEXT("LocX"), TEXT("LocY"), TEXT("LocZ"), Loc, Loc);
	if (Op->HasField(TEXT("Pitch"))) Rot.Pitch = Op->GetNumberField(TEXT("Pitch"));
	if (Op->HasField(TEXT("Yaw"))) Rot.Yaw = Op->GetNumberField(TEXT("Yaw"));
	if (Op->HasField(TEXT("Roll"))) Rot.Roll = Op->GetNumberField(TEXT("Roll"));
	ParseStaticMeshVec3(Op, TEXT("ScaleX"), TEXT("ScaleY"), TEXT("ScaleZ"), Scale, Scale);
	Socket->RelativeLocation = Loc;
	Socket->RelativeRotation = Rot;
	Socket->RelativeScale = Scale;
}

static void HandleSM_AddSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FString SocketName = FNexusArgs(Op).Str(TEXT("SocketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SocketName required"));
		return;
	}
	if (Mesh->FindSocket(FName(*SocketName)))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Socket already exists: %s"), *SocketName));
		return;
	}
	UStaticMeshSocket* Socket = NewObject<UStaticMeshSocket>(Mesh);
	Socket->SocketName = FName(*SocketName);
	Mesh->AddSocket(Socket);
	ApplySocketFields(Socket, Op);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("SocketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSM_SetSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FString SocketName = FNexusArgs(Op).Str(TEXT("SocketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SocketName required"));
		return;
	}
	UStaticMeshSocket* Socket = Mesh->FindSocket(FName(*SocketName));
	if (!Socket)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Socket not found: %s"), *SocketName));
		return;
	}
	ApplySocketFields(Socket, Op);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("SocketName"), SocketName);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSM_RemoveSocket(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FString SocketName = FNexusArgs(Op).Str(TEXT("SocketName"));
	if (SocketName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveSocket requires SocketName"));
		return;
	}
	UStaticMeshSocket* Socket = Mesh->FindSocket(FName(*SocketName));
	if (!Socket)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Socket not found: %s"), *SocketName));
		return;
	}
	Mesh->RemoveSocket(Socket);
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("bRemoved"), SocketName);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleSM_SetLodScreenSize(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UStaticMesh* Mesh = MeshFrom(Ctx);
	const FNexusArgs A(Op);
	const int32 LodIndex = static_cast<int32>(A.Num(TEXT("LODIndex")));
	if (!Op->HasField(TEXT("ScreenSize")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetLODScreenSize requires ScreenSize"));
		return;
	}
	if (!Mesh->IsSourceModelValid(LodIndex))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("LOD %d does not exist (sourceModels=%d)"), LodIndex, Mesh->GetNumSourceModels()));
		return;
	}
	const float ScreenSize = static_cast<float>(A.Num(TEXT("ScreenSize")));
	Mesh->GetSourceModel(LodIndex).ScreenSize.Default = ScreenSize;
#if NX_UE_HAS_STATIC_MESH_AUTO_LOD_SCREENSIZE_ACCESSOR
	Mesh->SetAutoComputeLODScreenSize(false);
#else
	Mesh->bAutoComputeLODScreenSize = false;
#endif
	MarkMeshDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("LODIndex"), LodIndex);
	Ctx.Entry->SetNumberField(TEXT("ScreenSize"), ScreenSize);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
#else
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetLODScreenSize editor only"));
#endif
}

static void HandleSM_Generate(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
    UStaticMesh* Mesh = MeshFrom(Ctx); if (!Mesh) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Static mesh is missing")); return; }
    const FNexusArgs A(Op); auto Input = MakeShared<FJsonObject>();
    Input->SetObjectField(TEXT("mesh"), FNexusNativeEditorTools::Reference(Mesh->GetPathName())); FString Tool;
    const TArray<TSharedPtr<FJsonValue>>* Percents = nullptr;
    if (Ctx.Action == TEXT("GenerateLODs")) {
        if (!Op->TryGetArrayField(TEXT("TrianglePercents"), Percents) || Percents->Num() < 1 || Percents->Num() > 7) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("GenerateLODs requires 1-7 retention fractions")); return; }
        double Previous = 1;
        for (const auto& Value : *Percents) { double Percent = 0; if (!Value->TryGetNumber(Percent) || !FMath::IsFinite(Percent) || Percent <= 0 || Percent > Previous) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("LOD retention must decrease within (0,1]")); return; } Previous = Percent; }
        Tool = TEXT("generate_lods"); Input->SetArrayField(TEXT("triangle_percents"), *Percents);
    } else if (Ctx.Action == TEXT("RemoveLODs")) Tool = TEXT("remove_lods");
    else if (Ctx.Action == TEXT("GenerateConvexCollision")) {
        for (const FString& Key : {TEXT("HullCount"),TEXT("MaxHullVertices"),TEXT("HullPrecision")}) if (Op->HasField(Key)) {
            const double Value=A.Num(*Key,-1); if (!FMath::IsFinite(Value) || Value<0 || Value>1000000 || Value!=static_cast<int32>(Value)) { Ctx.Entry->SetStringField(TEXT("Error"),TEXT("Convex limits must be bounded integers")); return; }
        }
        const int32 Count = static_cast<int32>(A.Num(TEXT("HullCount"),4)), Vertices = static_cast<int32>(A.Num(TEXT("MaxHullVertices"),16)), Precision = static_cast<int32>(A.Num(TEXT("HullPrecision"),100000));
        if (Count < 1 || Count > 64 || Vertices < 4 || Vertices > 64 || Precision < 10000 || Precision > 1000000) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Convex decomposition limits are invalid")); return; }
        Tool = TEXT("generate_convex_collisions"); Input->SetNumberField(TEXT("hull_count"),Count); Input->SetNumberField(TEXT("max_hull_verts"),Vertices); Input->SetNumberField(TEXT("hull_precision"),Precision);
    } else {
#if !NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
        Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Nanite requires UE5")); return;
#endif
        if (!Op->HasTypedField<EJson::Boolean>(TEXT("bEnabled"))) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetNaniteEnabled requires bEnabled")); return; }
        Tool = TEXT("set_nanite_enabled"); Input->SetBoolField(TEXT("enabled"),A.Bool(TEXT("bEnabled")));
    }
    if (!FNexusNativeEditorTools::EditSync(TEXT("editor_toolset.toolsets.static_mesh.StaticMeshTools"), Tool, Input, Ctx.Entry)) {
        bool bOk = false;
#if NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
        UStaticMeshEditorSubsystem* Editor = GEditor ? GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>() : nullptr;
        if (!Editor) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Static mesh editor subsystem is unavailable")); return; }
#endif
        if (Ctx.Action == TEXT("GenerateLODs")) {
#if NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
            FStaticMeshReductionOptions Options;
#else
            FEditorScriptingMeshReductionOptions Options;
#endif
            Options.bAutoComputeLODScreenSize = true;
            for (int32 Index = -1; Index < Percents->Num(); ++Index) {
#if NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
                FStaticMeshReductionSettings Setting;
#else
                FEditorScriptingMeshReductionSettings Setting;
#endif
                Setting.PercentTriangles = Index < 0 ? 1 : static_cast<float>((*Percents)[Index]->AsNumber()); Options.ReductionSettings.Add(Setting);
            }
            Mesh->Modify();
#if NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
            bOk = Editor->SetLods(Mesh, Options) > 0;
#else
            bOk = UEditorStaticMeshLibrary::SetLods(Mesh, Options) > 0;
#endif
        } else if (Ctx.Action == TEXT("RemoveLODs")) {
            Mesh->Modify();
#if NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
            bOk = Editor->RemoveLods(Mesh);
#else
            bOk = UEditorStaticMeshLibrary::RemoveLods(Mesh);
#endif
        } else if (Ctx.Action == TEXT("GenerateConvexCollision")) {
            Mesh->Modify();
#if NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
            bOk = Editor->SetConvexDecompositionCollisions(Mesh, static_cast<int32>(A.Num(TEXT("HullCount"),4)), static_cast<int32>(A.Num(TEXT("MaxHullVertices"),16)), static_cast<int32>(A.Num(TEXT("HullPrecision"),100000)));
#else
            bOk = UEditorStaticMeshLibrary::SetConvexDecompositionCollisions(Mesh, static_cast<int32>(A.Num(TEXT("HullCount"),4)), static_cast<int32>(A.Num(TEXT("MaxHullVertices"),16)), static_cast<int32>(A.Num(TEXT("HullPrecision"),100000)));
#endif
        } else {
#if NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
            auto Settings = Editor->GetNaniteSettings(Mesh); Settings.bEnabled = A.Bool(TEXT("bEnabled")); Mesh->Modify(); Editor->SetNaniteSettings(Mesh, Settings); bOk = Editor->GetNaniteSettings(Mesh).bEnabled == Settings.bEnabled;
#endif
        }
        if (!bOk) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Mesh generation failed; inspect editor diagnostics")); return; }
        Ctx.Entry->SetStringField(TEXT("Backend"), TEXT("EngineCompatibility"));
    }
    if (Ctx.Entry->HasField(TEXT("Error"))) return;
    MarkMeshDirty(Ctx); Ctx.Entry->SetNumberField(TEXT("LODCount"), Mesh->GetNumLODs());
    if (const auto* Body = FNexusAssetUtils::GetStaticMeshBodySetup(Mesh)) Ctx.Entry->SetNumberField(TEXT("ConvexCount"), Body->AggGeom.ConvexElems.Num());
}

bool FNexusManageAssetStaticMeshCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(AssetPath);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("StaticMesh not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Mesh;
	return true;
}

void FNexusManageAssetStaticMeshCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetMaterialSlot"),        &HandleSM_SetMaterialSlot);
	OutHandlers.Add(TEXT("SetProperty"),             &HandleSM_SetProperty);
	OutHandlers.Add(TEXT("SetCollisionTraceFlag"), &HandleSM_SetCollisionTraceFlag);
	OutHandlers.Add(TEXT("AddBoxCollision"),        &HandleSM_AddBoxCollision);
	OutHandlers.Add(TEXT("AddSphereCollision"),     &HandleSM_AddSphereCollision);
	OutHandlers.Add(TEXT("ClearSimpleCollision"),   &HandleSM_ClearSimpleCollision);
	OutHandlers.Add(TEXT("AddSocket"),               &HandleSM_AddSocket);
	OutHandlers.Add(TEXT("SetSocket"),               &HandleSM_SetSocket);
	OutHandlers.Add(TEXT("RemoveSocket"),            &HandleSM_RemoveSocket);
	OutHandlers.Add(TEXT("SetLODScreenSize"),      &HandleSM_SetLodScreenSize);
    for (const FString& Action : {TEXT("GenerateLODs"), TEXT("RemoveLODs"), TEXT("GenerateConvexCollision"), TEXT("SetNaniteEnabled")}) OutHandlers.Add(Action, &HandleSM_Generate);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetStaticMeshCapability)
