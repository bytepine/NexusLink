// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Blueprint/NexusGetAssetBlueprintCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusBlueprintGraphUtils.h"
#include "Utils/NexusPropertyReportUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/SceneComponent.h"

#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#endif
#include "Misc/Paths.h"

struct FBPQueryParams
{
	FString Section;
	FString NameFilter;
	FString GraphName;
	FString GraphType;
	TArray<FString> PropertyPaths;
	int32 Offset = 0;
	int32 Limit  = 100;
};

static FBPQueryParams ParseBPQueryParams(const TSharedPtr<FJsonObject>& Args)
{
	FBPQueryParams P;
	if (!Args.IsValid()) return P;
	P.Section     = FNexusJsonUtils::GetStringSafe(Args, TEXT("section")).ToLower();
	P.NameFilter  = FNexusJsonUtils::GetStringSafe(Args, TEXT("nameFilter"));
	P.GraphName   = FNexusJsonUtils::GetStringSafe(Args, TEXT("graphName"));
	P.GraphType   = FNexusJsonUtils::GetStringSafe(Args, TEXT("graphType")).ToLower();
	FNexusJsonUtils::ParseOffsetLimit(Args, P.Offset, P.Limit);
	P.PropertyPaths = FNexusJsonUtils::GetStringArray(Args, TEXT("propertyPaths"));
	return P;
}


#if WITH_EDITOR
static TSharedPtr<FJsonObject> HandleBPGraph(UBlueprint* BP, const FBPQueryParams& Q)
{
	TArray<UEdGraph*> AllGraphs;
	FNexusBlueprintGraphUtils::CollectAllGraphs(BP, AllGraphs);

	const FString GType = Q.GraphType.IsEmpty() ? TEXT("all") : Q.GraphType;
	if (GType != TEXT("all"))
		AllGraphs = AllGraphs.FilterByPredicate([&](const UEdGraph* G){ return FNexusBlueprintGraphUtils::GetBPGraphType(G) == GType; });

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("assetType"), TEXT("Blueprint"));
	Root->SetStringField(TEXT("name"), BP->GetName());

	if (Q.GraphName.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> List;
		for (const UEdGraph* G : AllGraphs) List.Add(MakeShared<FJsonValueObject>(FNexusBlueprintGraphUtils::BuildBPGraphSummary(G)));
		Root->SetArrayField(TEXT("graphs"), List);
		return Root;
	}

	UEdGraph* Target = nullptr;
	for (UEdGraph* G : AllGraphs) { if (G->GetName() == Q.GraphName) { Target = G; break; } }
	if (!Target)
	{
		TArray<FString> Avail;
		for (const UEdGraph* G : AllGraphs) Avail.Add(G->GetName());
		Root->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph '%s' 未找到。可用: %s"), *Q.GraphName, *FString::Join(Avail, TEXT(", "))));
		return Root;
	}

	Root->SetStringField(TEXT("graphName"), Target->GetName());
	Root->SetStringField(TEXT("graphType"), FNexusBlueprintGraphUtils::GetBPGraphType(Target));
	const FString ParentName = FNexusBlueprintGraphUtils::GetBPParentGraphName(Target);
	if (!ParentName.IsEmpty()) Root->SetStringField(TEXT("parentGraph"), ParentName);

	TArray<UEdGraphNode*> Filtered;
	for (UEdGraphNode* Node : Target->Nodes)
	{
		if (!Node) continue;
		if (!Q.NameFilter.IsEmpty())
		{
			const FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if (!FNexusStringMatchUtils::Matches(Title, Q.NameFilter) && !FNexusStringMatchUtils::Matches(Node->GetClass()->GetName(), Q.NameFilter)) continue;
		}
		Filtered.Add(Node);
	}

	const int32 Total = Filtered.Num(); int32 Start, End; FNexusJsonUtils::ComputeSlice(Total, Q.Offset, Q.Limit, Start, End);
	Root->SetNumberField(TEXT("totalNodeCount"), Total);
	Root->SetNumberField(TEXT("offset"), Start);
	Root->SetNumberField(TEXT("limit"),  Q.Limit);
	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (int32 i = Start; i < End; ++i)
		NodesArr.Add(MakeShared<FJsonValueObject>(FNexusBlueprintGraphUtils::SerializeBPNode(Filtered[i])));
	Root->SetArrayField(TEXT("nodes"), NodesArr);
	return Root;
}
#endif // WITH_EDITOR

