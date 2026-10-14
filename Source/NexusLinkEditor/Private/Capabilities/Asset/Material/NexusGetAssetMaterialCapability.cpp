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

TSharedPtr<FJsonObject> FNexusGetAssetMaterialCapability::BuildMaterialExpressionGraph(
	const TArray<UMaterialExpression*>& Expressions,
	const FString& NodeFilter, bool bIncludePins, bool bIncludeWires,
	int32 Offset, int32 Limit)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

	// 预先建 NodeId 映射，供连线查找
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
	for (int32 ExprIndex = Start; ExprIndex < End; ++ExprIndex)
	{
		UMaterialExpression* Expr = Filtered[ExprIndex];
		TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetStringField(TEXT("NodeId"),    FNexusMaterialUtils::GetExpressionNodeId(Expr));
		Node->SetStringField(TEXT("NodeClass"), Expr->GetClass()->GetName());
		const FString Desc = Expr->GetDescription();
		if (!Desc.IsEmpty()) Node->SetStringField(TEXT("Desc"), Desc);
		Node->SetNumberField(TEXT("PosX"), Expr->MaterialExpressionEditorX);
		Node->SetNumberField(TEXT("PosY"), Expr->MaterialExpressionEditorY);
		if (auto* PE = Cast<UMaterialExpressionParameter>(Expr)) Node->SetStringField(TEXT("ParameterName"), PE->ParameterName.ToString());

		if (bIncludePins || bIncludeWires)
		{
			TArray<TSharedPtr<FJsonValue>> Pins;
			for (int32 i = 0; ; ++i)
			{
				FExpressionInput* Input = Expr->GetInput(i);
				if (!Input) break;
				TSharedPtr<FJsonObject> Pin = MakeShared<FJsonObject>();
				Pin->SetStringField(TEXT("PinName"),   Expr->GetInputName(i).ToString());
				Pin->SetStringField(TEXT("Direction"), TEXT("input"));
				if (bIncludeWires && Input->Expression)
				{
					const FString* CID = ExprToId.Find(Input->Expression);
					if (CID) { Pin->SetStringField(TEXT("ConnectedNodeId"), *CID); Pin->SetNumberField(TEXT("ConnectedOutputIndex"), Input->OutputIndex); }
				}
				Pins.Add(MakeShared<FJsonValueObject>(Pin));
			}
			const TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
			for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
			{
				TSharedPtr<FJsonObject> Pin = MakeShared<FJsonObject>();
				Pin->SetStringField(TEXT("PinName"),   Outputs[OutputIndex].OutputName.IsNone() ? FString::Printf(TEXT("Output_%d"), OutputIndex) : Outputs[OutputIndex].OutputName.ToString());
				Pin->SetStringField(TEXT("Direction"), TEXT("Output"));
				Pins.Add(MakeShared<FJsonValueObject>(Pin));
			}
			// bIncludePins=false 且 bIncludeWires=true 时仍输出 pins，
			// 否则连线信息（ConnectedNodeId 等）无处可见。
			Node->SetArrayField(TEXT("Pins"), Pins);
		}

		Page.Add(MakeShared<FJsonValueObject>(Node));
	}

	Obj->SetNumberField(TEXT("TotalCount"), Total);
	Obj->SetNumberField(TEXT("Offset"),     Start);
	Obj->SetNumberField(TEXT("Count"),      Page.Num());
	Obj->SetArrayField(TEXT("Nodes"),       Page);
	return Obj;
}


struct FNexusMaterialQueryParams
{
	FString NameFilter;
	bool bIncludePins  = true;
	bool bIncludeWires = true;
	int32 Offset = 0;
	int32 Limit  = 100;
};

static FNexusMaterialQueryParams ParseMaterialQueryParams(const TSharedPtr<FJsonObject>& Args)
{
	FNexusMaterialQueryParams P;
	if (!Args.IsValid()) return P;
	if (Args->HasField(TEXT("NameFilter")))    P.NameFilter    = Args->GetStringField(TEXT("NameFilter"));
	if (Args->HasField(TEXT("bIncludePins")))   P.bIncludePins  = Args->GetBoolField(TEXT("bIncludePins"));
	if (Args->HasField(TEXT("bIncludeWires")))  P.bIncludeWires = Args->GetBoolField(TEXT("bIncludeWires"));
	if (Args->HasField(TEXT("Offset")))        P.Offset = FMath::Max(0, static_cast<int32>(Args->GetNumberField(TEXT("Offset"))));
	if (Args->HasField(TEXT("Limit")))         P.Limit  = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("Limit"))), 1, 500);
	return P;
}

// ── FNexusCapability 基础钩子 ─────────────────────────────────────────────────

void FNexusGetAssetMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetMaterial");
	Out.SearchAssetTypes = {TEXT("Material"), TEXT("MaterialInstance"), TEXT("MaterialFunction")};
	Out.Description = TEXT("Inspect Mat/MI/MF nodes and params. Sections=overview|params|graph.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = {
		TEXT("shader"), TEXT("instance"), TEXT("mf"), TEXT("Scalar"), TEXT("Texture")
	};
	Out.RelatedCapabilities = { TEXT("ManageAssetMaterial"), TEXT("CreateAssetMaterial"), TEXT("CreateAssetMaterialFunction") };
	Out.WhenToUse = TEXT("Read node graph/params/wires; no edits");
}

TSharedPtr<FJsonObject> FNexusGetAssetMaterialCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),     FNexusSchema::Str(TEXT("Material/MI/MaterialFunction asset path")))
		.Prop(TEXT("NameFilter"),    FNexusSchema::Str(TEXT("Parameter/node name filter")))
		.Prop(TEXT("bIncludePins"),   FNexusSchema::Bool(TEXT("Include pin details (graph)"), true, true))
		.Prop(TEXT("bIncludeWires"),  FNexusSchema::Bool(TEXT("Include connection info (graph)"), true, true))
		.Prop(TEXT("Offset"),        FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
		.Prop(TEXT("Limit"),         FNexusSchema::Int(TEXT("Max items per page"), 100, 1, 500))
		.Build();
}

// ── multi-section 钩子 ────────────────────────────────────────────────────────

TArray<FString> FNexusGetAssetMaterialCapability::GetSectionNames() const
{
	return { TEXT("overview"), TEXT("Params"), TEXT("graph") };
}

TArray<FString> FNexusGetAssetMaterialCapability::GetDefaultSectionNames() const
{
	return { TEXT("overview") };
}

bool FNexusGetAssetMaterialCapability::PrepareEntry(const TSharedPtr<FJsonObject>& Args,
                                               TSharedPtr<FJsonObject>&       OutEntry,
                                               void*&                         OutTargetOpaque,
                                               FString&                       OutError) const
{
	FString Path;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("AssetPath"), Path) || Path.IsEmpty())
	{
		OutError = TEXT("Missing AssetPath");
		return false;
	}

	OutEntry->SetStringField(TEXT("Path"), Path);

	UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
	if (!Obj)
	{
		OutError = FString::Printf(TEXT("Asset not found: %s"), *Path);
		return false;
	}

	if (!Obj->IsA(UMaterial::StaticClass()) && !Obj->IsA(UMaterialInstanceConstant::StaticClass()) && !Obj->IsA(UMaterialFunction::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Asset is not Material/MaterialInstance/MaterialFunction: %s"), *Path);
		return false;
	}

	// 写公共 locator 字段
	OutEntry->SetStringField(TEXT("name"), Obj->GetName());
	if (Obj->IsA(UMaterial::StaticClass()))                  OutEntry->SetStringField(TEXT("AssetType"), TEXT("Material"));
	else if (Obj->IsA(UMaterialInstanceConstant::StaticClass())) OutEntry->SetStringField(TEXT("AssetType"), TEXT("MaterialInstance"));
	else                                            OutEntry->SetStringField(TEXT("AssetType"), TEXT("MaterialFunction"));

	OutTargetOpaque = static_cast<void*>(Obj);
	return true;
}

