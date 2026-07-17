// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusManageAssetMaterialCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusMaterialUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
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
		if (Parts.Num() < 3) { OutError = FString::Printf(TEXT("Vector defaultValue 需要 'R,G,B' 或 'R,G,B,A'，实际: %s"), *Val); return false; }
		Vec->DefaultValue.R = FCString::Atof(*Parts[0]); Vec->DefaultValue.G = FCString::Atof(*Parts[1]);
		Vec->DefaultValue.B = FCString::Atof(*Parts[2]); Vec->DefaultValue.A = Parts.Num() >= 4 ? FCString::Atof(*Parts[3]) : 1.0f;
		OutType = TEXT("vector"); OutNorm = FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f"), Vec->DefaultValue.R, Vec->DefaultValue.G, Vec->DefaultValue.B, Vec->DefaultValue.A); return true;
	}
	if (UMaterialExpressionTextureBase* TexExpr = Cast<UMaterialExpressionTextureBase>(Expr))
	{
		UTexture* Tex = LoadObject<UTexture>(nullptr, *Val);
		if (!Tex) { OutError = FString::Printf(TEXT("纹理未找到: %s"), *Val); return false; }
		TexExpr->Texture = Tex; TexExpr->AutoSetSampleType();
		OutType = TEXT("texture"); OutNorm = Tex->GetPathName(); return true;
	}
	OutError = FString::Printf(TEXT("节点类 '%s' 不支持 defaultValue"), *Expr->GetClass()->GetName());
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
		if (P.Num() < 3) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("vector 值需要 'R,G,B' 或 'R,G,B,A'，实际: %s"), *V)); return; }
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
		if (!Tex) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("纹理未找到: %s"), *V)); return; }
		MI->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(*PN), Tex);
		Out->SetStringField(TEXT("paramName"), PN);
		Out->SetStringField(TEXT("paramType"), TEXT("texture"));
		Out->SetStringField(TEXT("value"), Tex->GetOutermost()->GetName());
	}
	else { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 paramType: %s"), *PT)); return; }
	MI->PostEditChange();
	MI->MarkPackageDirty();
}

static void DoAddNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("error"), TEXT("add_node requires a UMaterial asset")); return; }
	const FString ExprShort = Args->HasField(TEXT("expressionClass")) ? Args->GetStringField(TEXT("expressionClass")) : TEXT("");
	if (ExprShort.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("add_node requires expressionClass")); return; }
	const FString FullName = TEXT("MaterialExpression") + ExprShort;
	UClass* ExprClass = nullptr;
	// 优先精确查找，避免全局遍历
#if NX_UE_HAS_FIND_FIRST_OBJECT
	ExprClass = FindFirstObject<UClass>(*FullName, EFindFirstObjectOptions::NativeFirst);
	if (!ExprClass) ExprClass = FindFirstObject<UClass>(*ExprShort, EFindFirstObjectOptions::NativeFirst);
#else
	ExprClass = FindObject<UClass>(ANY_PACKAGE, *FullName);
	if (!ExprClass) ExprClass = FindObject<UClass>(ANY_PACKAGE, *ExprShort);