static TSharedPtr<FJsonObject> HandleBPDefaults(UBlueprint* BP, const FBPQueryParams& Q)
{
	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
	Info->SetStringField(TEXT("assetType"), TEXT("Blueprint"));
	Info->SetStringField(TEXT("name"), BP->GetName());
	if (BP->ParentClass) Info->SetStringField(TEXT("parentClass"), BP->ParentClass->GetName());

	UObject* CDO = BP->GeneratedClass ? BP->GeneratedClass->GetDefaultObject(false) : nullptr;
	if (!CDO) { Info->SetStringField(TEXT("error"), TEXT("Blueprint has no compiled CDO; compile first")); return Info; }

	// BP 自有变量集合，用于 inherited 标记的反向判断
	TSet<FString> BPVarNames;
#if WITH_EDITOR
	for (const FBPVariableDescription& Var : BP->NewVariables) BPVarNames.Add(Var.VarName.ToString());
#endif

	// 委托给通用工具；inherited 标记由 LeafClass=GeneratedClass 驱动
	int32 Total = 0;
	TArray<TSharedPtr<FJsonValue>> Page = FNexusPropertyReportUtils::BuildEditablePropsPage(
		BP->GeneratedClass, CDO, BP->GeneratedClass,
		Q.NameFilter, Q.PropertyPaths, Q.Offset, Q.Limit, Total);

	// 将 name 字段重命名为 path（BP defaults 的惯例），并修正 inherited 逻辑
	// BuildEditablePropsPage 用 OwnerClass != LeafClass 判断继承，而 BPDefaults 要用 BP.NewVariables 白名单
	for (TSharedPtr<FJsonValue>& V : Page)
	{
		TSharedPtr<FJsonObject> Entry = V->AsObject();
		if (!Entry.IsValid()) continue;
		FString PropName;
		Entry->TryGetStringField(TEXT("name"), PropName);
		Entry->SetStringField(TEXT("path"), PropName);
		Entry->RemoveField(TEXT("name"));
		// 用 NewVariables 白名单覆盖 inherited 标记
		Entry->RemoveField(TEXT("inherited"));
		if (!BPVarNames.Contains(PropName)) Entry->SetBoolField(TEXT("inherited"), true);
	}

	const int32 Start = FMath::Min(Q.Offset, Total);
	Info->SetNumberField(TEXT("totalCount"), Total);
	Info->SetNumberField(TEXT("offset"), Start);
	Info->SetNumberField(TEXT("limit"),  Q.Limit);
	Info->SetArrayField(TEXT("defaults"), Page);
	return Info;
}

// 递归序列化单个 SCS 节点为含层级的 JSON 对象
static TSharedPtr<FJsonObject> SerializeSCSNode(USCS_Node* Node)
{
	if (!Node) return nullptr;
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("variableName"), Node->GetVariableName().ToString());
	if (Node->ComponentTemplate)
	{
		Obj->SetStringField(TEXT("componentClass"), Node->ComponentTemplate->GetClass()->GetName());
		Obj->SetBoolField(TEXT("isSceneComponent"), Node->ComponentTemplate->IsA(USceneComponent::StaticClass()));
	}
	if (!Node->ParentComponentOrVariableName.IsNone())
		Obj->SetStringField(TEXT("attachParent"), Node->ParentComponentOrVariableName.ToString());
	if (Node->ChildNodes.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Children;
		for (USCS_Node* Child : Node->ChildNodes)
		{
			TSharedPtr<FJsonObject> ChildObj = SerializeSCSNode(Child);
			if (ChildObj.IsValid()) Children.Add(MakeShared<FJsonValueObject>(ChildObj));
		}
		Obj->SetArrayField(TEXT("children"), Children);
	}
	return Obj;
}

