// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusManageAssetMaterialGraph.h"
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


bool FNexusMaterialGraphOps::TrySetExprParamName(UMaterialExpression* Expr, const FName& Name)
{
	if (UMaterialExpressionParameter* P = Cast<UMaterialExpressionParameter>(Expr)) { P->ParameterName = Name; return true; }
	if (UMaterialExpressionTextureSampleParameter* TP = Cast<UMaterialExpressionTextureSampleParameter>(Expr)) { TP->ParameterName = Name; return true; }
	return false;
}

bool FNexusMaterialGraphOps::TryApplyNodeDefault(UMaterialExpression* Expr, const FString& Val,
	FString& OutType, FString& OutNorm, FString& OutError)
{
	if (UMaterialExpressionScalarParameter* Sc = Cast<UMaterialExpressionScalarParameter>(Expr))
	{
		Sc->DefaultValue = FCString::Atof(*Val);
		OutType = TEXT("Scalar"); OutNorm = FString::Printf(TEXT("%.6f"), Sc->DefaultValue); return true;
	}
	if (UMaterialExpressionVectorParameter* Vec = Cast<UMaterialExpressionVectorParameter>(Expr))
	{
		TArray<FString> Parts; Val.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() < 3) { OutError = FString::Printf(TEXT("Vector DefaultValue requires 'R,G,B' or 'R,G,B,A', got: %s"), *Val); return false; }
		Vec->DefaultValue.R = FCString::Atof(*Parts[0]); Vec->DefaultValue.G = FCString::Atof(*Parts[1]);
		Vec->DefaultValue.B = FCString::Atof(*Parts[2]); Vec->DefaultValue.A = Parts.Num() >= 4 ? FCString::Atof(*Parts[3]) : 1.0f;
		OutType = TEXT("Vector"); OutNorm = FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f"), Vec->DefaultValue.R, Vec->DefaultValue.G, Vec->DefaultValue.B, Vec->DefaultValue.A); return true;
	}
	if (UMaterialExpressionTextureBase* TexExpr = Cast<UMaterialExpressionTextureBase>(Expr))
	{
		UTexture* Tex = LoadObject<UTexture>(nullptr, *Val);
		if (!Tex) { OutError = FString::Printf(TEXT("Texture not found: %s"), *Val); return false; }
		TexExpr->Texture = Tex; TexExpr->AutoSetSampleType();
		OutType = TEXT("Texture"); OutNorm = Tex->GetPathName(); return true;
	}
	OutError = FString::Printf(TEXT("Node class '%s' does not support DefaultValue"), *Expr->GetClass()->GetName());
	return false;
}
UClass* FNexusMaterialGraphOps::ResolveMaterialExpressionClass(const FString& ExprShort, FString& OutError)
{
	if (ExprShort.IsEmpty()) { OutError = TEXT("AddNode requires ExpressionClass"); return nullptr; }
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

void FNexusMaterialGraphOps::ApplyNewExpressionFields(UMaterialExpression* NewExpr, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (Args->HasField(TEXT("PosX"))) NewExpr->MaterialExpressionEditorX = static_cast<int32>(Args->GetNumberField(TEXT("PosX")));
	if (Args->HasField(TEXT("PosY"))) NewExpr->MaterialExpressionEditorY = static_cast<int32>(Args->GetNumberField(TEXT("PosY")));
	TArray<TSharedPtr<FJsonValue>> Applied; TArray<FString> Errs;
	if (Args->HasField(TEXT("ParameterName"))) { const FString PN = Args->GetStringField(TEXT("ParameterName")); if (TrySetExprParamName(NewExpr, FName(*PN))) Applied.Add(MakeShared<FJsonValueString>(TEXT("ParameterName"))); else Errs.Add(FString::Printf(TEXT("ParameterName not applicable to '%s'"), *NewExpr->GetClass()->GetName())); }
	FString DV; if (Args->HasField(TEXT("DefaultValue"))) DV = Args->GetStringField(TEXT("DefaultValue")); else if (Args->HasField(TEXT("Value"))) DV = Args->GetStringField(TEXT("Value"));
	if (!DV.IsEmpty()) { FString AT, NV, Err; if (TryApplyNodeDefault(NewExpr, DV, AT, NV, Err)) Applied.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("DefaultValue(%s)"), *AT))); else Errs.Add(Err); }
	NewExpr->PostEditChange();
	Out->SetStringField(TEXT("NodeId"), FNexusMaterialUtils::GetExpressionNodeId(NewExpr));
	Out->SetStringField(TEXT("NodeClass"), NewExpr->GetClass()->GetName());
	if (Applied.Num() > 0) Out->SetArrayField(TEXT("AppliedFields"), Applied);
	if (Errs.Num() > 0) { TArray<TSharedPtr<FJsonValue>> EA; for (const FString& E : Errs) EA.Add(MakeShared<FJsonValueString>(E)); Out->SetArrayField(TEXT("FieldErrors"), EA); Out->SetStringField(TEXT("Error"), TEXT("Some fields could not be applied")); }
}