#endif
	if (!ExprClass || !ExprClass->IsChildOf(UMaterialExpression::StaticClass()) || ExprClass->HasAnyClassFlags(CLASS_Abstract))
	{
		// 回退到有上限的迭代查找（最多遍历 4096 个 UClass）
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
	if (!ExprClass) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("表达式类未找到: %s"), *ExprShort)); return; }
	UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, ExprClass);
	if (!NewExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("创建表达式失败: %s"), *ExprShort)); return; }
	if (Args->HasField(TEXT("posX"))) NewExpr->MaterialExpressionEditorX = static_cast<int32>(Args->GetNumberField(TEXT("posX")));
	if (Args->HasField(TEXT("posY"))) NewExpr->MaterialExpressionEditorY = static_cast<int32>(Args->GetNumberField(TEXT("posY")));
	TArray<TSharedPtr<FJsonValue>> Applied; TArray<FString> Errs;
	if (Args->HasField(TEXT("parameterName"))) { const FString PN = Args->GetStringField(TEXT("parameterName")); if (TrySetExprParamName(NewExpr, FName(*PN))) Applied.Add(MakeShared<FJsonValueString>(TEXT("parameterName"))); else Errs.Add(FString::Printf(TEXT("parameterName 不适用于 '%s'"), *NewExpr->GetClass()->GetName())); }
	FString DV; if (Args->HasField(TEXT("defaultValue"))) DV = Args->GetStringField(TEXT("defaultValue")); else if (Args->HasField(TEXT("value"))) DV = Args->GetStringField(TEXT("value"));
	if (!DV.IsEmpty()) { FString AT, NV, Err; if (TryApplyNodeDefault(NewExpr, DV, AT, NV, Err)) Applied.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("defaultValue(%s)"), *AT))); else Errs.Add(Err); }
	NewExpr->PostEditChange();
	Out->SetStringField(TEXT("nodeId"), FNexusMaterialUtils::GetExpressionNodeId(NewExpr));
	Out->SetStringField(TEXT("nodeClass"), NewExpr->GetClass()->GetName());
	if (Applied.Num() > 0) Out->SetArrayField(TEXT("appliedFields"), Applied);
	if (Errs.Num() > 0) { TArray<TSharedPtr<FJsonValue>> EA; for (const FString& E : Errs) EA.Add(MakeShared<FJsonValueString>(E)); Out->SetArrayField(TEXT("fieldErrors"), EA); Out->SetStringField(TEXT("error"), TEXT("部分字段未能应用")); }
}

static void DoRemoveNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("error"), TEXT("remove_node requires a UMaterial asset")); return; }
	const FString NodeId = Args->HasField(TEXT("nodeId")) ? Args->GetStringField(TEXT("nodeId")) : TEXT("");
	if (NodeId.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("remove_node requires nodeId")); return; }
	UMaterialExpression* Expr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, NodeId);
	if (!Expr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("表达式未找到: %s"), *NodeId)); return; }
	UMaterialEditingLibrary::DeleteMaterialExpression(Mat, Expr);
	Out->SetStringField(TEXT("removedNodeId"), NodeId);
}