static TSharedPtr<FJsonObject> HandleBPComponents(UBlueprint* BP, const FBPQueryParams& Q)
{
	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();

	USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
	if (!SCS)
	{
		// 非 Actor 蓝图无 SCS
		Info->SetStringField(TEXT("note"), TEXT("Blueprint has no SimpleConstructionScript (not an Actor Blueprint)"));
		Info->SetArrayField(TEXT("components"), TArray<TSharedPtr<FJsonValue>>());
		return Info;
	}

	// 全量节点列表，用于过滤和分页
	const TArray<USCS_Node*> AllNodes = SCS->GetAllNodes();
	TArray<USCS_Node*> Filtered;
	for (USCS_Node* Node : AllNodes)
	{
		if (!Node) continue;
		if (!Q.NameFilter.IsEmpty())
		{
			const FString VarName   = Node->GetVariableName().ToString();
			const FString ClassName = Node->ComponentTemplate ? Node->ComponentTemplate->GetClass()->GetName() : TEXT("");
			if (!FNexusStringMatchUtils::Matches(VarName, Q.NameFilter) &&
				!FNexusStringMatchUtils::Matches(ClassName, Q.NameFilter)) continue;
		}
		Filtered.Add(Node);
	}

	const int32 Total = Filtered.Num();
	int32 Start, End;
	FNexusJsonUtils::ComputeSlice(Total, Q.Offset, Q.Limit, Start, End);
	Info->SetNumberField(TEXT("totalCount"), Total);
	Info->SetNumberField(TEXT("offset"), Start);
	Info->SetNumberField(TEXT("limit"),  Q.Limit);

	// 扁平列表（支持 nameFilter + 分页）
	TArray<TSharedPtr<FJsonValue>> FlatList;
	for (int32 i = Start; i < End; ++i)
	{
		USCS_Node* Node = Filtered[i];
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("variableName"), Node->GetVariableName().ToString());
		if (Node->ComponentTemplate)
		{
			Obj->SetStringField(TEXT("componentClass"), Node->ComponentTemplate->GetClass()->GetName());
			Obj->SetBoolField(TEXT("isSceneComponent"), Node->ComponentTemplate->IsA(USceneComponent::StaticClass()));
		}
		if (!Node->ParentComponentOrVariableName.IsNone())
			Obj->SetStringField(TEXT("attachParent"), Node->ParentComponentOrVariableName.ToString());
		FlatList.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Info->SetArrayField(TEXT("components"), FlatList);

	// 层级树（始终输出，从 SCS 根节点递归构建）
	TArray<TSharedPtr<FJsonValue>> Tree;
	for (USCS_Node* Root : SCS->GetRootNodes())
	{
		TSharedPtr<FJsonObject> NodeObj = SerializeSCSNode(Root);
		if (NodeObj.IsValid()) Tree.Add(MakeShared<FJsonValueObject>(NodeObj));
	}
	Info->SetArrayField(TEXT("hierarchy"), Tree);

	return Info;
}

struct FBPEntryLocal { FString Name; FString Kind; FString Type; FString SubType; FString SubTypeObject; FString Category; bool bIsPublic = false; };

// ── FNexusCapability 基础钩子 ──────────────────────────────────────────────────

void FGetAssetBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_blueprint");
	Out.Description = TEXT("从编辑器读 BP。回答蓝图问题前必须先调；禁止从源码推断。");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = {
		TEXT("blueprint"), TEXT("variable"), TEXT("function"), TEXT("graph"), TEXT("read")
	};
	Out.RelatedCapabilities = { TEXT("manage_asset_blueprint"), TEXT("create_asset_blueprint") };
	Out.WhenToUse = TEXT("用户问蓝图变量/Graph/函数 — 必须先调，勿 grep 源码");
}

TSharedPtr<FJsonObject> FGetAssetBlueprintCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("蓝图资产路径")))
		.Prop(TEXT("nameFilter"),     FNexusSchema::Str(TEXT("项名称过滤（/regex/ ^前缀 后缀$）")))
		.Prop(TEXT("propertyPaths"), FNexusSchema::StrArr(TEXT("defaults 段精确属性名过滤，如 [\"bUseBuffClass\",\"Scale\"]")))
		.Prop(TEXT("graphName"),     FNexusSchema::Str(TEXT("图名（仅 graph 段）")))
		.Prop(TEXT("graphType"),  FNexusSchema::Enum(TEXT("图类型过滤"),
			{ TEXT("event"), TEXT("function"), TEXT("macro"), TEXT("animgraph"),
			  TEXT("statemachine"), TEXT("state"), TEXT("transition"), TEXT("conduit"), TEXT("all") }))
		.Prop(TEXT("offset"),     FNexusSchema::Int(TEXT("分页偏移"), 0, 0))
		.Prop(TEXT("limit"),      FNexusSchema::Int(TEXT("每页最大条数"), 100, 1, 500))
		.Required({ TEXT("assetPath") })
		.Build();
}

