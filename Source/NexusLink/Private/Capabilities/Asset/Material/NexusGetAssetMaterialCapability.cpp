// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusGetAssetMaterialCapability.h"

#if WITH_EDITOR

#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusMaterialUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"

// ── 共用 expression graph 序列化（静态公共方法）────────────────────────────────

TSharedPtr<FJsonObject> FGetAssetMaterialCapability::BuildMaterialExpressionGraph(
	const TArray<UMaterialExpression*>& Expressions,
	const FString& NodeFilter, bool bIncludePins, bool bIncludeWires,
	int32 Offset, int32 Limit)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

	// 预先建 nodeId 映射，供连线查找
	TMap<UMaterialExpression*, FString> ExprToId;
	for (UMaterialExpression* E : Expressions) if (E) ExprToId.Add(E, FNexusMaterialUtils::GetExpressionNodeId(E));

	// 先过滤出符合条件的表达式列表，再分页——避免序列化不需要的节点
	TArray<UMaterialExpression*> Filtered;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr) continue;
		const FString NodeId    = FNexusMaterialUtils::GetExpressionNodeId(Expr);
		const FString NodeClass = Expr->GetClass()->GetName();
		if (!NodeFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(NodeClass, NodeFilter) && !FNexusStringMatchUtils::Matches(NodeId, NodeFilter)) continue;
		Filtered.Add(Expr);
	}

	const int32 Total = Filtered.Num();
	int32 Start, End;
	FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);

	TArray<TSharedPtr<FJsonValue>> Page;
	for (int32 fi = Start; fi < End; ++fi)
	{
		UMaterialExpression* Expr = Filtered[fi];
		TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetStringField(TEXT("nodeId"),    FNexusMaterialUtils::GetExpressionNodeId(Expr));
		Node->SetStringField(TEXT("nodeClass"), Expr->GetClass()->GetName());
		const FString Desc = Expr->GetDescription();
		if (!Desc.IsEmpty()) Node->SetStringField(TEXT("desc"), Desc);
		Node->SetNumberField(TEXT("posX"), Expr->MaterialExpressionEditorX);
		Node->SetNumberField(TEXT("posY"), Expr->MaterialExpressionEditorY);
		if (auto* PE = Cast<UMaterialExpressionParameter>(Expr)) Node->SetStringField(TEXT("parameterName"), PE->ParameterName.ToString());

		if (bIncludePins || bIncludeWires)
		{
			TArray<TSharedPtr<FJsonValue>> Pins;
			for (int32 i = 0; ; ++i)
			{
				FExpressionInput* Input = Expr->GetInput(i);
				if (!Input) break;
				TSharedPtr<FJsonObject> Pin = MakeShared<FJsonObject>();
				Pin->SetStringField(TEXT("pinName"),   Expr->GetInputName(i).ToString());
				Pin->SetStringField(TEXT("direction"), TEXT("input"));
				if (bIncludeWires && Input->Expression)
				{
					const FString* CID = ExprToId.Find(Input->Expression);
					if (CID) { Pin->SetStringField(TEXT("connectedNodeId"), *CID); Pin->SetNumberField(TEXT("connectedOutputIndex"), Input->OutputIndex); }
				}
				Pins.Add(MakeShared<FJsonValueObject>(Pin));
			}
			const TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
			for (int32 i = 0; i < Outputs.Num(); ++i)
			{
				TSharedPtr<FJsonObject> Pin = MakeShared<FJsonObject>();
				Pin->SetStringField(TEXT("pinName"),   Outputs[i].OutputName.IsNone() ? FString::Printf(TEXT("Output_%d"), i) : Outputs[i].OutputName.ToString());
				Pin->SetStringField(TEXT("direction"), TEXT("output"));
				Pins.Add(MakeShared<FJsonValueObject>(Pin));
			}
			// includePins=false 且 includeWires=true 时仍输出 pins，
			// 否则连线信息（connectedNodeId 等）无处可见。
			Node->SetArrayField(TEXT("pins"), Pins);
		}

		Page.Add(MakeShared<FJsonValueObject>(Node));
	}

	Obj->SetNumberField(TEXT("totalCount"), Total);
	Obj->SetNumberField(TEXT("offset"),     Start);
	Obj->SetNumberField(TEXT("count"),      Page.Num());
	Obj->SetArrayField(TEXT("nodes"),       Page);
	return Obj;
}