void FNexusMaterialGraphOps::DoAddNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("Error"), TEXT("AddNode requires a UMaterial asset")); return; }
	const FString ExprShort = Args->HasField(TEXT("ExpressionClass")) ? Args->GetStringField(TEXT("ExpressionClass")) : TEXT("");
	FString ClassErr;
	UClass* ExprClass = ResolveMaterialExpressionClass(ExprShort, ClassErr);
	if (!ExprClass) { Out->SetStringField(TEXT("Error"), ClassErr); return; }
	UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, ExprClass);
	if (!NewExpr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to create expression: %s"), *ExprShort)); return; }
	ApplyNewExpressionFields(NewExpr, Args, Out);
}

void FNexusMaterialGraphOps::DoAddNodeInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("Error"), TEXT("AddNode requires a UMaterial or UMaterialFunction")); return; }
	const FString ExprShort = Args->HasField(TEXT("ExpressionClass")) ? Args->GetStringField(TEXT("ExpressionClass")) : TEXT("");
	FString ClassErr;
	UClass* ExprClass = ResolveMaterialExpressionClass(ExprShort, ClassErr);
	if (!ExprClass) { Out->SetStringField(TEXT("Error"), ClassErr); return; }
	UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(MF, ExprClass);
	if (!NewExpr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Failed to create expression: %s"), *ExprShort)); return; }
	ApplyNewExpressionFields(NewExpr, Args, Out);
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
}

void FNexusMaterialGraphOps::DoRemoveNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("Error"), TEXT("RemoveNode requires a UMaterial asset")); return; }
	const FString NodeId = Args->HasField(TEXT("NodeId")) ? Args->GetStringField(TEXT("NodeId")) : TEXT("");
	if (NodeId.IsEmpty()) { Out->SetStringField(TEXT("Error"), TEXT("RemoveNode requires NodeId")); return; }
	UMaterialExpression* Expr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, NodeId);
	if (!Expr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Expression not found: %s"), *NodeId)); return; }
	UMaterialEditingLibrary::DeleteMaterialExpression(Mat, Expr);
	Out->SetStringField(TEXT("RemovedNodeId"), NodeId);
}

void FNexusMaterialGraphOps::DoRemoveNodeInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("Error"), TEXT("RemoveNode requires a UMaterial or UMaterialFunction")); return; }
	const FString NodeId = Args->HasField(TEXT("NodeId")) ? Args->GetStringField(TEXT("NodeId")) : TEXT("");
	if (NodeId.IsEmpty()) { Out->SetStringField(TEXT("Error"), TEXT("RemoveNode requires NodeId")); return; }
	UMaterialExpression* Expr = FNexusMaterialUtils::FindExpressionByNodeId(MF, NodeId);
	if (!Expr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Expression not found: %s"), *NodeId)); return; }
	UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(MF, Expr);
	Out->SetStringField(TEXT("RemovedNodeId"), NodeId);
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
}