static void DoSetNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("error"), TEXT("set_node requires a UMaterial asset")); return; }
	const FString NodeId = Args->HasField(TEXT("nodeId")) ? Args->GetStringField(TEXT("nodeId")) : TEXT("");
	if (NodeId.IsEmpty()) { Out->SetStringField(TEXT("error"), TEXT("set_node requires nodeId")); return; }
	UMaterialExpression* Expr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, NodeId);
	if (!Expr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("表达式未找到: %s"), *NodeId)); return; }
	Out->SetStringField(TEXT("nodeId"), NodeId); Out->SetStringField(TEXT("nodeClass"), Expr->GetClass()->GetName());
	TArray<TSharedPtr<FJsonValue>> Applied; TArray<FString> Errs; int32 Req = 0;
	if (Args->HasField(TEXT("posX"))) { Expr->MaterialExpressionEditorX = static_cast<int32>(Args->GetNumberField(TEXT("posX"))); Applied.Add(MakeShared<FJsonValueString>(TEXT("posX"))); ++Req; }
	if (Args->HasField(TEXT("posY"))) { Expr->MaterialExpressionEditorY = static_cast<int32>(Args->GetNumberField(TEXT("posY"))); Applied.Add(MakeShared<FJsonValueString>(TEXT("posY"))); ++Req; }
	if (Args->HasField(TEXT("parameterName"))) { ++Req; const FString PN = Args->GetStringField(TEXT("parameterName")); if (TrySetExprParamName(Expr, FName(*PN))) { Applied.Add(MakeShared<FJsonValueString>(TEXT("parameterName"))); Out->SetStringField(TEXT("parameterName"), PN); } else Errs.Add(FString::Printf(TEXT("parameterName 不适用于 '%s'"), *Expr->GetClass()->GetName())); }
	FString DV; if (Args->HasField(TEXT("defaultValue"))) DV = Args->GetStringField(TEXT("defaultValue")); else if (Args->HasField(TEXT("value"))) DV = Args->GetStringField(TEXT("value"));
	if (!DV.IsEmpty()) { ++Req; FString AT, NV, Err; if (TryApplyNodeDefault(Expr, DV, AT, NV, Err)) { Applied.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("defaultValue(%s)"), *AT))); Out->SetStringField(TEXT("defaultValue"), NV); Out->SetStringField(TEXT("defaultValueType"), AT); } else Errs.Add(Err); }
	Expr->PostEditChange();
	if (Req == 0) { Out->SetStringField(TEXT("error"), TEXT("set_node requires at least one of: posX/posY/parameterName/defaultValue")); return; }
	if (Applied.Num() > 0) Out->SetArrayField(TEXT("appliedFields"), Applied);
	if (Errs.Num() > 0) { TArray<TSharedPtr<FJsonValue>> EA; for (const FString& E : Errs) EA.Add(MakeShared<FJsonValueString>(E)); Out->SetArrayField(TEXT("fieldErrors"), EA); Out->SetStringField(TEXT("error"), TEXT("部分字段未能应用")); }
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
	if (!SrcExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("源表达式未找到: %s"), *SourceNodeId)); return; }
	if (TargetNodeId.ToLower() == TEXT("material"))
	{
		EMaterialProperty Prop = ParseMatProp(TargetInputName);
		if (Prop == MP_MAX) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("未知材质属性: %s"), *TargetInputName)); return; }
		if (!UMaterialEditingLibrary::ConnectMaterialProperty(SrcExpr, SourceOutputName, Prop)) { Out->SetStringField(TEXT("error"), TEXT("ConnectMaterialProperty 失败")); return; }
	}
	else
	{
		UMaterialExpression* DstExpr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, TargetNodeId);
		if (!DstExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("目标表达式未找到: %s"), *TargetNodeId)); return; }
		if (!UMaterialEditingLibrary::ConnectMaterialExpressions(SrcExpr, SourceOutputName, DstExpr, TargetInputName)) { Out->SetStringField(TEXT("error"), TEXT("ConnectMaterialExpressions 失败")); return; }
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
		if (Prop == MP_MAX) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("未知材质属性: %s"), *TargetInputName)); return; }
		ClearMatPropInput(Mat, Prop);
	}
	else
	{
		UMaterialExpression* DstExpr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, TargetNodeId);
		if (!DstExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("目标表达式未找到: %s"), *TargetNodeId)); return; }
		int32 InputIdx = FindInputIdx(DstExpr, TargetInputName);
		if (InputIdx < 0) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("节点 '%s' 上未找到输入引脚 '%s'"), *TargetNodeId, *TargetInputName)); return; }
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
	if (!SrcExpr) { Out->SetStringField(TEXT("error"), FString::Printf(TEXT("源表达式未找到: %s"), *SourceNodeId)); return; }
	int32 Count = 0;
	int32 OutIdx = FindOutputIdx(SrcExpr, SourceOutputName);
	if (!SourceOutputName.IsEmpty() && OutIdx < 0)
	{
		Out->SetStringField(TEXT("error"), FString::Printf(TEXT("节点 '%s' 上未找到输出引脚 '%s'"), *SourceNodeId, *SourceOutputName));
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

void FManageAssetMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_material");
	Out.SearchAssetTypes = {TEXT("Material"), TEXT("MaterialInstance")};
	Out.Description = TEXT("批量编辑材质/MI：set_param/add_node/connect/recompile。");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),           FNexusSchema::Enum(TEXT("编辑操作"), { TEXT("set_param"), TEXT("add_node"), TEXT("remove_node"), TEXT("set_node"), TEXT("recompile"), TEXT("connect"), TEXT("disconnect"), TEXT("disconnect_all") }))
		.Prop(TEXT("paramName"),        FNexusSchema::Str(TEXT("参数名（set_param）")))
		.Prop(TEXT("paramType"),        FNexusSchema::Enum(TEXT("参数类型"), { TEXT("scalar"), TEXT("vector"), TEXT("texture") }))
		.Prop(TEXT("value"),            FNexusSchema::Str(TEXT("float / R,G,B,A / 纹理路径")))
		.Prop(TEXT("expressionClass"),  FNexusSchema::Str(TEXT("表达式类短名（add_node）")))
		.Prop(TEXT("parameterName"),    FNexusSchema::Str(TEXT("Parameter/TextureSampleParam 名")))
		.Prop(TEXT("defaultValue"),     FNexusSchema::Str(TEXT("float / R,G,B,A / 纹理路径")))
		.Prop(TEXT("nodeId"),           FNexusSchema::Str(TEXT("表达式节点 id（remove/set）")))
		.Prop(TEXT("posX"),             FNexusSchema::Num(TEXT("节点 X 坐标")))
		.Prop(TEXT("posY"),             FNexusSchema::Num(TEXT("节点 Y 坐标")))
		.Prop(TEXT("sourceNodeId"),     FNexusSchema::Str(TEXT("源节点 id（connect/disconnect_all）")))
		.Prop(TEXT("sourceOutputName"), FNexusSchema::Str(TEXT("源输出引脚名（默认第一个）")))
		.Prop(TEXT("targetNodeId"),     FNexusSchema::Str(TEXT("目标节点 id 或 Material（connect/disconnect）")))
		.Prop(TEXT("targetInputName"),  FNexusSchema::Str(TEXT("目标输入引脚或材质属性名")))
		.Required({ TEXT("action") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Material/MI 资产路径（共用）")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量材质操作"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = {
		TEXT("node"), TEXT("parameter"), TEXT("wire"), TEXT("pin"), TEXT("recompile")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_material"), TEXT("create_asset_material"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("写操作：设参数、增删节点、连线、重编译");
}

FCapabilityResult FManageAssetMaterialCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		if (!Arguments.IsValid())
		{
			OutError = TEXT("参数无效");
			return;
		}

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UObject* Obj = FNexusMaterialUtils::LoadMaterialAsset(AssetPath);
		if (!Obj) { OutError = FString::Printf(TEXT("资产未找到: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("operations"), OpsArr) || !OpsArr)
		{
			OutError = TEXT("缺少 operations");
			return;
		}
		if (OpsArr->Num() == 0)
		{
			OutError = TEXT("operations 不能为空");
			return;
		}

		for (const TSharedPtr<FJsonValue>& Val : *OpsArr)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("无效的操作项"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FString Action = Item->HasField(TEXT("action")) ? Item->GetStringField(TEXT("action")).ToLower() : TEXT("");
			OutEntry->SetStringField(TEXT("action"), Action);

			if (Action.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("缺少 action"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			if (Action == TEXT("recompile"))
			{
				UMaterial* Mat = Cast<UMaterial>(Obj);
				if (!Mat)
					OutEntry->SetStringField(TEXT("error"), TEXT("recompile requires a UMaterial asset"));
				else
					UMaterialEditingLibrary::RecompileMaterial(Mat);
			}
			else if (Action == TEXT("set_param"))     { DoSetParam(Cast<UMaterialInstanceConstant>(Obj), Item, OutEntry); }
			else if (Action == TEXT("add_node"))      { DoAddNode(Cast<UMaterial>(Obj), Item, OutEntry);      }
			else if (Action == TEXT("remove_node"))   { DoRemoveNode(Cast<UMaterial>(Obj), Item, OutEntry);   }
			else if (Action == TEXT("set_node"))      { DoSetNode(Cast<UMaterial>(Obj), Item, OutEntry);      }
			else if (Action == TEXT("connect"))       { DoConnect(Cast<UMaterial>(Obj), Item, OutEntry);      }
			else if (Action == TEXT("disconnect"))    { DoDisconnect(Cast<UMaterial>(Obj), Item, OutEntry);   }
			else if (Action == TEXT("disconnect_all")){ DoDisconnectAll(Cast<UMaterial>(Obj), Item, OutEntry);}
			else { OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action)); }

			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}
	
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetMaterialCapability)

#endif // WITH_EDITOR
