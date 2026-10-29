// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusManageAssetMaterialCapability.h"
#include "Capabilities/Asset/Material/NexusManageAssetMaterialGraph.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "Utils/NexusEditorToolsets.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusMaterialUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "MaterialEditingLibrary.h"
#include "NexusMcpTool.h"


static void DoSetParam(UMaterialInstanceConstant* MI, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MI) { Out->SetStringField(TEXT("Error"), TEXT("SetParam requires a MaterialInstance asset")); return; }
	const FString PN = Args->HasField(TEXT("ParamName")) ? Args->GetStringField(TEXT("ParamName")) : TEXT("");
	const FString PT = Args->HasField(TEXT("ParamType")) ? Args->GetStringField(TEXT("ParamType")).ToLower() : TEXT("");
	const FString V  = Args->HasField(TEXT("Value"))     ? Args->GetStringField(TEXT("Value"))     : TEXT("");
	if (PN.IsEmpty() || PT.IsEmpty()) { Out->SetStringField(TEXT("Error"), TEXT("SetParam requires ParamName, ParamType and value")); return; }

	if (PT == TEXT("Scalar"))
	{
		float F = FCString::Atof(*V);
		MI->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(*PN), F);
		Out->SetStringField(TEXT("ParamName"), PN);
		Out->SetStringField(TEXT("ParamType"), TEXT("Scalar"));
		Out->SetNumberField(TEXT("Value"), F);
	}
	else if (PT == TEXT("Vector"))
	{
		FLinearColor C(FLinearColor::Black);
		TArray<FString> P; V.ParseIntoArray(P, TEXT(","));
		if (P.Num() < 3) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("vector value needs 'R,G,B' or 'R,G,B,A', got: %s"), *V)); return; }
		C.R = FCString::Atof(*P[0]); C.G = FCString::Atof(*P[1]);
		C.B = FCString::Atof(*P[2]); C.A = P.Num() >= 4 ? FCString::Atof(*P[3]) : 1.0f;
		MI->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(*PN), C);
		Out->SetStringField(TEXT("ParamName"), PN);
		Out->SetStringField(TEXT("ParamType"), TEXT("Vector"));
		Out->SetStringField(TEXT("Value"), FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f"), C.R, C.G, C.B, C.A));
	}
	else if (PT == TEXT("Texture"))
	{
		UTexture* Tex = LoadObject<UTexture>(nullptr, *V);
		if (!Tex) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Texture not found: %s"), *V)); return; }
		MI->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(*PN), Tex);
		Out->SetStringField(TEXT("ParamName"), PN);
		Out->SetStringField(TEXT("ParamType"), TEXT("Texture"));
		Out->SetStringField(TEXT("Value"), Tex->GetOutermost()->GetName());
	}
	else { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unknown ParamType: %s"), *PT)); return; }
	MI->PostEditChange();
	MI->MarkPackageDirty();
}

static UObject* MaterialTargetFromContext(FNexusActionContext& Ctx)
{
	return static_cast<UObject*>(Ctx.Target);
}

static void HandleMaterialRecompile(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMaterial* Mat = Cast<UMaterial>(MaterialTargetFromContext(Ctx));
	if (!Mat)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("recompile only for UMaterial; MaterialFunction need not recompile"));
		return;
	}
    auto Input = MakeShared<FJsonObject>(); Input->SetObjectField(TEXT("material_or_function"), FNexusNativeEditorTools::Reference(Mat->GetPathName()));
    if (FNexusNativeEditorTools::EditSync(TEXT("editor_toolset.toolsets.material.MaterialTools"), TEXT("recompile"), Input, Ctx.Entry)) return;
	UMaterialEditingLibrary::RecompileMaterial(Mat);
}

static void HandleMaterialSetParam(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	DoSetParam(Cast<UMaterialInstanceConstant>(MaterialTargetFromContext(Ctx)), Op, Ctx.Entry);
}