void FNexusMaterialGraphOps::DoSetNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("Error"), TEXT("SetNode requires a UMaterial asset")); return; }
	const FString NodeId = Args->HasField(TEXT("NodeId")) ? Args->GetStringField(TEXT("NodeId")) : TEXT("");
	if (NodeId.IsEmpty()) { Out->SetStringField(TEXT("Error"), TEXT("SetNode requires NodeId")); return; }
	UMaterialExpression* Expr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, NodeId);
	if (!Expr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Expression not found: %s"), *NodeId)); return; }
	Out->SetStringField(TEXT("NodeId"), NodeId); Out->SetStringField(TEXT("NodeClass"), Expr->GetClass()->GetName());
	TArray<TSharedPtr<FJsonValue>> Applied; TArray<FString> Errs; int32 Req = 0;
	if (Args->HasField(TEXT("PosX"))) { Expr->MaterialExpressionEditorX = static_cast<int32>(Args->GetNumberField(TEXT("PosX"))); Applied.Add(MakeShared<FJsonValueString>(TEXT("PosX"))); ++Req; }
	if (Args->HasField(TEXT("PosY"))) { Expr->MaterialExpressionEditorY = static_cast<int32>(Args->GetNumberField(TEXT("PosY"))); Applied.Add(MakeShared<FJsonValueString>(TEXT("PosY"))); ++Req; }
	if (Args->HasField(TEXT("ParameterName"))) { ++Req; const FString PN = Args->GetStringField(TEXT("ParameterName")); if (TrySetExprParamName(Expr, FName(*PN))) { Applied.Add(MakeShared<FJsonValueString>(TEXT("ParameterName"))); Out->SetStringField(TEXT("ParameterName"), PN); } else Errs.Add(FString::Printf(TEXT("ParameterName not applicable to '%s'"), *Expr->GetClass()->GetName())); }
	FString DV; if (Args->HasField(TEXT("DefaultValue"))) DV = Args->GetStringField(TEXT("DefaultValue")); else if (Args->HasField(TEXT("Value"))) DV = Args->GetStringField(TEXT("Value"));
	if (!DV.IsEmpty()) { ++Req; FString AT, NV, Err; if (TryApplyNodeDefault(Expr, DV, AT, NV, Err)) { Applied.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("DefaultValue(%s)"), *AT))); Out->SetStringField(TEXT("DefaultValue"), NV); Out->SetStringField(TEXT("DefaultValueType"), AT); } else Errs.Add(Err); }
	Expr->PostEditChange();
	if (Req == 0) { Out->SetStringField(TEXT("Error"), TEXT("SetNode requires at least one of: PosX/PosY/ParameterName/DefaultValue")); return; }
	if (Applied.Num() > 0) Out->SetArrayField(TEXT("AppliedFields"), Applied);
	if (Errs.Num() > 0) { TArray<TSharedPtr<FJsonValue>> EA; for (const FString& E : Errs) EA.Add(MakeShared<FJsonValueString>(E)); Out->SetArrayField(TEXT("FieldErrors"), EA); Out->SetStringField(TEXT("Error"), TEXT("Some fields could not be applied")); }
}

void FNexusMaterialGraphOps::DoSetNodeInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("Error"), TEXT("SetNode requires a UMaterial or UMaterialFunction")); return; }
	const FString NodeId = Args->HasField(TEXT("NodeId")) ? Args->GetStringField(TEXT("NodeId")) : TEXT("");
	if (NodeId.IsEmpty()) { Out->SetStringField(TEXT("Error"), TEXT("SetNode requires NodeId")); return; }
	UMaterialExpression* Expr = FNexusMaterialUtils::FindExpressionByNodeId(MF, NodeId);
	if (!Expr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Expression not found: %s"), *NodeId)); return; }
	Out->SetStringField(TEXT("NodeId"), NodeId); Out->SetStringField(TEXT("NodeClass"), Expr->GetClass()->GetName());
	TArray<TSharedPtr<FJsonValue>> Applied; TArray<FString> Errs; int32 Req = 0;
	if (Args->HasField(TEXT("PosX"))) { Expr->MaterialExpressionEditorX = static_cast<int32>(Args->GetNumberField(TEXT("PosX"))); Applied.Add(MakeShared<FJsonValueString>(TEXT("PosX"))); ++Req; }
	if (Args->HasField(TEXT("PosY"))) { Expr->MaterialExpressionEditorY = static_cast<int32>(Args->GetNumberField(TEXT("PosY"))); Applied.Add(MakeShared<FJsonValueString>(TEXT("PosY"))); ++Req; }
	if (Args->HasField(TEXT("ParameterName"))) { ++Req; const FString PN = Args->GetStringField(TEXT("ParameterName")); if (TrySetExprParamName(Expr, FName(*PN))) { Applied.Add(MakeShared<FJsonValueString>(TEXT("ParameterName"))); Out->SetStringField(TEXT("ParameterName"), PN); } else Errs.Add(FString::Printf(TEXT("ParameterName not applicable to '%s'"), *Expr->GetClass()->GetName())); }
	FString DV; if (Args->HasField(TEXT("DefaultValue"))) DV = Args->GetStringField(TEXT("DefaultValue")); else if (Args->HasField(TEXT("Value"))) DV = Args->GetStringField(TEXT("Value"));
	if (!DV.IsEmpty()) { ++Req; FString AT, NV, Err; if (TryApplyNodeDefault(Expr, DV, AT, NV, Err)) { Applied.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("DefaultValue(%s)"), *AT))); Out->SetStringField(TEXT("DefaultValue"), NV); Out->SetStringField(TEXT("DefaultValueType"), AT); } else Errs.Add(Err); }
	Expr->PostEditChange();
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
	if (Req == 0) { Out->SetStringField(TEXT("Error"), TEXT("SetNode requires at least one of: PosX/PosY/ParameterName/DefaultValue")); return; }
	if (Applied.Num() > 0) Out->SetArrayField(TEXT("AppliedFields"), Applied);
	if (Errs.Num() > 0) { TArray<TSharedPtr<FJsonValue>> EA; for (const FString& E : Errs) EA.Add(MakeShared<FJsonValueString>(E)); Out->SetArrayField(TEXT("FieldErrors"), EA); Out->SetStringField(TEXT("Error"), TEXT("Some fields could not be applied")); }
}

