// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Domain
#include "CoreMinimal.h"
#include "Misc/PackageName.h"
#include "Utils/NexusVersionCompat.h"
#if NX_UE_HAS_MATERIAL_DOMAIN_HEADER
#include "MaterialDomain.h"
#endif
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionParameter.h"

/**
 * 材质 / MaterialFunction 表达式侧共用工具（跨 UE 版本取表达式列表、按 nodeId 定位、路径加载）。
 */
class NEXUSLINK_API FNexusMaterialUtils
{
public:
	/**
	 * EMaterialDomain 枚举 → 可读字符串（如 "Surface"、"DeferredDecal"）。
	 * 利用 StaticEnum 元数据，去除 "MD_" 前缀后返回。
	 */
	static FString DomainToString(EMaterialDomain D);

	/**
	 * EBlendMode 枚举 → 可读字符串（Opaque / Masked / Translucent / Additive / Modulate / Unknown）。
	 */
	static FString BlendModeToString(EBlendMode M);

	/**
	 * 解析 materialDomain 字符串为 EMaterialDomain 枚举。
	 * 支持：surface / deferredDecal(decal) / lightFunction / volume / postProcess / ui / runtimeVirtualTexture(rvt)。
	 * 失败时 OutErr 非空，返回 false。
	 */
	static bool TryParseMaterialDomain(const FString& InStr, EMaterialDomain& OutDomain, FString& OutErr);
#if WITH_EDITOR
#if NX_UE_HAS_MATERIAL_EDITOR_ONLY_DATA
	/** 跨版本获取材质表达式列表引用（UE5.1+：EditorOnlyData）。 */
	static auto& GetExpressions(UMaterial* Mat)
	{
		return Mat->GetEditorOnlyData()->ExpressionCollection.Expressions;
	}
	/** 跨版本获取 MaterialFunction 表达式列表引用（UE5.1+）。 */
	static auto& GetExpressions(UMaterialFunction* MF)
	{
		return MF->GetEditorOnlyData()->ExpressionCollection.Expressions;
	}
#else
	/** 跨版本获取材质表达式列表引用（UE4/5.0：直接成员）。 */
	static TArray<UMaterialExpression*>& GetExpressions(UMaterial* Mat)
	{
		return Mat->Expressions;
	}
	/** 跨版本获取 MaterialFunction 表达式列表引用（UE4/5.0）。 */
	static TArray<UMaterialExpression*>& GetExpressions(UMaterialFunction* MF)
	{
		return MF->FunctionExpressions;
	}
#endif
#else
	/** 非编辑器构建：表达式 API 不可用，返回空列表。 */
	static TArray<UMaterialExpression*>& GetExpressions(UMaterial* Mat)
	{
		(void)Mat;
		static TArray<UMaterialExpression*> Empty;
		return Empty;
	}
	static TArray<UMaterialExpression*>& GetExpressions(UMaterialFunction* MF)
	{
		(void)MF;
		static TArray<UMaterialExpression*> Empty;
		return Empty;
	}
#endif

	/** 加载材质资产，支持短路径和 ObjectPath 两种格式。 */
	static UObject* LoadMaterialAsset(const FString& AssetPath)
	{
		UObject* Obj = LoadObject<UObject>(nullptr, *AssetPath);
		if (!Obj)
		{
			const FString FullPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
			Obj = LoadObject<UObject>(nullptr, *FullPath);
		}
		return Obj;
	}

	/** 为材质表达式生成稳定的 nodeId（使用 UObject::GetName()）。 */
	static FString GetExpressionNodeId(UMaterialExpression* Expr)
	{
		return Expr ? Expr->GetName() : TEXT("");
	}

	/** 在表达式列表中按 nodeId 查找表达式。 */
	static UMaterialExpression* FindExpressionByNodeId(UMaterial* Mat, const FString& NodeId)
	{
		for (UMaterialExpression* Expr : GetExpressions(Mat))
		{
			if (Expr && Expr->GetName() == NodeId)
			{
				return Expr;
			}
		}
		return nullptr;
	}

	/** 在 MaterialFunction 表达式列表中按 nodeId 查找表达式。 */
	static UMaterialExpression* FindExpressionByNodeId(UMaterialFunction* MF, const FString& NodeId)
	{
		for (UMaterialExpression* Expr : GetExpressions(MF))
		{
			if (Expr && Expr->GetName() == NodeId)
			{
				return Expr;
			}
		}
		return nullptr;
	}
};