static void HandleMaterialAddNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { FNexusMaterialGraphOps::DoAddNode(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { FNexusMaterialGraphOps::DoAddNodeInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddNode requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialRemoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { FNexusMaterialGraphOps::DoRemoveNode(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { FNexusMaterialGraphOps::DoRemoveNodeInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveNode requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialSetNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { FNexusMaterialGraphOps::DoSetNode(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { FNexusMaterialGraphOps::DoSetNodeInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetNode requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialConnect(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { FNexusMaterialGraphOps::DoConnect(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { FNexusMaterialGraphOps::DoConnectInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("connect requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialDisconnect(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { FNexusMaterialGraphOps::DoDisconnect(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { FNexusMaterialGraphOps::DoDisconnectInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("disconnect requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialDisconnectAll(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { FNexusMaterialGraphOps::DoDisconnectAll(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { FNexusMaterialGraphOps::DoDisconnectAllInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("DisconnectAll requires a UMaterial or UMaterialFunction")); }
}

void FNexusManageAssetMaterialCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("Recompile"),       &HandleMaterialRecompile);
	OutHandlers.Add(TEXT("SetParam"),       &HandleMaterialSetParam);
	OutHandlers.Add(TEXT("AddNode"),        &HandleMaterialAddNode);
	OutHandlers.Add(TEXT("RemoveNode"),     &HandleMaterialRemoveNode);
	OutHandlers.Add(TEXT("SetNode"),        &HandleMaterialSetNode);
	OutHandlers.Add(TEXT("Connect"),         &HandleMaterialConnect);
	OutHandlers.Add(TEXT("Disconnect"),      &HandleMaterialDisconnect);
	OutHandlers.Add(TEXT("DisconnectAll"),  &HandleMaterialDisconnectAll);
}

bool FNexusManageAssetMaterialCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);

	UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
	if (!Obj)
	{
		OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Obj;
	return true;
}

void FNexusManageAssetMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetMaterial");
	Out.SearchAssetTypes = {TEXT("Material"), TEXT("MaterialInstance"), TEXT("MaterialFunction")};
	Out.Description = TEXT("Batch edit Mat/MI/MF. MF disallows SetParam/recompile; do not use TargetNodeId=material.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),           FNexusSchema::Enum(TEXT("Edit operation"), { TEXT("SetParam"), TEXT("AddNode"), TEXT("RemoveNode"), TEXT("SetNode"), TEXT("Recompile"), TEXT("Connect"), TEXT("Disconnect"), TEXT("DisconnectAll") }))
		.Prop(TEXT("ParamName"),        FNexusSchema::Str(TEXT("Parameter name (SetParam)")))
		.Prop(TEXT("ParamType"),        FNexusSchema::Enum(TEXT("Parameter type"), { TEXT("Scalar"), TEXT("Vector"), TEXT("Texture") }))
		.Prop(TEXT("Value"),            FNexusSchema::Str(TEXT("float / R,G,B,A / texture path")))
		.Prop(TEXT("ExpressionClass"),  FNexusSchema::Str(TEXT("Expression class short name (AddNode)")))
		.Prop(TEXT("ParameterName"),    FNexusSchema::Str(TEXT("Parameter/TextureSampleParam name")))
		.Prop(TEXT("DefaultValue"),     FNexusSchema::Str(TEXT("float / R,G,B,A / texture path")))
		.Prop(TEXT("NodeId"),           FNexusSchema::Str(TEXT("Expression node id (remove/set)")))
		.Prop(TEXT("PosX"),             FNexusSchema::Num(TEXT("Node X position")))
		.Prop(TEXT("PosY"),             FNexusSchema::Num(TEXT("Node Y position")))
		.Prop(TEXT("SourceNodeId"),     FNexusSchema::Str(TEXT("Source node id (connect/DisconnectAll)")))
		.Prop(TEXT("SourceOutputName"), FNexusSchema::Str(TEXT("Source output pin name (default first)")))
		.Prop(TEXT("TargetNodeId"),     FNexusSchema::Str(TEXT("Target node id or Material (connect/disconnect)")))
		.Prop(TEXT("TargetInputName"),  FNexusSchema::Str(TEXT("Target input pin or material property name")))
		.Required({ TEXT("Action") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Material/MI/MaterialFunction asset path (shared)")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch material ops"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = {
		TEXT("Node"), TEXT("parameter"), TEXT("wire"), TEXT("pin"), TEXT("Recompile")
	};
	Out.RelatedCapabilities = { TEXT("GetAssetMaterial"), TEXT("CreateAssetMaterial"), TEXT("CreateAssetMaterialFunction"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Write Mat/MI/MF graph and MI params; MF wires use expression NodeId");
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetMaterialCapability)

#endif // WITH_EDITOR