void FNexusGetAssetMaterialCapability::ExecuteSection(const FString&                 SectionName,
                                                 const TSharedPtr<FJsonObject>& Args,
                                                 void*                          TargetOpaque,
                                                 TSharedPtr<FJsonObject>&       InOutDetail,
                                                 FString&                       OutError) const
{
	UObject* Obj = static_cast<UObject*>(TargetOpaque);
	if (!Obj) { OutError = TEXT("Invalid material target"); return; }

	UMaterial*                 Mat = Cast<UMaterial>(Obj);
	UMaterialInstanceConstant* MI  = Cast<UMaterialInstanceConstant>(Obj);
	UMaterialFunction*         MF  = Cast<UMaterialFunction>(Obj);

	FNexusMaterialQueryParams Q = ParseMaterialQueryParams(Args);

	if (SectionName == TEXT("overview"))
	{
		if (MF)
		{
			InOutDetail->SetNumberField(TEXT("ExpressionCount"), FNexusMaterialUtils::GetExpressions(MF).Num());
			if (!MF->Description.IsEmpty()) InOutDetail->SetStringField(TEXT("description"), MF->Description);
		}
		else if (Mat)
		{
			InOutDetail->SetStringField(TEXT("MaterialDomain"), FNexusMaterialUtils::DomainToString(Mat->MaterialDomain));
			InOutDetail->SetStringField(TEXT("BlendMode"),      FNexusMaterialUtils::BlendModeToString(Mat->BlendMode));
			if (Mat->IsTwoSided()) InOutDetail->SetBoolField(TEXT("bTwoSided"), true);
			auto& Exprs = FNexusMaterialUtils::GetExpressions(Mat);
			InOutDetail->SetNumberField(TEXT("ExpressionCount"), Exprs.Num());
			int32 PC = 0;
			for (UMaterialExpression* E : Exprs) if (E && E->IsA(UMaterialExpressionParameter::StaticClass())) ++PC;
			InOutDetail->SetNumberField(TEXT("ParameterCount"), PC);
		}
		else if (MI)
		{
			InOutDetail->SetStringField(TEXT("ParentMaterial"),
			(MI->Parent && MI->Parent->GetOutermost()) ? MI->Parent->GetOutermost()->GetName() : TEXT("None"));
			InOutDetail->SetNumberField(TEXT("ScalarParameterCount"),  MI->ScalarParameterValues.Num());
			InOutDetail->SetNumberField(TEXT("VectorParameterCount"),  MI->VectorParameterValues.Num());
			InOutDetail->SetNumberField(TEXT("TextureParameterCount"), MI->TextureParameterValues.Num());
		}
	}
	else if (SectionName == TEXT("Params"))
	{
		if (MF)
		{
			// MaterialFunction 没有参数
			InOutDetail->SetNumberField(TEXT("TotalCount"), 0);
			InOutDetail->SetArrayField(TEXT("Parameters"), TArray<TSharedPtr<FJsonValue>>());
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
				P->SetStringField(TEXT("NodeId"),         FNexusMaterialUtils::GetExpressionNodeId(Expr));
				P->SetStringField(TEXT("ParameterName"),  PN);
				if (auto* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
				{
					P->SetStringField(TEXT("ParamType"),    TEXT("Scalar"));
					P->SetNumberField(TEXT("DefaultValue"), Scalar->DefaultValue);
				}
				else if (auto* Vec = Cast<UMaterialExpressionVectorParameter>(Expr))
				{
					P->SetStringField(TEXT("ParamType"),    TEXT("Vector"));
					P->SetStringField(TEXT("DefaultValue"), FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f"),
						Vec->DefaultValue.R, Vec->DefaultValue.G, Vec->DefaultValue.B, Vec->DefaultValue.A));
				}
				else if (auto* Tex = Cast<UMaterialExpressionTextureSampleParameter>(Expr))
				{
					P->SetStringField(TEXT("ParamType"),    TEXT("Texture"));
					P->SetStringField(TEXT("DefaultValue"), Tex->Texture ? Tex->Texture->GetPathName() : TEXT("None"));
				}
				else
				{
					P->SetStringField(TEXT("ParamType"), TEXT("Other"));
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
				J->SetStringField(TEXT("ParamName"), Name);
				J->SetStringField(TEXT("ParamType"), T);
				J->SetStringField(TEXT("Value"),     Val);
				Params.Add(MakeShared<FJsonValueObject>(J));
			};
			for (const auto& P : MI->ScalarParameterValues)  AddParam(P.ParameterInfo.Name.ToString(), TEXT("Scalar"), FString::SanitizeFloat(P.ParameterValue));
			for (const auto& P : MI->VectorParameterValues)  AddParam(P.ParameterInfo.Name.ToString(), TEXT("Vector"), FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f"), P.ParameterValue.R, P.ParameterValue.G, P.ParameterValue.B, P.ParameterValue.A));
			for (const auto& P : MI->TextureParameterValues) AddParam(P.ParameterInfo.Name.ToString(), TEXT("Texture"), P.ParameterValue ? P.ParameterValue->GetPathName() : TEXT("None"));
		}

		const int32 Total = Params.Num();
		int32 Start, End; FNexusJsonUtils::ComputeSlice(Total, Q.Offset, Q.Limit, Start, End);
		TArray<TSharedPtr<FJsonValue>> Page;
		for (int32 ParamIndex = Start; ParamIndex < End; ++ParamIndex) Page.Add(Params[ParamIndex]);
		InOutDetail->SetNumberField(TEXT("TotalCount"), Total);
		InOutDetail->SetNumberField(TEXT("Offset"),     Start);
		InOutDetail->SetArrayField(TEXT("Parameters"),  Page);
	}
	else if (SectionName == TEXT("graph"))
	{
		if (MI)
		{
			OutError = TEXT("Section=graph only for Material/MaterialFunction, not MaterialInstance");
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
		OutError = FString::Printf(TEXT("Unhandled section '%s'"), *SectionName);
	}
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetMaterialCapability)

#endif // WITH_EDITOR