/** 按名查输出索引；名为空返回 0（取第一个），找不到返回 -1。 */
int32 FNexusMaterialGraphOps::FindOutputIdx(UMaterialExpression* Expr, const FString& OutputName)
{
	if (OutputName.IsEmpty()) return 0;
	const TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
	for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex) { if (Outputs[OutputIndex].OutputName.ToString() == OutputName) return OutputIndex; }
	return -1;
}

/** 按名查输入索引；名为空返回 0（取第一个），找不到返回 -1。 */
int32 FNexusMaterialGraphOps::FindInputIdx(UMaterialExpression* Expr, const FString& InputName)
{
	if (InputName.IsEmpty()) return 0;
	for (int32 i = 0; Expr->GetInput(i) != nullptr; ++i) { if (Expr->GetInputName(i).ToString() == InputName) return i; }
	return -1;
}

bool FNexusMaterialGraphOps::ClearMatPropInput(UMaterial* Mat, EMaterialProperty Prop)
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

int32 FNexusMaterialGraphOps::DisconnectMatPropInputs(UMaterial* Mat, UMaterialExpression* SrcExpr, bool bFilterOut, int32 OutIdx)
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

EMaterialProperty FNexusMaterialGraphOps::ParseMatProp(const FString& Name)
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

void FNexusMaterialGraphOps::DoConnect(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("Error"), TEXT("connect requires a UMaterial asset")); return; }
	const FString SourceNodeId     = Args->HasField(TEXT("SourceNodeId"))     ? Args->GetStringField(TEXT("SourceNodeId"))     : TEXT("");
	const FString SourceOutputName = Args->HasField(TEXT("SourceOutputName")) ? Args->GetStringField(TEXT("SourceOutputName")) : TEXT("");
	const FString TargetNodeId     = Args->HasField(TEXT("TargetNodeId"))     ? Args->GetStringField(TEXT("TargetNodeId"))     : TEXT("");
	const FString TargetInputName  = Args->HasField(TEXT("TargetInputName"))  ? Args->GetStringField(TEXT("TargetInputName"))  : TEXT("");
	if (SourceNodeId.IsEmpty()) { Out->SetStringField(TEXT("Error"), TEXT("connect requires SourceNodeId")); return; }
	UMaterialExpression* SrcExpr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, SourceNodeId);
	if (!SrcExpr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Source expression not found: %s"), *SourceNodeId)); return; }
	if (TargetNodeId.ToLower() == TEXT("Material"))
	{
		EMaterialProperty Prop = ParseMatProp(TargetInputName);
		if (Prop == MP_MAX) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unknown material property: %s"), *TargetInputName)); return; }
		if (!UMaterialEditingLibrary::ConnectMaterialProperty(SrcExpr, SourceOutputName, Prop)) { Out->SetStringField(TEXT("Error"), TEXT("ConnectMaterialProperty failed")); return; }
	}
	else
	{
		UMaterialExpression* DstExpr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, TargetNodeId);
		if (!DstExpr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Target expression not found: %s"), *TargetNodeId)); return; }
		if (!UMaterialEditingLibrary::ConnectMaterialExpressions(SrcExpr, SourceOutputName, DstExpr, TargetInputName)) { Out->SetStringField(TEXT("Error"), TEXT("ConnectMaterialExpressions failed")); return; }
	}
	Mat->PostEditChange();
}

