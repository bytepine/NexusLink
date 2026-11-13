// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorGraphTools.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Editor.h"
#include "GraphEditor.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "BehaviorTree/BehaviorTree.h"
#if NX_UE_HAS_BEHAVIOR_TREE_EDITOR_VIEW
#include "BehaviorTreeEditor.h"
#endif
#if WITH_PCG
#include "PCGGraph.h"
#if NX_UE_HAS_PCG_EDITOR_VIEW
#include "PCGEditor.h"
#include "Framework/Docking/TabManager.h"
#endif
#endif
#include "Engine/World.h"
#include "Materials/Material.h"
#include "IMaterialEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/UObjectHash.h"
#include "Misc/App.h"
#include "Dom/JsonValue.h"
#include "Widgets/Docking/SDockTab.h"

void FNexusEditorGraphTools::Define(FNexusCapabilityDefinition& Out, bool bWrite)
{
	auto Schema = FNexusSchema::Object().Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset discovered via SearchAsset")));
	Schema.Prop(TEXT("GraphName"), FNexusSchema::Str(TEXT("Relative graph path returned by GetEditorGraph; required except graph listing/Open")));
	Schema.Prop(TEXT("NodeId"), FNexusSchema::Str(TEXT("Node GUID from this graph snapshot; Get filter or FocusNode target")));
	if (bWrite)
	{
		Schema.Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Native view command"), {TEXT("Open"),TEXT("SetView"),TEXT("ZoomToFit"),TEXT("FocusNode"),TEXT("SelectNodes"),TEXT("ClearSelection")}));
		Schema.Prop(TEXT("NodeIds"), FNexusSchema::StrArr(TEXT("SelectNodes: 1-256 GUIDs, all validated before selection changes")));
		Schema.Prop(TEXT("SelectionMode"), FNexusSchema::Enum(TEXT("SelectNodes mode"), {TEXT("Replace"),TEXT("Add"),TEXT("Remove")}, TEXT("Replace")));
		Schema.Prop(TEXT("ViewLocation"), FNexusSchema::Object().Required(TEXT("X"), FNexusSchema::Num(TEXT("Graph-space left offset"))).Required(TEXT("Y"), FNexusSchema::Num(TEXT("Graph-space top offset"))).Build().ToSharedRef());
		Schema.Prop(TEXT("Zoom"), FNexusSchema::Num(TEXT("Positive scale up to 10; engine snaps/clamps to supported levels")));
		Schema.Prop(TEXT("bOnlySelection"), FNexusSchema::Bool(TEXT("ZoomToFit selected nodes only"), true, false));
	}
	else
	{
		Schema.Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Graph path or node title/class substring")));
		Schema.Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Node page offset"), 0, 0));
		Schema.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Node page size"), 100, 1, 500));
	}
	Out.InputSchema = Schema.Build();
	Out.OutputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Source asset path")))
		.Prop(TEXT("Backend"), FNexusSchema::Str(TEXT("Native engine API backend")))
		.Prop(TEXT("GraphName"), FNexusSchema::Str(TEXT("Exact relative graph path")))
		.Prop(TEXT("GraphPath"), FNexusSchema::Str(TEXT("Current graph object path; preview paths may be transient")))
		.Prop(TEXT("GraphSource"), FNexusSchema::Str(TEXT("Asset or EditorPreview")))
		.Prop(TEXT("Graphs"), FNexusSchema::ArrayOf(TEXT("Graph inventory"), FNexusSchema::AnyObject(TEXT("Graph path, class and open state"))))
		.Prop(TEXT("Graphs_defaults"), FNexusSchema::AnyObject(TEXT("Common graph fields")))
		.Prop(TEXT("Nodes"), FNexusSchema::ArrayOf(TEXT("Node layout and view state; pin semantics use typed asset tools"), FNexusSchema::AnyObject(TEXT("Node GUID, title, position and size"))))
		.Prop(TEXT("Nodes_defaults"), FNexusSchema::AnyObject(TEXT("Common node fields")))
		.Prop(TEXT("SelectedNodeIds"), FNexusSchema::StrArr(TEXT("Selected GUIDs in this graph")))
		.Prop(TEXT("ViewLocation"), FNexusSchema::Object().Required(TEXT("X"), FNexusSchema::Num(TEXT("Graph left offset"))).Required(TEXT("Y"), FNexusSchema::Num(TEXT("Graph top offset"))).Build().ToSharedRef())
		.Prop(TEXT("Zoom"), FNexusSchema::Num(TEXT("Current native scale")))
		.Prop(TEXT("bIsOpen"), FNexusSchema::Bool(TEXT("A native graph view exists")))
		.Prop(TEXT("bViewAvailable"), FNexusSchema::Bool(TEXT("Viewport and selection were read from a live view")))
		.Prop(TEXT("bDirty"), FNexusSchema::Bool(TEXT("Source asset package is dirty; this tool does not save")))
		.Prop(TEXT("bRequestAccepted"), FNexusSchema::Bool(TEXT("Native command was submitted")))
		.Prop(TEXT("bViewDeferred"), FNexusSchema::Bool(TEXT("Focus/fit animation requires later readback")))
		.Prop(TEXT("SnapshotAt"), FNexusSchema::Str(TEXT("Snapshot UTC time")))
		.Prop(TEXT("TtlSeconds"), FNexusSchema::Int(TEXT("Snapshot TTL")))
		.Prop(TEXT("Total"), FNexusSchema::Int(TEXT("Matching graphs or nodes")))
		.Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Actual node offset")))
		.Prop(TEXT("bTruncated"), FNexusSchema::Bool(TEXT("More matches remain"))).Build();
	Out.Tags = {bWrite ? FNexusMcpTags::Write : FNexusMcpTags::Readonly, FNexusMcpTags::Editor};
	Out.Prerequisites = {TEXT("EditorOnly")};
	Out.bAutoTransaction = false; Out.bAllowBatch = false; Out.bCacheable = false;
	Out.ExtraSearchKeywords = {TEXT("node selection"),TEXT("canvas view"),TEXT("pan zoom"),TEXT("graph focus"),TEXT("frame nodes")};
	Out.RelatedCapabilities = {bWrite ? TEXT("GetEditorGraph") : TEXT("ControlEditorGraph"), TEXT("ControlEditorAssetEditor"), TEXT("GetAssetBlueprint"), TEXT("ManageAssetBlueprint"), TEXT("GetAssetMaterial"), TEXT("ManageAssetMaterial")};
}