struct FMaterialQueryParams
{
	FString NameFilter;
	bool bIncludePins  = true;
	bool bIncludeWires = true;
	int32 Offset = 0;
	int32 Limit  = 100;
};

static FMaterialQueryParams ParseMaterialQueryParams(const TSharedPtr<FJsonObject>& Args)
{
	FMaterialQueryParams P;
	if (!Args.IsValid()) return P;
	if (Args->HasField(TEXT("nameFilter")))    P.NameFilter    = Args->GetStringField(TEXT("nameFilter"));
	if (Args->HasField(TEXT("includePins")))   P.bIncludePins  = Args->GetBoolField(TEXT("includePins"));
	if (Args->HasField(TEXT("includeWires")))  P.bIncludeWires = Args->GetBoolField(TEXT("includeWires"));
	if (Args->HasField(TEXT("offset")))        P.Offset = FMath::Max(0, static_cast<int32>(Args->GetNumberField(TEXT("offset"))));
	if (Args->HasField(TEXT("limit")))         P.Limit  = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("limit"))), 1, 500);
	return P;
}

// ── FNexusCapability 基础钩子 ─────────────────────────────────────────────────

void FGetAssetMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_material");
	Out.Description = TEXT("检查 Mat/MI/MF 节点与参数。sections=overview|params|graph；可过滤分页。");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = {
		TEXT("shader"), TEXT("instance"), TEXT("mf"), TEXT("scalar"), TEXT("texture")
	};
	Out.RelatedCapabilities = { TEXT("manage_asset_material"), TEXT("create_asset_material") };
	Out.WhenToUse = TEXT("读节点图/参数/连线；不含编辑");
}

TSharedPtr<FJsonObject> FGetAssetMaterialCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),     FNexusSchema::Str(TEXT("Material/MI/MaterialFunction 资产路径")))
		.Prop(TEXT("assetPaths"),    FNexusSchema::StrArr(TEXT("多个材质资产路径（批量）")))
		.Prop(TEXT("nameFilter"),    FNexusSchema::Str(TEXT("参数/节点名过滤")))
		.Prop(TEXT("includePins"),   FNexusSchema::Bool(TEXT("包含引脚详情（graph）"), true, true))
		.Prop(TEXT("includeWires"),  FNexusSchema::Bool(TEXT("包含连线信息（graph）"), true, true))
		.Prop(TEXT("offset"),        FNexusSchema::Int(TEXT("分页偏移"), 0, 0))
		.Prop(TEXT("limit"),         FNexusSchema::Int(TEXT("每页最大条数"), 100, 1, 500))
		.Build();
}

// ── multi-section 钩子 ────────────────────────────────────────────────────────

TArray<FString> FGetAssetMaterialCapability::GetSectionNames() const
{
	return { TEXT("overview"), TEXT("params"), TEXT("graph") };
}

TArray<FString> FGetAssetMaterialCapability::GetDefaultSectionNames() const
{
	return { TEXT("overview") };
}

bool FGetAssetMaterialCapability::PrepareEntry(const TSharedPtr<FJsonObject>& Args,
                                               TSharedPtr<FJsonObject>&       OutEntry,
                                               void*&                         OutTargetOpaque,
                                               FString&                       OutError) const
{
	FString Path;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("assetPath"), Path) || Path.IsEmpty())
	{
		OutError = TEXT("缺少 assetPath");
		return false;
	}

	OutEntry->SetStringField(TEXT("assetPath"), Path);

	UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
	if (!Obj)
	{
		OutError = FString::Printf(TEXT("资产未找到: %s"), *Path);
		return false;
	}

	if (!Obj->IsA(UMaterial::StaticClass()) && !Obj->IsA(UMaterialInstanceConstant::StaticClass()) && !Obj->IsA(UMaterialFunction::StaticClass()))
	{
		OutError = FString::Printf(TEXT("资产不是 Material/MaterialInstance/MaterialFunction: %s"), *Path);
		return false;
	}

	// 写公共 locator 字段
	OutEntry->SetStringField(TEXT("name"), Obj->GetName());
	if (Obj->IsA(UMaterial::StaticClass()))                  OutEntry->SetStringField(TEXT("assetType"), TEXT("Material"));
	else if (Obj->IsA(UMaterialInstanceConstant::StaticClass())) OutEntry->SetStringField(TEXT("assetType"), TEXT("MaterialInstance"));
	else                                            OutEntry->SetStringField(TEXT("assetType"), TEXT("MaterialFunction"));

	OutTargetOpaque = static_cast<void*>(Obj);
	return true;
}