void FNexusMaterialGraphOps::DoDisconnect(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("Error"), TEXT("disconnect requires a UMaterial asset")); return; }
	const FString TargetNodeId    = Args->HasField(TEXT("TargetNodeId"))    ? Args->GetStringField(TEXT("TargetNodeId"))    : TEXT("");
	const FString TargetInputName = Args->HasField(TEXT("TargetInputName")) ? Args->GetStringField(TEXT("TargetInputName")) : TEXT("");
	if (TargetNodeId.IsEmpty() || TargetInputName.IsEmpty()) { Out->SetStringField(TEXT("Error"), TEXT("disconnect requires TargetNodeId and TargetInputName")); return; }
	if (TargetNodeId.ToLower() == TEXT("Material"))
	{
		EMaterialProperty Prop = ParseMatProp(TargetInputName);
		if (Prop == MP_MAX) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unknown material property: %s"), *TargetInputName)); return; }
		ClearMatPropInput(Mat, Prop);
	}
	else
	{
		UMaterialExpression* DstExpr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, TargetNodeId);
		if (!DstExpr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Target expression not found: %s"), *TargetNodeId)); return; }
		int32 InputIdx = FindInputIdx(DstExpr, TargetInputName);
		if (InputIdx < 0) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Input pin '%s' not found on node '%s'"), *TargetNodeId, *TargetInputName)); return; }
		FExpressionInput* Inp = DstExpr->GetInput(InputIdx);
		if (Inp) { Inp->Expression = nullptr; Inp->OutputIndex = 0; }
	}
	Mat->PostEditChange();
}