struct FNexusGraphTarget final
{
	UObject* Asset = nullptr;
	UObject* Root = nullptr;
	IAssetEditorInstance* AssetEditor = nullptr;
	TArray<UEdGraph*> Graphs;
	bool bPreview = false;
};

static bool NexusResolveGraphTarget(const FString& AssetPath, FNexusGraphTarget& Target, FString& Error)
{
	Target = FNexusGraphTarget();
	if (!GEditor || IsRunningCommandlet()) { Error = TEXT("An editor host is required"); return false; }
	Target.Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
	if (!Target.Asset || Target.Asset->IsA(UWorld::StaticClass())) { Error = TEXT("A non-map asset is required"); return false; }
	Target.Root = Target.Asset;
	auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!Editors) { Error = TEXT("Asset editor subsystem is unavailable"); return false; }
	const auto Instances = Editors->FindEditorsForAsset(Target.Asset);
	if (Instances.Num() > 1) { Error = TEXT("Multiple editors are editing this asset; close duplicates before targeting a graph view"); return false; }
	Target.AssetEditor = Instances.Num() ? Instances[0] : nullptr;
	if (Target.AssetEditor && Target.AssetEditor->GetEditorName() == FName(TEXT("MaterialEditor")))
	{
		// 材质窗口使用预览副本，原资产的图不能代表当前窗口。
		auto* MaterialEditor = static_cast<IMaterialEditor*>(Target.AssetEditor);
		UMaterial* Preview = Cast<UMaterial>(MaterialEditor->GetMaterialInterface());
		if (!Preview) { Error = TEXT("Material editor preview graph is unavailable"); return false; }
		Target.Root = Preview; Target.bPreview = true;
	}
	TArray<UObject*> Owned;
#if NX_UE_HAS_GET_OBJECTS_FLAGS_ENUM
	GetObjectsWithOuter(Target.Root, Owned, EGetObjectsFlags::IncludeNestedObjects);
#else
	GetObjectsWithOuter(Target.Root, Owned, true);
#endif
	for (UObject* Object : Owned) if (UEdGraph* Graph = Cast<UEdGraph>(Object)) Target.Graphs.Add(Graph);
	Target.Graphs.Sort([&Target](const UEdGraph& Left, const UEdGraph& Right) { return Left.GetPathName(Target.Root) < Right.GetPathName(Target.Root); });
	return true;
}

