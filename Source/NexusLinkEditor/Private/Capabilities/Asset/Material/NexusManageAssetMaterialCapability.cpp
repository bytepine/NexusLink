// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusManageAssetMaterialCapability.h"

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

static bool TrySetExprParamName(UMaterialExpression* Expr, const FName& Name)
{
	if (UMaterialExpressionParameter* P = Cast<UMaterialExpressionParameter>(Expr)) { P->ParameterName = Name; return true; }
	if (UMaterialExpressionTextureSampleParameter* TP = Cast<UMaterialExpressionTextureSampleParameter>(Expr)) { TP->ParameterName = Name; return true; }
	return false;
}

static bool TryApplyNodeDefault(UMaterialExpression* Expr, const FString& Val,
	FString& OutType, FString& OutNorm, FString& OutError)
{
	if (UMaterialExpressionScalarParameter* Sc = Cast<UMaterialExpressionScalarParameter>(Expr))
	{
		Sc->DefaultValue = FCString::Atof(*Val);
		OutType = TEXT("scalar"); OutNorm = FString::Printf(TEXT("%.6f"), Sc->DefaultValue); return true;
	}
	if (UMaterialExpressionVectorParameter* Vec = Cast<UMaterialExpressionVectorParameter>(Expr))
	{
		TArray<FString> Parts; Val.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() < 3) { OutError = FString::Printf(TEXT("Vector defaultValue requires 'R,G,B' or 'R,G,B,A', got: %s"), *Val); return false; }
		Vec->DefaultValue.R = FCString::Atof(*Parts[0]); Vec->DefaultValue.G = FCString::Atof(*Parts[1]);
		Vec->DefaultValue.B = FCString::Atof(*Parts[2]); Vec->DefaultValue.A = Parts.Num() >= 4 ? FCString::Atof(*Parts[3]) : 1.0f;
		OutType = TEXT("vector"); OutNorm = FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f"), Vec->DefaultValue.R, Vec->DefaultValue.G, Vec->DefaultValue.B, Vec->DefaultValue.A); return true;
	}
	if (UMaterialExpressionTextureBase* TexExpr = Cast<UMaterialExpressionTextureBase>(Expr))
	{
		UTexture* Tex = LoadObject<UTexture>(nullptr, *Val);
		if (!Tex) { OutError = FString::Printf(TEXT("Texture not found: %s"), *Val); return false; }
		TexExpr->Texture = Tex; TexExpr->AutoSetSampleType();
		OutType = TEXT("texture"); OutNorm = Tex->GetPathName(); return true;
	}
	OutError = FString::Printf(TEXT("Node class '%s' does not support defaultValue"), *Expr->GetClass()->GetName());
	return false;
}

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