void FNexusMaterialGraphOps::DoDisconnectAll(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!Mat) { Out->SetStringField(TEXT("Error"), TEXT("DisconnectAll requires a UMaterial asset")); return; }
	const FString SourceNodeId     = Args->HasField(TEXT("SourceNodeId"))     ? Args->GetStringField(TEXT("SourceNodeId"))     : TEXT("");
	const FString SourceOutputName = Args->HasField(TEXT("SourceOutputName")) ? Args->GetStringField(TEXT("SourceOutputName")) : TEXT("");
	if (SourceNodeId.IsEmpty()) { Out->SetStringField(TEXT("Error"), TEXT("DisconnectAll requires SourceNodeId")); return; }
	UMaterialExpression* SrcExpr = FNexusMaterialUtils::FindExpressionByNodeId(Mat, SourceNodeId);
	if (!SrcExpr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Source expression not found: %s"), *SourceNodeId)); return; }
	int32 Count = 0;
	int32 OutIdx = FindOutputIdx(SrcExpr, SourceOutputName);
	if (!SourceOutputName.IsEmpty() && OutIdx < 0)
	{
		Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Output pin '%s' not found on node '%s'"), *SourceNodeId, *SourceOutputName));
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
	Out->SetNumberField(TEXT("DisconnectedCount"), Count);
	Mat->PostEditChange();
}

UMaterialExpression* FNexusMaterialGraphOps::FindFunctionOutputExpr(UMaterialFunction* MF)
{
	for (UMaterialExpression* Expr : FNexusMaterialUtils::GetExpressions(MF))
	{
		if (Cast<UMaterialExpressionFunctionOutput>(Expr)) return Expr;
	}
	return nullptr;
}

void FNexusMaterialGraphOps::DoConnectInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("Error"), TEXT("connect requires a UMaterial or UMaterialFunction")); return; }
	const FString SourceNodeId     = Args->HasField(TEXT("SourceNodeId"))     ? Args->GetStringField(TEXT("SourceNodeId"))     : TEXT("");
	const FString SourceOutputName = Args->HasField(TEXT("SourceOutputName")) ? Args->GetStringField(TEXT("SourceOutputName")) : TEXT("");
	FString TargetNodeId           = Args->HasField(TEXT("TargetNodeId"))     ? Args->GetStringField(TEXT("TargetNodeId"))     : TEXT("");
	const FString TargetInputName  = Args->HasField(TEXT("TargetInputName"))  ? Args->GetStringField(TEXT("TargetInputName"))  : TEXT("");
	if (SourceNodeId.IsEmpty()) { Out->SetStringField(TEXT("Error"), TEXT("connect requires SourceNodeId")); return; }
	if (TargetNodeId.ToLower() == TEXT("Material"))
	{
		Out->SetStringField(TEXT("Error"), TEXT("MaterialFunction cannot connect Material property; use FunctionOutput NodeId or TargetNodeId=output"));
		return;
	}
	UMaterialExpression* SrcExpr = FNexusMaterialUtils::FindExpressionByNodeId(MF, SourceNodeId);
	if (!SrcExpr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Source expression not found: %s"), *SourceNodeId)); return; }
	UMaterialExpression* DstExpr = nullptr;
	if (TargetNodeId.ToLower() == TEXT("Output"))
	{
		DstExpr = FindFunctionOutputExpr(MF);
		if (!DstExpr) { Out->SetStringField(TEXT("Error"), TEXT("FunctionOutput node not found")); return; }
	}
	else
	{
		DstExpr = FNexusMaterialUtils::FindExpressionByNodeId(MF, TargetNodeId);
	}
	if (!DstExpr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Target expression not found: %s"), *TargetNodeId)); return; }
	if (!UMaterialEditingLibrary::ConnectMaterialExpressions(SrcExpr, SourceOutputName, DstExpr, TargetInputName))
	{
		Out->SetStringField(TEXT("Error"), TEXT("ConnectMaterialExpressions failed"));
		return;
	}
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
	MF->PostEditChange();
}

void FNexusMaterialGraphOps::DoDisconnectInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("Error"), TEXT("disconnect requires a UMaterial or UMaterialFunction")); return; }
	FString TargetNodeId          = Args->HasField(TEXT("TargetNodeId"))    ? Args->GetStringField(TEXT("TargetNodeId"))    : TEXT("");
	const FString TargetInputName = Args->HasField(TEXT("TargetInputName")) ? Args->GetStringField(TEXT("TargetInputName")) : TEXT("");
	if (TargetNodeId.IsEmpty() || TargetInputName.IsEmpty()) { Out->SetStringField(TEXT("Error"), TEXT("disconnect requires TargetNodeId and TargetInputName")); return; }
	if (TargetNodeId.ToLower() == TEXT("Material"))
	{
		Out->SetStringField(TEXT("Error"), TEXT("MaterialFunction cannot disconnect Material property; use FunctionOutput NodeId or TargetNodeId=output"));
		return;
	}
	UMaterialExpression* DstExpr = (TargetNodeId.ToLower() == TEXT("Output"))
		? FindFunctionOutputExpr(MF)
		: FNexusMaterialUtils::FindExpressionByNodeId(MF, TargetNodeId);
	if (!DstExpr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Target expression not found: %s"), *TargetNodeId)); return; }
	int32 InputIdx = FindInputIdx(DstExpr, TargetInputName);
	if (InputIdx < 0) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Input pin '%s' not found on node '%s'"), *TargetNodeId, *TargetInputName)); return; }
	FExpressionInput* Inp = DstExpr->GetInput(InputIdx);
	if (Inp) { Inp->Expression = nullptr; Inp->OutputIndex = 0; }
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
	MF->PostEditChange();
}

void FNexusMaterialGraphOps::DoDisconnectAllInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out)
{
	if (!MF) { Out->SetStringField(TEXT("Error"), TEXT("DisconnectAll requires a UMaterial or UMaterialFunction")); return; }
	const FString SourceNodeId     = Args->HasField(TEXT("SourceNodeId"))     ? Args->GetStringField(TEXT("SourceNodeId"))     : TEXT("");
	const FString SourceOutputName = Args->HasField(TEXT("SourceOutputName")) ? Args->GetStringField(TEXT("SourceOutputName")) : TEXT("");
	if (SourceNodeId.IsEmpty()) { Out->SetStringField(TEXT("Error"), TEXT("DisconnectAll requires SourceNodeId")); return; }
	UMaterialExpression* SrcExpr = FNexusMaterialUtils::FindExpressionByNodeId(MF, SourceNodeId);
	if (!SrcExpr) { Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Source expression not found: %s"), *SourceNodeId)); return; }
	int32 Count = 0;
	int32 OutIdx = FindOutputIdx(SrcExpr, SourceOutputName);
	if (!SourceOutputName.IsEmpty() && OutIdx < 0)
	{
		Out->SetStringField(TEXT("Error"), FString::Printf(TEXT("Output pin '%s' not found on node '%s'"), *SourceNodeId, *SourceOutputName));
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
	Out->SetNumberField(TEXT("DisconnectedCount"), Count);
	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
	MF->PostEditChange();
}

#endif // WITH_EDITOR