// ── multi-section 钩子 ─────────────────────────────────────────────────────────

TArray<FString> FGetAssetBlueprintCapability::GetSectionNames() const
{
	return { TEXT("variable"), TEXT("function"), TEXT("component"), TEXT("graph"), TEXT("graphOverview"), TEXT("defaults") };
}

TArray<FString> FGetAssetBlueprintCapability::GetDefaultSectionNames() const
{
	// 旧版未传 section 时的行为：返回 variable + function
	return { TEXT("variable"), TEXT("function") };
}

bool FGetAssetBlueprintCapability::PrepareEntry(const TSharedPtr<FJsonObject>& Args,
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

	UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
	if (!Obj)
	{
		OutError = FString::Printf(TEXT("资产未找到: %s"), *Path);
		return false;
	}

	UBlueprint* BP = Cast<UBlueprint>(Obj);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("资产不是 Blueprint: %s"), *Path);
		return false;
	}

	OutEntry->SetStringField(TEXT("name"), BP->GetName());
	if (BP->ParentClass) { OutEntry->SetStringField(TEXT("parentClass"), BP->ParentClass->GetName()); }

	OutTargetOpaque = static_cast<void*>(BP);
	return true;
}

void FGetAssetBlueprintCapability::ExecuteSection(const FString&                 SectionName,
                                                  const TSharedPtr<FJsonObject>& Args,
                                                  void*                          TargetOpaque,
                                                  TSharedPtr<FJsonObject>&       InOutDetail,
                                                  FString&                       OutError) const
{
	UBlueprint* BP = static_cast<UBlueprint*>(TargetOpaque);
	if (!BP)
	{
		OutError = TEXT("无效的 Blueprint 目标");
		return;
	}

	FBPQueryParams Q = ParseBPQueryParams(Args);

	if (SectionName == TEXT("graphOverview"))
	{
#if WITH_EDITOR
		// 只返回所有 Graph 的摘要列表，不展开节点，适合 section=all 场景
		FBPQueryParams OvQ; OvQ.GraphType = Q.GraphType;
		TSharedPtr<FJsonObject> GraphResult = HandleBPGraph(BP, OvQ);
		for (const auto& Pair : GraphResult->Values)
			if (Pair.Key != TEXT("assetType") && Pair.Key != TEXT("name"))
				InOutDetail->SetField(Pair.Key, Pair.Value);
#else
		OutError = TEXT("graphOverview 仅在编辑器构建可用");
#endif
	}
	else if (SectionName == TEXT("graph"))
	{
#if WITH_EDITOR
		TSharedPtr<FJsonObject> GraphResult = HandleBPGraph(BP, Q);
		for (const auto& Pair : GraphResult->Values)
			if (Pair.Key != TEXT("assetType") && Pair.Key != TEXT("name"))
				InOutDetail->SetField(Pair.Key, Pair.Value);
#else
		OutError = TEXT("graph 仅在编辑器构建可用");
#endif
	}
	else if (SectionName == TEXT("defaults"))
	{
		TSharedPtr<FJsonObject> DefResult = HandleBPDefaults(BP, Q);
		for (const auto& Pair : DefResult->Values)
			if (Pair.Key != TEXT("assetType") && Pair.Key != TEXT("name") && Pair.Key != TEXT("parentClass"))
				InOutDetail->SetField(Pair.Key, Pair.Value);
	}
	else if (SectionName == TEXT("component"))
	{
		TSharedPtr<FJsonObject> CompResult = HandleBPComponents(BP, Q);
		for (const auto& Pair : CompResult->Values)
			InOutDetail->SetField(Pair.Key, Pair.Value);
	}
	else if (SectionName == TEXT("variable") || SectionName == TEXT("function"))
	{
#if WITH_EDITOR
		// variable / function 共享同一套 entry 列表，构建时按 SectionName 过滤
		TArray<FBPEntryLocal> All;

		if (SectionName == TEXT("variable"))
		{
			for (const FBPVariableDescription& Var : BP->NewVariables)
			{
				FBPEntryLocal E;
			E.Name = Var.VarName.ToString(); E.Kind = TEXT("variable");
			E.Type = Var.VarType.PinCategory.ToString(); E.bIsPublic = (Var.PropertyFlags & CPF_Edit) != 0;
			if (!Var.VarType.PinSubCategory.IsNone()) E.SubType = Var.VarType.PinSubCategory.ToString();
			if (Var.VarType.PinSubCategoryObject.IsValid()) E.SubTypeObject = Var.VarType.PinSubCategoryObject->GetName();
			if (Var.HasMetaData(TEXT("Category"))) E.Category = Var.GetMetaData(TEXT("Category"));
				All.Add(E);
			}
		}
		else // function
		{
			for (UEdGraph* Graph : BP->FunctionGraphs)
			{
				if (!Graph) continue;
				FBPEntryLocal E; E.Name = Graph->GetName(); E.Kind = TEXT("function");
				All.Add(E);
			}
		}

		All = All.FilterByPredicate([&](const FBPEntryLocal& E)
		{
			if (!Q.NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(E.Name, Q.NameFilter)) return false;
			return true;
		});

		const int32 Total = All.Num(); int32 Start, End; FNexusJsonUtils::ComputeSlice(Total, Q.Offset, Q.Limit, Start, End);

		TArray<TSharedPtr<FJsonValue>> Page;
		for (int32 i = Start; i < End; ++i)
		{
			const FBPEntryLocal& E = All[i];
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), E.Name);
			Obj->SetStringField(TEXT("kind"), E.Kind);
			if (!E.Type.IsEmpty())          { Obj->SetStringField(TEXT("type"),          E.Type); }
			if (!E.SubType.IsEmpty())       { Obj->SetStringField(TEXT("subType"),       E.SubType); }
			if (!E.SubTypeObject.IsEmpty()) { Obj->SetStringField(TEXT("subTypeObject"), E.SubTypeObject); }
			if (!E.Category.IsEmpty())      { Obj->SetStringField(TEXT("category"),      E.Category); }
			if (E.Kind == TEXT("variable") && E.bIsPublic) { Obj->SetBoolField(TEXT("isPublic"), true); }
			// 函数节：从 GeneratedClass 提取签名
			if (E.Kind == TEXT("function") && BP->GeneratedClass)
			{
				UFunction* Func = BP->GeneratedClass->FindFunctionByName(*E.Name);
				if (Func)
				{
					TArray<TSharedPtr<FJsonValue>> Inputs, Outputs;
					for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
					{
						FProperty* Prm = *It;
						TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
						PObj->SetStringField(TEXT("name"), Prm->GetName());
						PObj->SetStringField(TEXT("type"), Prm->GetCPPType());
						if (Prm->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
							Outputs.Add(MakeShared<FJsonValueObject>(PObj));
						else
							Inputs.Add(MakeShared<FJsonValueObject>(PObj));
					}
					if (Inputs.Num()  > 0) Obj->SetArrayField(TEXT("inputs"),  Inputs);
					if (Outputs.Num() > 0) Obj->SetArrayField(TEXT("outputs"), Outputs);
				}
			}
			Page.Add(MakeShared<FJsonValueObject>(Obj));
		}

		// 每个 section 写独立的列表键，避免 variable 和 function 合并时覆盖
		const FString ListKey = (SectionName == TEXT("variable")) ? TEXT("variables") : TEXT("functions");
		InOutDetail->SetNumberField(FString::Printf(TEXT("%sTotalCount"), *SectionName), Total);
		InOutDetail->SetArrayField(ListKey, Page);
#else
		OutError = FString::Printf(TEXT("section '%s' 仅在编辑器构建可用"), *SectionName);
#endif
	}
	else
	{
		OutError = FString::Printf(TEXT("未处理的 section '%s'"), *SectionName);
	}
}

TArray<TSharedPtr<FJsonObject>> FGetAssetBlueprintCapability::ExpandPerEntry(
	const TSharedPtr<FJsonObject>& Args) const
{
	return {};
}

REGISTER_MCP_CAPABILITY(FGetAssetBlueprintCapability)