static UClass* ResolveMaterialExpressionClass(const FString& ExprShort, FString& OutError)
{
	if (ExprShort.IsEmpty()) { OutError = TEXT("add_node requires expressionClass"); return nullptr; }
	const FString FullName = TEXT("MaterialExpression") + ExprShort;
	UClass* ExprClass = nullptr;
#if NX_UE_HAS_FIND_FIRST_OBJECT
	ExprClass = FindFirstObject<UClass>(*FullName, EFindFirstObjectOptions::NativeFirst);
	if (!ExprClass) ExprClass = FindFirstObject<UClass>(*ExprShort, EFindFirstObjectOptions::NativeFirst);
#else
	ExprClass = FindObject<UClass>(ANY_PACKAGE, *FullName);
	if (!ExprClass) ExprClass = FindObject<UClass>(ANY_PACKAGE, *ExprShort);
#endif
	if (!ExprClass || !ExprClass->IsChildOf(UMaterialExpression::StaticClass()) || ExprClass->HasAnyClassFlags(CLASS_Abstract))
	{
		ExprClass = nullptr;
		int32 SearchCount = 0;
		for (TObjectIterator<UClass> It; It && SearchCount < 4096; ++It, ++SearchCount)
		{
			if (It->IsChildOf(UMaterialExpression::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
			{
				if (It->GetName() == FullName || It->GetName() == ExprShort) { ExprClass = *It; break; }
			}
		}
	}
	if (!ExprClass) { OutError = FString::Printf(TEXT("Expression class not found: %s"), *ExprShort); return nullptr; }
	return ExprClass;
}

static void ApplyNewExpressionFields(UMaterialExpression* NewExpr, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (Args->HasField(TEXT("posX"))) NewExpr->MaterialExpressionEditorX = static_cast<int32>(Args->GetNumberField(TEXT("posX")));
	if (Args->HasField(TEXT("posY"))) NewExpr->MaterialExpressionEditorY = static_cast<int32>(Args->GetNumberField(TEXT("posY")));
	TArray<TSharedPtr<FJsonValue>> Applied; TArray<FString> Errs;
	if (Args->HasField(TEXT("parameterName"))) { const FString PN = Args->GetStringField(TEXT("parameterName")); if (TrySetExprParamName(NewExpr, FName(*PN))) Applied.Add(MakeShared<FJsonValueString>(TEXT("parameterName"))); else Errs.Add(FString::Printf(TEXT("parameterName not applicable to '%s'"), *NewExpr->GetClass()->GetName())); }
	FString DV; if (Args->HasField(TEXT("defaultValue"))) DV = Args->GetStringField(TEXT("defaultValue")); else if (Args->HasField(TEXT("value"))) DV = Args->GetStringField(TEXT("value"));
	if (!DV.IsEmpty()) { FString AT, NV, Err; if (TryApplyNodeDefault(NewExpr, DV, AT, NV, Err)) Applied.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("defaultValue(%s)"), *AT))); else Errs.Add(Err); }
	NewExpr->PostEditChange();
	Out->SetStringField(TEXT("nodeId"), FNexusMaterialUtils::GetExpressionNodeId(NewExpr));
	Out->SetStringField(TEXT("nodeClass"), NewExpr->GetClass()->GetName());
	if (Applied.Num() > 0) Out->SetArrayField(TEXT("appliedFields"), Applied);
	if (Errs.Num() > 0) { TArray<TSharedPtr<FJsonValue>> EA; for (const FString& E : Errs) EA.Add(MakeShared<FJsonValueString>(E)); Out->SetArrayField(TEXT("fieldErrors"), EA); Out->SetStringField(TEXT("error"), TEXT("Some fields could not be applied")); }
}

static void DoAddNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("error"), TEXT("add_node requires a UMaterial asset")); return; }
	const FString ExprShort = Args->HasField(TEXT("expressionClass")) ? Args->GetStringField(TEXT("expressionClass")) : TEXT("");
	FString ClassErr;
	UClass* ExprClass = ResolveMaterialExpressionClass(ExprShort, ClassErr);
	if (!ExprClass) { Out->SetStringField(TEXT("error"), ClassErr); return; }
	UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, ExprClass);
	if (!NewExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to create expression: %s"), *ExprShort)); return; }
	ApplyNewExpressionFields(NewExpr, Args, Out);
}

static void DoAddNodeInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("error"), TEXT("add_node requires a UMaterial or UMaterialFunction")); return; }
	const FString ExprShort = Args->HasField(TEXT("expressionClass")) ? Args->GetStringField(TEXT("expressionClass")) : TEXT("");
	FString ClassErr;
	UClass* ExprClass = ResolveMaterialExpressionClass(ExprShort, ClassErr);
	if (!ExprClass) { Out->SetStringField(TEXT("error"), ClassErr); return; }
	UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(MF, ExprClass);
	if (!NewExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to create expression: %s"), *ExprShort)); return; }
	ApplyNewExpressionFields(NewExpr, Args, Out);
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
}

