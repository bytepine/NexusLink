// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorGraphTools.h"
#include "Utils/NexusVersionCompat.h"

#if WITH_PCG
#include "EdGraph/EdGraph.h"
#include "GraphEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Docking/SDockTab.h"
#include "PCGGraph.h"
#if NX_UE_HAS_PCG_EDITOR_VIEW
#include "PCGEditor.h"
#include "Framework/Docking/TabManager.h"
#endif

#if NX_UE_HAS_PCG_EDITOR_DOCUMENT_TABS
static UPCGGraph* NexusGetPCGSourceGraph(UObject* Asset, UEdGraph* Graph)
{
	UPCGGraph* PCGGraph = Graph ? Graph->GetTypedOuter<UPCGGraph>() : nullptr;
	if (!PCGGraph || (PCGGraph != Asset && !PCGGraph->IsIn(Asset))) return nullptr;
	// 公开接口返回不透明的 UPCGEditorGraph 指针，仅比对身份，不依赖私有引擎头。
	if (static_cast<const void*>(PCGGraph->GetEditorGraph()) != static_cast<const void*>(Graph)) return nullptr;
	return PCGGraph;
}
#endif

static TOptional<TSharedPtr<SGraphEditor>> NexusFindPCGView(UObject* Asset, IAssetEditorInstance* AssetEditor, UEdGraph* Graph)
{
	if (Asset->IsA(UPCGGraph::StaticClass()))
	{
#if NX_UE_HAS_PCG_EDITOR_VIEW
		if (AssetEditor && AssetEditor->GetEditorName() == FName(TEXT("PCGEditor")))
		{
#if NX_UE_HAS_PCG_EDITOR_DOCUMENT_TABS
			if (!NexusGetPCGSourceGraph(Asset, Graph)) return TSharedPtr<SGraphEditor>();
			// 多文档共享 GraphEditor 标签类型，直接按原生图对象查找，不能取通配标签首项。
			const auto View = SGraphEditor::FindGraphEditorForGraph(Graph);
			if (View.IsValid() && View->GetCurrentGraph() == Graph) return TSharedPtr<SGraphEditor>(View);
#else
			auto* Editor = static_cast<FPCGEditor*>(AssetEditor);
			const auto Manager = Editor->GetTabManager();
			const FName TabId = Editor->GetPanelID(EPCGEditorPanel::GraphEditor);
			const auto Tab = Manager.IsValid() ? Manager->FindExistingLiveTab(TabId) : nullptr;
			if (Tab.IsValid() && Tab->GetContent()->GetType() == FName(TEXT("SGraphEditor")))
			{
				const auto View = StaticCastSharedRef<SGraphEditor>(Tab->GetContent());
				if (View->GetCurrentGraph() == Graph) return TSharedPtr<SGraphEditor>(View);
			}
#endif
		}
#endif
		return TSharedPtr<SGraphEditor>();
	}
	return {};
}

#if NX_UE_HAS_PCG_EDITOR_DOCUMENT_TABS
static bool NexusOpenPCGView(UObject* Asset, IAssetEditorInstance* AssetEditor, UEdGraph* Graph, TSharedPtr<SGraphEditor>& View, FString& Error)
{
		if (Asset->IsA(UPCGGraph::StaticClass()) && AssetEditor
			&& AssetEditor->GetEditorName() == FName(TEXT("PCGEditor")))
		{
			UPCGGraph* PCGGraph = NexusGetPCGSourceGraph(Asset, Graph);
			if (!PCGGraph) { Error = TEXT("GraphName must identify the current editor graph of an owned PCG graph"); return true; }
			const auto Tab = static_cast<FPCGEditor*>(AssetEditor)->OpenDocument(PCGGraph, FDocumentTracker::OpenNewDocument);
			View.Reset();
			if (Tab.IsValid() && Tab->GetContent()->GetType() == FName(TEXT("SGraphEditor")))
			{
				const auto OpenedView = StaticCastSharedRef<SGraphEditor>(Tab->GetContent());
				if (OpenedView->GetCurrentGraph() == Graph) View = OpenedView;
			}
			return true;
		}
	return false;
}
#endif

void NexusInstallPCGGraphView()
{
	FNexusEditorGraphTools::FindPCGView = &NexusFindPCGView;
#if NX_UE_HAS_PCG_EDITOR_DOCUMENT_TABS
	FNexusEditorGraphTools::OpenPCGView = &NexusOpenPCGView;
#endif
}

void NexusRemovePCGGraphView()
{
	FNexusEditorGraphTools::FindPCGView = nullptr;
	FNexusEditorGraphTools::OpenPCGView = nullptr;
}
#endif