static TSharedPtr<SGraphEditor> NexusFindGraphView(const FNexusGraphTarget& Target, UEdGraph* Graph)
{
#if WITH_PCG
	if (Target.Asset->IsA(UPCGGraph::StaticClass()))
	{
#if NX_UE_HAS_PCG_EDITOR_VIEW
		if (Target.AssetEditor && Target.AssetEditor->GetEditorName() == FName(TEXT("PCGEditor")))
		{
			auto* Editor = static_cast<FPCGEditor*>(Target.AssetEditor);
			const auto Manager = Editor->GetTabManager();
#if NX_UE_HAS_PCG_EDITOR_DOCUMENT_TABS
			const FName TabId = PCGEditorTabs::GraphEditorID;
#else
			const FName TabId = Editor->GetPanelID(EPCGEditorPanel::GraphEditor);
#endif
			const auto Tab = Manager.IsValid() ? Manager->FindExistingLiveTab(TabId) : nullptr;
			if (Tab.IsValid() && Tab->GetContent()->GetType() == FName(TEXT("SGraphEditor")))
			{
				const auto View = StaticCastSharedRef<SGraphEditor>(Tab->GetContent());
				if (View->GetCurrentGraph() == Graph) return View;
			}
		}
#endif
		return nullptr;
	}
#endif
	if (Target.Asset->IsA(UBehaviorTree::StaticClass()))
	{
#if NX_UE_HAS_BEHAVIOR_TREE_EDITOR_VIEW
		if (Target.AssetEditor && Target.AssetEditor->GetEditorName() == FName(TEXT("Behavior Tree")))
		{
			const auto View = static_cast<FBehaviorTreeEditor*>(Target.AssetEditor)->GetFocusedGraphPtr().Pin();
			if (View.IsValid() && View->GetCurrentGraph() == Graph) return View;
		}
#endif
		return nullptr;
	}
	if (!Target.Asset->IsA(UBlueprint::StaticClass())) return SGraphEditor::FindGraphEditorForGraph(Graph);
	if (!Target.AssetEditor) return nullptr;
	const FName EditorName = Target.AssetEditor->GetEditorName();
	if (EditorName != FName(TEXT("BlueprintEditor")) && EditorName != FName(TEXT("AnimationBlueprintEditor"))
		&& EditorName != FName(TEXT("WidgetBlueprintEditor"))) return nullptr;
	const auto BlueprintEditor = FKismetEditorUtilities::GetIBlueprintEditorForObject(Graph, false);
	if (!BlueprintEditor.IsValid() || static_cast<IAssetEditorInstance*>(BlueprintEditor.Get()) != Target.AssetEditor) return nullptr;
	// 从目标编辑器的文档直接取得图视图，避免全局查找误中同图的 Diff 窗口。
	TArray<TSharedPtr<SDockTab>> Tabs;
	static_cast<FBlueprintEditor*>(BlueprintEditor.Get())->FindOpenTabsContainingDocument(Graph, Tabs);
	if (Tabs.Num() != 1 || !Tabs[0].IsValid() || Tabs[0]->GetContent()->GetType() != FName(TEXT("SGraphEditor"))) return nullptr;
	const TSharedRef<SGraphEditor> View = StaticCastSharedRef<SGraphEditor>(Tabs[0]->GetContent());
	if (View->GetCurrentGraph() != Graph) return nullptr;
	return View;
}

static UEdGraph* NexusResolveGraph(const FNexusGraphTarget& Target, const FString& Name, FString& Error)
{
	UEdGraph* Match = nullptr;
	for (UEdGraph* Graph : Target.Graphs) if (Graph->GetPathName(Target.Root) == Name) return Graph;
	for (UEdGraph* Graph : Target.Graphs) if (Graph->GetName() == Name || (Name.IsEmpty() && Target.Graphs.Num() == 1))
	{
		if (Match) { Error = TEXT("Ambiguous GraphName; use the exact relative path from GetEditorGraph"); return nullptr; }
		Match = Graph;
	}
	if (!Match) Error = TEXT("GraphName must identify one owned graph; list graphs with GetEditorGraph first");
	return Match;
}

static UEdGraphNode* NexusResolveGraphNode(UEdGraph* Graph, const FString& Id, FString& Error)
{
	FGuid Guid;
	if (!FGuid::Parse(Id, Guid) || !Guid.IsValid()) { Error = TEXT("NodeId must be a valid GUID from this graph snapshot"); return nullptr; }
	UEdGraphNode* Match = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes) if (Node && Node->NodeGuid == Guid)
	{
		if (Match) { Error = TEXT("NodeId is ambiguous in this graph"); return nullptr; }
		Match = Node;
	}
	if (!Match) Error = TEXT("NodeId does not belong to the selected graph; query again after edits");
	return Match;
}