static void DoRemoveNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("error"), TEXT("remove_node requires a UMaterial asset")); return; }
	const FString NodeId = Args->HasField(TEXT("nodeId")) ? Args->GetStringField(TEXT("nodeId")) : TEXT("");
	if (NodeId.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("remove_node requires nodeId")); return; }
	UMaterialExpression* Expr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, NodeId);
	if (!Expr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Expression not found: %s"), *NodeId)); return; }
	UMaterialEditingLibrary::DeleteMaterialExpression(Mat, Expr);
	Out->SetStringField(TEXT("removedNodeId"), NodeId);
}

static void DoRemoveNodeInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("error"), TEXT("remove_node requires a UMaterial or UMaterialFunction")); return; }
	const FString NodeId = Args->HasField(TEXT("nodeId")) ? Args->GetStringField(TEXT("nodeId")) : TEXT("");
	if (NodeId.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("remove_node requires nodeId")); return; }
	UMaterialExpression* Expr = FNexusMaterialUtils::FindExpressionByNodeId(MF, NodeId);
	if (!Expr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Expression not found: %s"), *NodeId)); return; }
	UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(MF, Expr);
	Out->SetStringField(TEXT("removedNodeId"), NodeId);
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
}

static void DoSetNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("error"), TEXT("set_node requires a UMaterial asset")); return; }
	const FString NodeId = Args->HasField(TEXT("nodeId")) ? Args->GetStringField(TEXT("nodeId")) : TEXT("");
	if (NodeId.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("set_node requires nodeId")); return; }
	UMaterialExpression* Expr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, NodeId);
	if (!Expr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Expression not found: %s"), *NodeId)); return; }
	Out->SetStringField(TEXT("nodeId"), NodeId); Out->SetStringField(TEXT("nodeClass"), Expr->GetClass()->GetName());
	TArray<TSharedPtr<FJsonValue>> Applied; TArray<FString> Errs; int32 Req = 0;
	if (Args->HasField(TEXT("posX"))) { Expr->MaterialExpressionEditorX = static_cast<int32>(Args->GetNumberField(TEXT("posX"))); Applied.Add(MakeShared<FJsonValueString>(TEXT("posX"))); ++Req; }
	if (Args->HasField(TEXT("posY"))) { Expr->MaterialExpressionEditorY = static_cast<int32>(Args->GetNumberField(TEXT("posY"))); Applied.Add(MakeShared<FJsonValueString>(TEXT("posY"))); ++Req; }
	if (Args->HasField(TEXT("parameterName"))) { ++Req; const FString PN = Args->GetStringField(TEXT("parameterName")); if (TrySetExprParamName(Expr, FName(*PN))) { Applied.Add(MakeShared<FJsonValueString>(TEXT("parameterName"))); Out->SetStringField(TEXT("parameterName"), PN); } else Errs.Add(FString::Printf(TEXT("parameterName not applicable to '%s'"), *Expr->GetClass()->GetName())); }
	FString DV; if (Args->HasField(TEXT("defaultValue"))) DV = Args->GetStringField(TEXT("defaultValue")); else if (Args->HasField(TEXT("value"))) DV = Args->GetStringField(TEXT("value"));
	if (!DV.IsEmpty()) { ++Req; FString AT, NV, Err; if (TryApplyNodeDefault(Expr, DV, AT, NV, Err)) { Applied.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("defaultValue(%s)"), *AT))); Out->SetStringField(TEXT("defaultValue"), NV); Out->SetStringField(TEXT("defaultValueType"), AT); } else Errs.Add(Err); }
	Expr->PostEditChange();
	if (Req == 0) { Out->SetStringField(TEXT("error"), TEXT("set_node requires at least one of: posX/posY/parameterName/defaultValue")); return; }
	if (Applied.Num() > 0) Out->SetArrayField(TEXT("appliedFields"), Applied);
	if (Errs.Num() > 0) { TArray<TSharedPtr<FJsonValue>> EA; for (const FString& E : Errs) EA.Add(MakeShared<FJsonValueString>(E)); Out->SetArrayField(TEXT("fieldErrors"), EA); Out->SetStringField(TEXT("error"), TEXT("Some fields could not be applied")); }
}

