// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"

struct FNexusMaterialGraphOps final
{
	static bool TrySetExprParamName(UMaterialExpression* Expr, const FName& Name);
	static bool TryApplyNodeDefault(UMaterialExpression* Expr, const FString& Val, FString& OutType, FString& OutNorm, FString& OutError);
	static UClass* ResolveMaterialExpressionClass(const FString& ExprShort, FString& OutError);
	static void ApplyNewExpressionFields(UMaterialExpression* NewExpr, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static void DoAddNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static void DoAddNodeInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static void DoRemoveNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static void DoRemoveNodeInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static void DoSetNode(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static void DoSetNodeInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static int32 FindOutputIdx(UMaterialExpression* Expr, const FString& OutputName);
	static int32 FindInputIdx(UMaterialExpression* Expr, const FString& InputName);
	static bool ClearMatPropInput(UMaterial* Mat, EMaterialProperty Prop);
	static int32 DisconnectMatPropInputs(UMaterial* Mat, UMaterialExpression* SrcExpr, bool bFilterOut, int32 OutIdx);
	static EMaterialProperty ParseMatProp(const FString& Name);
	static void DoConnect(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static void DoDisconnect(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static void DoDisconnectAll(UMaterial* Mat, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static UMaterialExpression* FindFunctionOutputExpr(UMaterialFunction* MF);
	static void DoConnectInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static void DoDisconnectInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
	static void DoDisconnectAllInFunction(UMaterialFunction* MF, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Out);
};