static TSharedPtr<FJsonObject> NexusGraphState(const FNexusGraphTarget& Target, UEdGraph* Graph, const TSharedPtr<SGraphEditor>& View)
{
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("AssetPath"), Target.Asset->GetPathName()); Result->SetStringField(TEXT("Backend"), TEXT("Independent"));
	Result->SetStringField(TEXT("GraphSource"), Target.bPreview ? TEXT("EditorPreview") : TEXT("Asset"));
	Result->SetBoolField(TEXT("bDirty"), Target.Asset->GetOutermost()->IsDirty());
	if (!Graph) return Result;
	Result->SetStringField(TEXT("GraphName"), Graph->GetPathName(Target.Root)); Result->SetStringField(TEXT("GraphPath"), Graph->GetPathName());
	Result->SetBoolField(TEXT("bIsOpen"), View.IsValid()); Result->SetBoolField(TEXT("bViewAvailable"), View.IsValid());
	if (View.IsValid())
	{
#if NX_UE_HAS_GRAPH_EDITOR_VIEW_VECTOR2F
		FVector2f Location;
#else
		FVector2D Location;
#endif
		float Zoom = 1; View->GetViewLocation(Location, Zoom);
		auto Offset = MakeShared<FJsonObject>(); Offset->SetNumberField(TEXT("X"), Location.X); Offset->SetNumberField(TEXT("Y"), Location.Y);
		Result->SetObjectField(TEXT("ViewLocation"), Offset); Result->SetNumberField(TEXT("Zoom"), Zoom);
		TArray<FString> Selected;
		for (UObject* Object : View->GetSelectedNodes()) if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object)) if (Node->GetGraph() == Graph) Selected.Add(Node->NodeGuid.ToString());
		Selected.Sort(); TArray<TSharedPtr<FJsonValue>> Values; for (const FString& Id : Selected) Values.Add(MakeShared<FJsonValueString>(Id));
		Result->SetArrayField(TEXT("SelectedNodeIds"), Values);
	}
	return Result;
}