static void DoSetNodeInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("error"), TEXT("set_node requires a UMaterial or UMaterialFunction")); return; }
	const FString NodeId = Args->HasField(TEXT("nodeId")) ? Args->GetStringField(TEXT("nodeId")) : TEXT("");
	if (NodeId.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("set_node requires nodeId")); return; }
	UMaterialExpression* Expr = FNexusMaterialUtils::FindExpressionByNodeId(MF, NodeId);
	if (!Expr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Expression not found: %s"), *NodeId)); return; }
	Out->SetStringField(TEXT("nodeId"), NodeId); Out->SetStringField(TEXT("nodeClass"), Expr->GetClass()->GetName());
	TArray<TSharedPtr<FJsonValue>> Applied; TArray<FString> Errs; int32 Req = 0;
	if (Args->HasField(TEXT("posX"))) { Expr->MaterialExpressionEditorX = static_cast<int32>(Args->GetNumberField(TEXT("posX"))); Applied.Add(MakeShared<FJsonValueString>(TEXT("posX"))); ++Req; }
	if (Args->HasField(TEXT("posY"))) { Expr->MaterialExpressionEditorY = static_cast<int32>(Args->GetNumberField(TEXT("posY"))); Applied.Add(MakeShared<FJsonValueString>(TEXT("posY"))); ++Req; }
	if (Args->HasField(TEXT("parameterName"))) { ++Req; const FString PN = Args->GetStringField(TEXT("parameterName")); if (TrySetExprParamName(Expr, FName(*PN))) { Applied.Add(MakeShared<FJsonValueString>(TEXT("parameterName"))); Out->SetStringField(TEXT("parameterName"), PN); } else Errs.Add(FString::Printf(TEXT("parameterName not applicable to '%s'"), *Expr->GetClass()->GetName())); }
	FString DV; if (Args->HasField(TEXT("defaultValue"))) DV = Args->GetStringField(TEXT("defaultValue")); else if (Args->HasField(TEXT("value"))) DV = Args->GetStringField(TEXT("value"));
	if (!DV.IsEmpty()) { ++Req; FString AT, NV, Err; if (TryApplyNodeDefault(Expr, DV, AT, NV, Err)) { Applied.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("defaultValue(%s)"), *AT))); Out->SetStringField(TEXT("defaultValue"), NV); Out->SetStringField(TEXT("defaultValueType"), AT); } else Errs.Add(Err); }
	Expr->PostEditChange();
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
	if (Req == 0) { Out->SetStringField(TEXT("error"), TEXT("set_node requires at least one of: posX/posY/parameterName/defaultValue")); return; }
	if (Applied.Num() > 0) Out->SetArrayField(TEXT("appliedFields"), Applied);
	if (Errs.Num() > 0) { TArray<TSharedPtr<FJsonValue>> EA; for (const FString& E : Errs) EA.Add(MakeShared<FJsonValueString>(E)); Out->SetArrayField(TEXT("fieldErrors"), EA); Out->SetStringField(TEXT("error"), TEXT("Some fields could not be applied")); }
}

/** 按名查输出索引；名为空返回 0（取第一个），找不到返回 -1。 */
static int32 FindOutputIdx(UMaterialExpression* Expr, const FString& OutputName)
{
	if (OutputName.IsEmpty()) return 0;
	const TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
	for (int32 i = 0; i < Outputs.Num(); ++i) { if (Outputs[i].OutputName.ToString() == OutputName) return i; }
	return -1;
}

/** 按名查输入索引；名为空返回 0（取第一个），找不到返回 -1。 */
static int32 FindInputIdx(UMaterialExpression* Expr, const FString& InputName)
{
	if (InputName.IsEmpty()) return 0;
	for (int32 i = 0; Expr->GetInput(i) != nullptr; ++i) { if (Expr->GetInputName(i).ToString() == InputName) return i; }
	return -1;
}

