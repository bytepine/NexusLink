// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusManageAssetMaterialCapability.h"
#include "Capabilities/Asset/Material/NexusManageAssetMaterialGraph.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
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
	if (!MI) { Out->SetStringField(TEXT("error"), TEXT("set_param requires a MaterialInstance asset")); return; }
	const FString PN = Args->HasField(TEXT("paramName")) ? Args->GetStringField(TEXT("paramName")) : TEXT("");
	const FString PT = Args->HasField(TEXT("paramType")) ? Args->GetStringField(TEXT("paramType")).ToLower() : TEXT("");
	const FString V  = Args->HasField(TEXT("value"))     ? Args->GetStringField(TEXT("value"))     : TEXT("");
	if (PN.IsEmpty() || PT.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("set_param requires paramName, paramType and value")); return; }

	if (PT == TEXT("scalar"))
	{
		float F = FCString::Atof(*V);
		MI->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(*PN), F);
		Out->SetStringField(TEXT("paramName"), PN);
		Out->SetStringField(TEXT("paramType"), TEXT("scalar"));
		Out->SetNumberField(TEXT("value"), F);
	}
	else if (PT == TEXT("vector"))
	{
		FLinearColor C(FLinearColor::Black);
		TArray<FString> P; V.ParseIntoArray(P, TEXT(","));
		if (P.Num() < 3) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("vector value needs 'R,G,B' or 'R,G,B,A', got: %s"), *V)); return; }
		C.R = FCString::Atof(*P[0]); C.G = FCString::Atof(*P[1]);
		C.B = FCString::Atof(*P[2]); C.A = P.Num() >= 4 ? FCString::Atof(*P[3]) : 1.0f;
		MI->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(*PN), C);
		Out->SetStringField(TEXT("paramName"), PN);
		Out->SetStringField(TEXT("paramType"), TEXT("vector"));
		Out->SetStringField(TEXT("value"), FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f"), C.R, C.G, C.B, C.A));
	}
	else if (PT == TEXT("texture"))
	{
		UTexture* Tex = LoadObject<UTexture>(nullptr, *V);
		if (!Tex) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Texture not found: %s"), *V)); return; }
		MI->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(*PN), Tex);
		Out->SetStringField(TEXT("paramName"), PN);
		Out->SetStringField(TEXT("paramType"), TEXT("texture"));
		Out->SetStringField(TEXT("value"), Tex->GetOutermost()->GetName());
	}
	else { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown paramType: %s"), *PT)); return; }
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
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("recompile only for UMaterial; MaterialFunction need not recompile"));
		return;
	}
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
	else { Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_node requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialRemoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { FNexusMaterialGraphOps::DoRemoveNode(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { FNexusMaterialGraphOps::DoRemoveNodeInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_node requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialSetNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { FNexusMaterialGraphOps::DoSetNode(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { FNexusMaterialGraphOps::DoSetNodeInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_node requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialConnect(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { FNexusMaterialGraphOps::DoConnect(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { FNexusMaterialGraphOps::DoConnectInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("error"), TEXT("connect requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialDisconnect(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { FNexusMaterialGraphOps::DoDisconnect(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { FNexusMaterialGraphOps::DoDisconnectInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("error"), TEXT("disconnect requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialDisconnectAll(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { FNexusMaterialGraphOps::DoDisconnectAll(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { FNexusMaterialGraphOps::DoDisconnectAllInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("error"), TEXT("disconnect_all requires a UMaterial or UMaterialFunction")); }
}

void FManageAssetMaterialCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("recompile"),       &HandleMaterialRecompile);
	OutHandlers.Add(TEXT("set_param"),       &HandleMaterialSetParam);
	OutHandlers.Add(TEXT("add_node"),        &HandleMaterialAddNode);
	OutHandlers.Add(TEXT("remove_node"),     &HandleMaterialRemoveNode);
	OutHandlers.Add(TEXT("set_node"),        &HandleMaterialSetNode);
	OutHandlers.Add(TEXT("connect"),         &HandleMaterialConnect);
	OutHandlers.Add(TEXT("disconnect"),      &HandleMaterialDisconnect);
	OutHandlers.Add(TEXT("disconnect_all"),  &HandleMaterialDisconnectAll);
}

bool FManageAssetMaterialCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);

	UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
	if (!Obj)
	{
		OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Obj;
	return true;
}

void FManageAssetMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_material");
	Out.SearchAssetTypes = {TEXT("Material"), TEXT("MaterialInstance"), TEXT("MaterialFunction")};
	Out.Description = TEXT("Batch edit Mat/MI/MF. MF disallows set_param/recompile; do not use targetNodeId=material.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),           FNexusSchema::Enum(TEXT("Edit operation"), { TEXT("set_param"), TEXT("add_node"), TEXT("remove_node"), TEXT("set_node"), TEXT("recompile"), TEXT("connect"), TEXT("disconnect"), TEXT("disconnect_all") }))
		.Prop(TEXT("paramName"),        FNexusSchema::Str(TEXT("Parameter name (set_param)")))
		.Prop(TEXT("paramType"),        FNexusSchema::Enum(TEXT("Parameter type"), { TEXT("scalar"), TEXT("vector"), TEXT("texture") }))
		.Prop(TEXT("value"),            FNexusSchema::Str(TEXT("float / R,G,B,A / texture path")))
		.Prop(TEXT("expressionClass"),  FNexusSchema::Str(TEXT("Expression class short name (add_node)")))
		.Prop(TEXT("parameterName"),    FNexusSchema::Str(TEXT("Parameter/TextureSampleParam name")))
		.Prop(TEXT("defaultValue"),     FNexusSchema::Str(TEXT("float / R,G,B,A / texture path")))
		.Prop(TEXT("nodeId"),           FNexusSchema::Str(TEXT("Expression node id (remove/set)")))
		.Prop(TEXT("posX"),             FNexusSchema::Num(TEXT("Node X position")))
		.Prop(TEXT("posY"),             FNexusSchema::Num(TEXT("Node Y position")))
		.Prop(TEXT("sourceNodeId"),     FNexusSchema::Str(TEXT("Source node id (connect/disconnect_all)")))
		.Prop(TEXT("sourceOutputName"), FNexusSchema::Str(TEXT("Source output pin name (default first)")))
		.Prop(TEXT("targetNodeId"),     FNexusSchema::Str(TEXT("Target node id or Material (connect/disconnect)")))
		.Prop(TEXT("targetInputName"),  FNexusSchema::Str(TEXT("Target input pin or material property name")))
		.Required({ TEXT("action") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Material/MI/MaterialFunction asset path (shared)")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch material ops"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = {
		TEXT("node"), TEXT("parameter"), TEXT("wire"), TEXT("pin"), TEXT("recompile")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_material"), TEXT("create_asset_material"), TEXT("create_asset_material_function"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("Write Mat/MI/MF graph and MI params; MF wires use expression nodeId");
}

REGISTER_MCP_CAPABILITY(FManageAssetMaterialCapability)

#endif // WITH_EDITOR