FCapabilityResult FNexusEditorGraphTools::Execute(const TSharedPtr<FJsonObject>& Arguments, bool bWrite)
{
	const FNexusArgs Args(Arguments);
	const FString Action = Args.Str(TEXT("Action")), GraphName = Args.Str(TEXT("GraphName"));
	const int32 RequestedOffset = Args.Int(TEXT("Offset"), 0), RequestedLimit = Args.Int(TEXT("Limit"), 100);
	if (!bWrite && (RequestedOffset < 0 || RequestedLimit < 1 || RequestedLimit > 500)) return FCapabilityResult::MakeArgInvalid(TEXT("Offset must be nonnegative; Limit must be in [1,500]"));
	if (bWrite && Action != TEXT("Open") && GraphName.IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("GraphName is required for view commands"));
	if (!bWrite && GraphName.IsEmpty() && !Args.Str(TEXT("NodeId")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("NodeId filtering requires GraphName"));
	if (bWrite && (!FApp::CanEverRender() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().CanDisplayWindows()))
		return FCapabilityResult::MakeFatal(TEXT("Native graph view commands require a GUI editor"));
	FNexusGraphTarget Target; FString Error;
	if (!NexusResolveGraphTarget(Args.Str(TEXT("AssetPath")), Target, Error)) return FCapabilityResult::MakeFatal(Error);
	if (bWrite && Action == TEXT("Open"))
	{
		auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!Target.AssetEditor && !Editors->OpenEditorForAsset(Target.Asset, EToolkitMode::Standalone, TSharedPtr<IToolkitHost>(), false)) return FCapabilityResult::MakeFatal(TEXT("Native asset editor failed to open"));
		if (!NexusResolveGraphTarget(Args.Str(TEXT("AssetPath")), Target, Error)) return FCapabilityResult::MakeFatal(Error);
		if (Target.AssetEditor) Target.AssetEditor->FocusWindow(Target.Asset);
	}
	if (GraphName.IsEmpty() && (!bWrite || Action == TEXT("Open")))
	{
		auto Result = NexusGraphState(Target, nullptr, nullptr);
		TArray<TSharedPtr<FJsonValue>> Graphs; int32 Total = 0;
		for (UEdGraph* Graph : Target.Graphs)
		{
			const FString Name = Graph->GetPathName(Target.Root);
			if (!Args.Str(TEXT("NameFilter")).IsEmpty() && !Name.Contains(Args.Str(TEXT("NameFilter")))) continue;
			++Total; if (Graphs.Num() >= 128) continue;
			auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("GraphName"), Name); Entry->SetStringField(TEXT("GraphPath"), Graph->GetPathName());
			Entry->SetStringField(TEXT("GraphClass"), Graph->GetClass()->GetPathName());
			if (const UEdGraphSchema* GraphSchema = Graph->GetSchema()) Entry->SetStringField(TEXT("SchemaClass"), GraphSchema->GetClass()->GetPathName());
			Entry->SetNumberField(TEXT("NodeCount"), Graph->Nodes.Num()); Entry->SetBoolField(TEXT("bIsOpen"), NexusFindGraphView(Target, Graph).IsValid());
			Graphs.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("Graphs"), Graphs); Result->SetNumberField(TEXT("Total"), Total); Result->SetBoolField(TEXT("bTruncated"), Graphs.Num() < Total);
		if (bWrite) { Result->SetBoolField(TEXT("bRequestAccepted"), true); Result->SetBoolField(TEXT("bViewDeferred"), false); }
		FCapabilityResult Out; Out.Entries.Add(MakeShared<FJsonValueObject>(Result)); return Out;
	}
	UEdGraph* Graph = NexusResolveGraph(Target, GraphName, Error);
	if (!Graph) return FCapabilityResult::MakeArgInvalid(Error);
	TSharedPtr<SGraphEditor> View = NexusFindGraphView(Target, Graph);
	if (bWrite && Action == TEXT("Open"))
	{
		if (Target.Asset->IsA(UBlueprint::StaticClass()))
		{
			const auto BlueprintEditor = FKismetEditorUtilities::GetIBlueprintEditorForObject(Graph, false);
			if (BlueprintEditor.IsValid()) View = BlueprintEditor->OpenGraphAndBringToFront(Graph);
		}
#if NX_UE_HAS_MATERIAL_GRAPH_HYPERLINK
		else if (Target.bPreview) { static_cast<IMaterialEditor*>(Target.AssetEditor)->JumpToHyperlink(Graph); View = SGraphEditor::FindGraphEditorForGraph(Graph); }
#endif
		if (Target.AssetEditor) Target.AssetEditor->FocusWindow(Target.Asset);
	}
	if (bWrite && !View.IsValid()) return FCapabilityResult::MakeFatal(TEXT("No native view exists for this graph; open its asset/graph first or use its domain-specific tool"));
	if (!bWrite)
	{
		UEdGraphNode* OnlyNode = nullptr;
		if (!Args.Str(TEXT("NodeId")).IsEmpty()) { OnlyNode = NexusResolveGraphNode(Graph, Args.Str(TEXT("NodeId")), Error); if (!OnlyNode) return FCapabilityResult::MakeArgInvalid(Error); }
		auto Result = NexusGraphState(Target, Graph, View);
		const int32 Offset = FMath::Min(RequestedOffset, Graph->Nodes.Num());
		const int32 Limit = RequestedLimit;
		int32 Total = 0; TArray<TSharedPtr<FJsonValue>> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes) if (Node && (!OnlyNode || Node == OnlyNode))
		{
			const FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(); const FString Filter = Args.Str(TEXT("NameFilter"));
			if (!Filter.IsEmpty() && !Title.Contains(Filter) && !Node->GetClass()->GetName().Contains(Filter)) continue;
			if (Total++ < Offset || Nodes.Num() >= Limit) continue;
			auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("NodeId"), Node->NodeGuid.ToString()); Entry->SetStringField(TEXT("NodeClass"), Node->GetClass()->GetPathName()); Entry->SetStringField(TEXT("Title"), Title);
			Entry->SetNumberField(TEXT("PosX"), Node->NodePosX); Entry->SetNumberField(TEXT("PosY"), Node->NodePosY); Entry->SetNumberField(TEXT("Width"), Node->NodeWidth); Entry->SetNumberField(TEXT("Height"), Node->NodeHeight);
			Entry->SetStringField(TEXT("Comment"), Node->NodeComment); if (View.IsValid()) Entry->SetBoolField(TEXT("bSelected"), View->GetSelectedNodes().Contains(Node));
			Nodes.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("Nodes"), Nodes); Result->SetNumberField(TEXT("Total"), Total); Result->SetNumberField(TEXT("Offset"), Offset); Result->SetBoolField(TEXT("bTruncated"), Offset + Nodes.Num() < Total);
		FCapabilityResult Out; Out.Entries.Add(MakeShared<FJsonValueObject>(Result)); return Out;
	}
	// 先验证本次命令的全部目标与数值，避免部分选择或视图更新。
	TArray<UEdGraphNode*> Selected;
	UEdGraphNode* Focus = nullptr;
	if (Action == TEXT("FocusNode")) { Focus = NexusResolveGraphNode(Graph, Args.Str(TEXT("NodeId")), Error); if (!Focus) return FCapabilityResult::MakeArgInvalid(Error); }
	if (Action == TEXT("SelectNodes"))
	{
		TArray<FString> Ids; Arguments->TryGetStringArrayField(TEXT("NodeIds"), Ids);
		if (!Ids.Num() || Ids.Num() > 256) return FCapabilityResult::MakeArgInvalid(TEXT("SelectNodes requires 1-256 NodeIds"));
		for (const FString& Id : Ids) { UEdGraphNode* Node = NexusResolveGraphNode(Graph, Id, Error); if (!Node) return FCapabilityResult::MakeArgInvalid(Error); Selected.AddUnique(Node); }
	}
	if (Action == TEXT("ZoomToFit") && Args.Bool(TEXT("bOnlySelection")) && View->GetSelectedNodes().Num() == 0) return FCapabilityResult::MakeArgInvalid(TEXT("Selection is empty"));
	if (Action == TEXT("SetView"))
	{
#if NX_UE_HAS_GRAPH_EDITOR_VIEW_VECTOR2F
		FVector2f Location;
#else
		FVector2D Location;
#endif
		float CurrentZoom = 1; View->GetViewLocation(Location, CurrentZoom);
		const TSharedPtr<FJsonObject>* LocationArg = nullptr;
		if (!Arguments->HasField(TEXT("ViewLocation")) && !Arguments->HasField(TEXT("Zoom"))) return FCapabilityResult::MakeArgInvalid(TEXT("SetView requires ViewLocation or Zoom"));
		if (Arguments->HasField(TEXT("ViewLocation")))
		{
			double X = 0, Y = 0;
			if (!Arguments->TryGetObjectField(TEXT("ViewLocation"), LocationArg) || !LocationArg->IsValid() || !(*LocationArg)->TryGetNumberField(TEXT("X"), X) || !(*LocationArg)->TryGetNumberField(TEXT("Y"), Y)
				|| !FMath::IsFinite(X) || !FMath::IsFinite(Y) || FMath::Abs(X) > 10000000 || FMath::Abs(Y) > 10000000) return FCapabilityResult::MakeArgInvalid(TEXT("ViewLocation requires finite graph-space X/Y within +/-10000000"));
			Location.X = static_cast<float>(X); Location.Y = static_cast<float>(Y);
		}
		const double Zoom = Args.Num(TEXT("Zoom"), CurrentZoom);
		if (!FMath::IsFinite(Zoom) || Zoom <= 0 || Zoom > 10) return FCapabilityResult::MakeArgInvalid(TEXT("Zoom must be finite and in (0,10]"));
		View->SetViewLocation(Location, static_cast<float>(Zoom));
	}
	else if (Action == TEXT("SelectNodes"))
	{
		const FString Mode = Args.Str(TEXT("SelectionMode"), TEXT("Replace"));
		if (Mode == TEXT("Replace")) View->ClearSelectionSet();
		for (UEdGraphNode* Node : Selected) View->SetNodeSelection(Node, Mode != TEXT("Remove"));
	}
	else if (Action == TEXT("ClearSelection")) View->ClearSelectionSet();
	else if (Action == TEXT("FocusNode")) View->JumpToNode(Focus, false, true);
	else if (Action == TEXT("ZoomToFit")) View->ZoomToFit(Args.Bool(TEXT("bOnlySelection")));
	auto Result = NexusGraphState(Target, Graph, View);
	Result->SetBoolField(TEXT("bRequestAccepted"), true); Result->SetBoolField(TEXT("bViewDeferred"), Action == TEXT("FocusNode") || Action == TEXT("ZoomToFit"));
	FCapabilityResult Out; Out.Entries.Add(MakeShared<FJsonValueObject>(Result)); return Out;
}