static bool ClearMatPropInput(UMaterial* Mat, EMaterialProperty Prop)
{
#if NX_UE_HAS_MATERIAL_EDITOR_ONLY_DATA
	auto* D = Mat->GetEditorOnlyData();
	#define NX_CLR(F) { D->F.Expression = nullptr; D->F.OutputIndex = 0; return true; }
#else
	#define NX_CLR(F) { Mat->F.Expression = nullptr; Mat->F.OutputIndex = 0; return true; }
#endif
	switch (Prop)
	{
	case MP_BaseColor:           NX_CLR(BaseColor)
	case MP_Metallic:            NX_CLR(Metallic)
	case MP_Specular:            NX_CLR(Specular)
	case MP_Roughness:           NX_CLR(Roughness)
	case MP_EmissiveColor:       NX_CLR(EmissiveColor)
	case MP_Opacity:             NX_CLR(Opacity)
	case MP_OpacityMask:         NX_CLR(OpacityMask)
	case MP_Normal:              NX_CLR(Normal)
	case MP_WorldPositionOffset: NX_CLR(WorldPositionOffset)
	case MP_AmbientOcclusion:    NX_CLR(AmbientOcclusion)
	case MP_SubsurfaceColor:     NX_CLR(SubsurfaceColor)
	default:                     return false;
	}
#undef NX_CLR
}

static int32 DisconnectMatPropInputs(UMaterial* Mat, UMaterialExpression* SrcExpr, bool bFilterOut, int32 OutIdx)
{
	int32 Count = 0;
#if NX_UE_HAS_MATERIAL_EDITOR_ONLY_DATA
	auto* D = Mat->GetEditorOnlyData();
	#define NX_CHK(F) if (D->F.Expression == SrcExpr && (!bFilterOut || D->F.OutputIndex == OutIdx)) { D->F.Expression = nullptr; D->F.OutputIndex = 0; ++Count; }
#else
	#define NX_CHK(F) if (Mat->F.Expression == SrcExpr && (!bFilterOut || Mat->F.OutputIndex == OutIdx)) { Mat->F.Expression = nullptr; Mat->F.OutputIndex = 0; ++Count; }
#endif
	NX_CHK(BaseColor) NX_CHK(Metallic) NX_CHK(Specular) NX_CHK(Roughness) NX_CHK(EmissiveColor)
	NX_CHK(Opacity) NX_CHK(OpacityMask) NX_CHK(Normal) NX_CHK(WorldPositionOffset)
	NX_CHK(AmbientOcclusion) NX_CHK(SubsurfaceColor)
#undef NX_CHK
	return Count;
}

static EMaterialProperty ParseMatProp(const FString& Name)
{
	const FString L = Name.ToLower();
	if (L == TEXT("basecolor"))           return MP_BaseColor;
	if (L == TEXT("metallic"))            return MP_Metallic;
	if (L == TEXT("specular"))            return MP_Specular;
	if (L == TEXT("roughness"))           return MP_Roughness;
	if (L == TEXT("emissivecolor"))       return MP_EmissiveColor;
	if (L == TEXT("opacity"))             return MP_Opacity;
	if (L == TEXT("opacitymask"))         return MP_OpacityMask;
	if (L == TEXT("normal"))              return MP_Normal;
	if (L == TEXT("worldpositionoffset")) return MP_WorldPositionOffset;
	if (L == TEXT("ambientocclusion"))    return MP_AmbientOcclusion;
	if (L == TEXT("subsurfacecolor"))     return MP_SubsurfaceColor;
	return MP_MAX;
}