void FGetAssetMaterialCapability::ExecuteSection(const FString&                 SectionName,
                                                 const TSharedPtr<FJsonObject>& Args,
                                                 void*                          TargetOpaque,
                                                 TSharedPtr<FJsonObject>&       InOutDetail,
                                                 FString&                       OutError) const
{
	UObject* Obj = static_cast<UObject*>(TargetOpaque);
	if (!Obj) { OutError = TEXT("无效的材质目标"); return; }

	UMaterial*                 Mat = Cast<UMaterial>(Obj);
	UMaterialInstanceConstant* MI  = Cast<UMaterialInstanceConstant>(Obj);
	UMaterialFunction*         MF  = Cast<UMaterialFunction>(Obj);

	FMaterialQueryParams Q = ParseMaterialQueryParams(Args);

	if (SectionName == TEXT("overview"))
	{
		if (MF)
		{
			InOutDetail->SetNumberField(TEXT("expressionCount"), FNexusMaterialUtils::GetExpressions(MF).Num());
			if (!MF->Description.IsEmpty()) InOutDetail->SetStringField(TEXT("description"), MF->Description);
		}
		else if (Mat)
		{
			InOutDetail->SetStringField(TEXT("materialDomain"), FNexusMaterialUtils::DomainToString(Mat->MaterialDomain));
			InOutDetail->SetStringField(TEXT("blendMode"),      FNexusMaterialUtils::BlendModeToString(Mat->BlendMode));
			if (Mat->IsTwoSided()) InOutDetail->SetBoolField(TEXT("twoSided"), true);
			auto& Exprs = FNexusMaterialUtils::GetExpressions(Mat);
			InOutDetail->SetNumberField(TEXT("expressionCount"), Exprs.Num());
			int32 PC = 0;
			for (UMaterialExpression* E : Exprs) if (E && E->IsA(UMaterialExpressionParameter::StaticClass())) ++PC;
			InOutDetail->SetNumberField(TEXT("parameterCount"), PC);
		}
		else if (MI)
		{
			InOutDetail->SetStringField(TEXT("parentMaterial"),
			(MI->Parent && MI->Parent->GetOutermost()) ? MI->Parent->GetOutermost()->GetName() : TEXT("None"));
			InOutDetail->SetNumberField(TEXT("scalarParameterCount"),  MI->ScalarParameterValues.Num());
			InOutDetail->SetNumberField(TEXT("vectorParameterCount"),  MI->VectorParameterValues.Num());
			InOutDetail->SetNumberField(TEXT("textureParameterCount"), MI->TextureParameterValues.Num());
		}
	}
	else if (SectionName == TEXT("params"))
	{
		if (MF)
		{
			// MaterialFunction 没有参数
			InOutDetail->SetNumberField(TEXT("totalCount"), 0);
			InOutDetail->SetArrayField(TEXT("parameters"), TArray<TSharedPtr<FJsonValue>>());
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Params;
		if (Mat)
		{
			auto& Exprs = FNexusMaterialUtils::GetExpressions(Mat);
			for (UMaterialExpression* Expr : Exprs)
			{
				if (!Expr) continue;
				UMaterialExpressionParameter* PE = Cast<UMaterialExpressionParameter>(Expr);
				if (!PE) continue;
				const FString PN = PE->ParameterName.ToString();
				if (!Q.NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(PN, Q.NameFilter)) continue;
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("nodeId"),         FNexusMaterialUtils::GetExpressionNodeId(Expr));
				P->SetStringField(TEXT("parameterName"),  PN);
				if (auto* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
				{
					P->SetStringField(TEXT("paramType"),    TEXT("scalar"));
					P->SetNumberField(TEXT("defaultValue"), Scalar->DefaultValue);
				}
				else if (auto* Vec = Cast<UMaterialExpressionVectorParameter>(Expr))
				{
					P->SetStringField(TEXT("paramType"),    TEXT("vector"));
					P->SetStringField(TEXT("defaultValue"), FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f"),
						Vec->DefaultValue.R, Vec->DefaultValue.G, Vec->DefaultValue.B, Vec->DefaultValue.A));
				}
				else if (auto* Tex = Cast<UMaterialExpressionTextureSampleParameter>(Expr))
				{
					P->SetStringField(TEXT("paramType"),    TEXT("texture"));
					P->SetStringField(TEXT("defaultValue"), Tex->Texture ? Tex->Texture->GetPathName() : TEXT("None"));
				}
				else
				{
					P->SetStringField(TEXT("paramType"), TEXT("other"));
				}
				Params.Add(MakeShared<FJsonValueObject>(P));
			}
		}
		else if (MI)
		{
			auto AddParam = [&](const FString& Name, const FString& T, const FString& Val)
			{
				if (!Q.NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(Name, Q.NameFilter)) return;
				TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
				J->SetStringField(TEXT("paramName"), Name);
				J->SetStringField(TEXT("paramType"), T);
				J->SetStringField(TEXT("value"),     Val);
				Params.Add(MakeShared<FJsonValueObject>(J));
			};
			for (const auto& P : MI->ScalarParameterValues)  AddParam(P.ParameterInfo.Name.ToString(), TEXT("scalar"), FString::SanitizeFloat(P.ParameterValue));
			for (const auto& P : MI->VectorParameterValues)  AddParam(P.ParameterInfo.Name.ToString(), TEXT("vector"), FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f"), P.ParameterValue.R, P.ParameterValue.G, P.ParameterValue.B, P.ParameterValue.A));
			for (const auto& P : MI->TextureParameterValues) AddParam(P.ParameterInfo.Name.ToString(), TEXT("texture"), P.ParameterValue ? P.ParameterValue->GetPathName() : TEXT("None"));
		}

		const int32 Total = Params.Num();
		int32 Start, End; FNexusJsonUtils::ComputeSlice(Total, Q.Offset, Q.Limit, Start, End);
		TArray<TSharedPtr<FJsonValue>> Page;
		for (int32 i = Start; i < End; ++i) Page.Add(Params[i]);
		InOutDetail->SetNumberField(TEXT("totalCount"), Total);
		InOutDetail->SetNumberField(TEXT("offset"),     Start);
		InOutDetail->SetArrayField(TEXT("parameters"),  Page);
	}
	else if (SectionName == TEXT("graph"))
	{
		if (MI)
		{
			OutError = TEXT("section=graph 仅适用于 Material/MaterialFunction，不适用 MaterialInstance");
			return;
		}

		TArray<UMaterialExpression*> EA;
		if (Mat)
		{
			for (UMaterialExpression* E : FNexusMaterialUtils::GetExpressions(Mat)) EA.Add(E);
		}
		else if (MF)
		{
			for (UMaterialExpression* E : FNexusMaterialUtils::GetExpressions(MF)) EA.Add(E);
		}

		TSharedPtr<FJsonObject> GraphResult = BuildMaterialExpressionGraph(EA, Q.NameFilter, Q.bIncludePins, Q.bIncludeWires, Q.Offset, Q.Limit);
		for (const auto& Pair : GraphResult->Values)
		{
			InOutDetail->SetField(Pair.Key, Pair.Value);
		}
	}
	else
	{
		OutError = FString::Printf(TEXT("未处理的 section '%s'"), *SectionName);
	}
}

TArray<TSharedPtr<FJsonObject>> FGetAssetMaterialCapability::ExpandPerEntry(
	const TSharedPtr<FJsonObject>& Args) const
{
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (!Args.IsValid() || !Args->TryGetArrayField(TEXT("assetPaths"), PathsArr) || !PathsArr || PathsArr->Num() == 0)
	{
		return {};
	}

	TArray<TSharedPtr<FJsonObject>> Result;
	for (const TSharedPtr<FJsonValue>& V : *PathsArr)
	{
		FString Path;
		if (!V.IsValid() || !V->TryGetString(Path) || Path.IsEmpty())
		{
			// 无效/空项：插入一个带 error 的占位 entry，让基类生成对应 error entry
			TSharedPtr<FJsonObject> ErrArgs = MakeShared<FJsonObject>();
			ErrArgs->SetStringField(TEXT("assetPath"), TEXT(""));
			Result.Add(ErrArgs);
			continue;
		}

		TSharedPtr<FJsonObject> EntryArgs = MakeShared<FJsonObject>();
		for (const auto& Pair : Args->Values)
		{
			if (Pair.Key != TEXT("assetPaths")) { EntryArgs->SetField(Pair.Key, Pair.Value); }
		}
		EntryArgs->SetStringField(TEXT("assetPath"), Path);
		Result.Add(EntryArgs);
	}
	return Result;
}

REGISTER_MCP_CAPABILITY(FGetAssetMaterialCapability)

#endif // WITH_EDITOR