static void DoConnect(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("error"), TEXT("connect requires a UMaterial asset")); return; }
	const FString SourceNodeId     = Args->HasField(TEXT("sourceNodeId"))     ? Args->GetStringField(TEXT("sourceNodeId"))     : TEXT("");
	const FString SourceOutputName = Args->HasField(TEXT("sourceOutputName")) ? Args->GetStringField(TEXT("sourceOutputName")) : TEXT("");
	const FString TargetNodeId     = Args->HasField(TEXT("targetNodeId"))     ? Args->GetStringField(TEXT("targetNodeId"))     : TEXT("");
	const FString TargetInputName  = Args->HasField(TEXT("targetInputName"))  ? Args->GetStringField(TEXT("targetInputName"))  : TEXT("");
	if (SourceNodeId.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("connect requires sourceNodeId")); return; }
	UMaterialExpression* SrcExpr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, SourceNodeId);
	if (!SrcExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Source expression not found: %s"), *SourceNodeId)); return; }
	if (TargetNodeId.ToLower() == TEXT("material"))
	{
		EMaterialProperty Prop = ParseMatProp(TargetInputName);
		if (Prop == MP_MAX) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown material property: %s"), *TargetInputName)); return; }
		if (!UMaterialEditingLibrary::ConnectMaterialProperty(SrcExpr, SourceOutputName, Prop)) { Out->SetStringField(TEXT("error"), TEXT("ConnectMaterialProperty failed")); return; }
	}
	else
	{
		UMaterialExpression* DstExpr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, TargetNodeId);
		if (!DstExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Target expression not found: %s"), *TargetNodeId)); return; }
		if (!UMaterialEditingLibrary::ConnectMaterialExpressions(SrcExpr, SourceOutputName, DstExpr, TargetInputName)) { Out->SetStringField(TEXT("error"), TEXT("ConnectMaterialExpressions failed")); return; }
	}
	Mat->PostEditChange();
}

static void DoDisconnect(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("error"), TEXT("disconnect requires a UMaterial asset")); return; }
	const FString TargetNodeId    = Args->HasField(TEXT("targetNodeId"))    ? Args->GetStringField(TEXT("targetNodeId"))    : TEXT("");
	const FString TargetInputName = Args->HasField(TEXT("targetInputName")) ? Args->GetStringField(TEXT("targetInputName")) : TEXT("");
	if (TargetNodeId.IsEmpty() || TargetInputName.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("disconnect requires targetNodeId and targetInputName")); return; }
	if (TargetNodeId.ToLower() == TEXT("material"))
	{
		EMaterialProperty Prop = ParseMatProp(TargetInputName);
		if (Prop == MP_MAX) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown material property: %s"), *TargetInputName)); return; }
		ClearMatPropInput(Mat, Prop);
	}
	else
	{
		UMaterialExpression* DstExpr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, TargetNodeId);
		if (!DstExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Target expression not found: %s"), *TargetNodeId)); return; }
		int32 InputIdx = FindInputIdx(DstExpr, TargetInputName);
		if (InputIdx < 0) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Input pin '%s' not found on node '%s'"), *TargetNodeId, *TargetInputName)); return; }
		FExpressionInput* Inp = DstExpr->GetInput(InputIdx);
		if (Inp) { Inp->Expression = nullptr; Inp->OutputIndex = 0; }
	}
	Mat->PostEditChange();
}

static void DoDisconnectAll(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("error"), TEXT("disconnect_all requires a UMaterial asset")); return; }
	const FString SourceNodeId     = Args->HasField(TEXT("sourceNodeId"))     ? Args->GetStringField(TEXT("sourceNodeId"))     : TEXT("");
	const FString SourceOutputName = Args->HasField(TEXT("sourceOutputName")) ? Args->GetStringField(TEXT("sourceOutputName")) : TEXT("");
	if (SourceNodeId.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("disconnect_all requires sourceNodeId")); return; }
	UMaterialExpression* SrcExpr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, SourceNodeId);
	if (!SrcExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Source expression not found: %s"), *SourceNodeId)); return; }
	int32 Count = 0;
	int32 OutIdx = FindOutputIdx(SrcExpr, SourceOutputName);
	if (!SourceOutputName.IsEmpty() && OutIdx < 0)
	{
		Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Output pin '%s' not found on node '%s'"), *SourceNodeId, *SourceOutputName));
		return;
	}
	for (UMaterialExpression* Expr : FNexusMaterialUtils::GetExpressions(Mat))
	{
		if (!Expr) continue;
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Inp = Expr->GetInput(i);
			if (!Inp) break;
			if (Inp->Expression == SrcExpr && (SourceOutputName.IsEmpty() || Inp->OutputIndex == OutIdx))
			{ Inp->Expression = nullptr; Inp->OutputIndex = 0; ++Count; }
		}
	}
	Count += DisconnectMatPropInputs(Mat, SrcExpr, !SourceOutputName.IsEmpty(), OutIdx);
	Out->SetNumberField(TEXT("disconnectedCount"), Count);
	Mat->PostEditChange();
}

static UMaterialExpression* FindFunctionOutputExpr(UMaterialFunction* MF)
{
	for (UMaterialExpression* Expr : FNexusMaterialUtils::GetExpressions(MF))
	{
		if (Cast<UMaterialExpressionFunctionOutput>(Expr)) return Expr;
	}
	return nullptr;
}

static void DoConnectInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("error"), TEXT("connect requires a UMaterial or UMaterialFunction")); return; }
	const FString SourceNodeId     = Args->HasField(TEXT("sourceNodeId"))     ? Args->GetStringField(TEXT("sourceNodeId"))     : TEXT("");
	const FString SourceOutputName = Args->HasField(TEXT("sourceOutputName")) ? Args->GetStringField(TEXT("sourceOutputName")) : TEXT("");
	FString TargetNodeId           = Args->HasField(TEXT("targetNodeId"))     ? Args->GetStringField(TEXT("targetNodeId"))     : TEXT("");
	const FString TargetInputName  = Args->HasField(TEXT("targetInputName"))  ? Args->GetStringField(TEXT("targetInputName"))  : TEXT("");
	if (SourceNodeId.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("connect requires sourceNodeId")); return; }
	if (TargetNodeId.ToLower() == TEXT("material"))
	{
		Out->SetStringField(TEXT("error"), TEXT("MaterialFunction cannot connect Material property; use FunctionOutput nodeId or targetNodeId=output"));
		return;
	}
	UMaterialExpression* SrcExpr = FNexusMaterialUtils::FindExpressionByNodeId(MF, SourceNodeId);
	if (!SrcExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Source expression not found: %s"), *SourceNodeId)); return; }
	UMaterialExpression* DstExpr = nullptr;
	if (TargetNodeId.ToLower() == TEXT("output"))
	{
		DstExpr = FindFunctionOutputExpr(MF);
		if (!DstExpr) { Out->SetStringField(TEXT("error"), TEXT("FunctionOutput node not found")); return; }
	}
	else
	{
		DstExpr = FNexusMaterialUtils::FindExpressionByNodeId(MF, TargetNodeId);
	}
	if (!DstExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Target expression not found: %s"), *TargetNodeId)); return; }
	if (!UMaterialEditingLibrary::ConnectMaterialExpressions(SrcExpr, SourceOutputName, DstExpr, TargetInputName))
	{
		Out->SetStringField(TEXT("error"), TEXT("ConnectMaterialExpressions failed"));
		return;
	}
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
	MF->PostEditChange();
}

static void DoDisconnectInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("error"), TEXT("disconnect requires a UMaterial or UMaterialFunction")); return; }
	FString TargetNodeId          = Args->HasField(TEXT("targetNodeId"))    ? Args->GetStringField(TEXT("targetNodeId"))    : TEXT("");
	const FString TargetInputName = Args->HasField(TEXT("targetInputName")) ? Args->GetStringField(TEXT("targetInputName")) : TEXT("");
	if (TargetNodeId.IsEmpty() || TargetInputName.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("disconnect requires targetNodeId and targetInputName")); return; }
	if (TargetNodeId.ToLower() == TEXT("material"))
	{
		Out->SetStringField(TEXT("error"), TEXT("MaterialFunction cannot disconnect Material property; use FunctionOutput nodeId or targetNodeId=output"));
		return;
	}
	UMaterialExpression* DstExpr = (TargetNodeId.ToLower() == TEXT("output"))
		? FindFunctionOutputExpr(MF)
		: FNexusMaterialUtils::FindExpressionByNodeId(MF, TargetNodeId);
	if (!DstExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Target expression not found: %s"), *TargetNodeId)); return; }
	int32 InputIdx = FindInputIdx(DstExpr, TargetInputName);
	if (InputIdx < 0) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Input pin '%s' not found on node '%s'"), *TargetNodeId, *TargetInputName)); return; }
	FExpressionInput* Inp = DstExpr->GetInput(InputIdx);
	if (Inp) { Inp->Expression = nullptr; Inp->OutputIndex = 0; }
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
	MF->PostEditChange();
}

static void DoDisconnectAllInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("error"), TEXT("disconnect_all requires a UMaterial or UMaterialFunction")); return; }
	const FString SourceNodeId     = Args->HasField(TEXT("sourceNodeId"))     ? Args->GetStringField(TEXT("sourceNodeId"))     : TEXT("");
	const FString SourceOutputName = Args->HasField(TEXT("sourceOutputName")) ? Args->GetStringField(TEXT("sourceOutputName")) : TEXT("");
	if (SourceNodeId.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("disconnect_all requires sourceNodeId")); return; }
	UMaterialExpression* SrcExpr = FNexusMaterialUtils::FindExpressionByNodeId(MF, SourceNodeId);
	if (!SrcExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Source expression not found: %s"), *SourceNodeId)); return; }
	int32 Count = 0;
	int32 OutIdx = FindOutputIdx(SrcExpr, SourceOutputName);
	if (!SourceOutputName.IsEmpty() && OutIdx < 0)
	{
		Out->SetStringField(TEXT("error"), FString::Printf(TEXT("Output pin '%s' not found on node '%s'"), *SourceNodeId, *SourceOutputName));
		return;
	}
	for (UMaterialExpression* Expr : FNexusMaterialUtils::GetExpressions(MF))
	{
		if (!Expr) continue;
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Inp = Expr->GetInput(i);
			if (!Inp) break;
			if (Inp->Expression == SrcExpr && (SourceOutputName.IsEmpty() || Inp->OutputIndex == OutIdx))
			{ Inp->Expression = nullptr; Inp->OutputIndex = 0; ++Count; }
		}
	}
	Out->SetNumberField(TEXT("disconnectedCount"), Count);
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
	MF->PostEditChange();
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
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { DoAddNode(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { DoAddNodeInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_node requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialRemoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { DoRemoveNode(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { DoRemoveNodeInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_node requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialSetNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { DoSetNode(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { DoSetNodeInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_node requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialConnect(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { DoConnect(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { DoConnectInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("error"), TEXT("connect requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialDisconnect(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { DoDisconnect(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { DoDisconnectInFunction(MF, Op, Ctx.Entry); }
	else { Ctx.Entry->SetStringField(TEXT("error"), TEXT("disconnect requires a UMaterial or UMaterialFunction")); }
}

static void HandleMaterialDisconnectAll(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UObject* Obj = MaterialTargetFromContext(Ctx);
	if (UMaterial* Mat = Cast<UMaterial>(Obj)) { DoDisconnectAll(Mat, Op, Ctx.Entry); }
	else if (UMaterialFunction* MF = Cast<UMaterialFunction>(Obj)) { DoDisconnectAllInFunction(MF, Op, Ctx.Entry); }
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
