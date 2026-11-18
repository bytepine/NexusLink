// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusAnimationToolActions.h"

// 每条记录同时供动画动作发现、执行路由、参数校验和版本提示使用。
static const FNexusAnimationOutlinerAction OutlinerActions[] =
{
	{
		TEXT("GetOutlinerTree"), false, TEXT("NexusAnimation.Outliner.get_outliner_tree"),
		TEXT("SequencerOutlinerScriptingObject.GetRootNode"), TEXT("5.3"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")
	},
	{
		TEXT("GetOutlinerChildren"), false, TEXT("NexusAnimation.Outliner.get_outliner_children"),
		TEXT("SequencerOutlinerScriptingObject.GetChildren"), TEXT("5.3"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"node":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"type_filter":{"anyOf":[{"type":"string"},{"type":"null"}],"default":null}},"required":["node"]})schema")
	},
	{
		TEXT("GetOutlinerSelection"), false, TEXT("NexusAnimation.Outliner.get_outliner_selection"),
		TEXT("SequencerOutlinerScriptingObject.GetSelection"), TEXT("5.3"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")
	},
	{
		TEXT("GetNodeLabel"), false, TEXT("NexusAnimation.Outliner.get_node_label"),
		TEXT("SequencerViewModelStructExtensions.GetLabel"), TEXT("5.3"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"node":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["node"]})schema")
	},
	{
		TEXT("IsNodeExpanded"), false, TEXT("NexusAnimation.Outliner.is_node_expanded"),
		TEXT("SequencerOutlinerScriptingObject.IsExpanded"), TEXT("5.8"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"node":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["node"]})schema")
	},
	{
		TEXT("GetMutedNodes"), false, TEXT("NexusAnimation.Outliner.get_muted_nodes"),
		TEXT("SequencerOutlinerScriptingObject.GetMuteNodes"), TEXT("5.7"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")
	},
	{
		TEXT("GetSoloedNodes"), false, TEXT("NexusAnimation.Outliner.get_soloed_nodes"),
		TEXT("SequencerOutlinerScriptingObject.GetSoloNodes"), TEXT("5.7"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")
	},
	{
		TEXT("GetDeactivatedNodes"), false, TEXT("NexusAnimation.Outliner.get_deactivated_nodes"),
		TEXT("SequencerOutlinerScriptingObject.GetDeactivatedNodes"), TEXT("5.7"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")
	},
	{
		TEXT("GetLockedNodes"), false, TEXT("NexusAnimation.Outliner.get_locked_nodes"),
		TEXT("SequencerOutlinerScriptingObject.GetLockedNodes"), TEXT("5.7"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")
	},
	{
		TEXT("GetPinnedNodes"), false, TEXT("NexusAnimation.Outliner.get_pinned_nodes"),
		TEXT("SequencerOutlinerScriptingObject.GetPinnedNodes"), TEXT("5.7"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")
	},
	{
		TEXT("GetSectionsForNodes"), false, TEXT("NexusAnimation.Outliner.get_sections_for_nodes"),
		TEXT("SequencerModuleOutlinerScriptingObject.GetSections"), TEXT("5.6"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128}},"required":["nodes"]})schema")
	},
	{
		TEXT("SetOutlinerSelection"), true, TEXT("NexusAnimation.Outliner.set_outliner_selection"),
		TEXT("SequencerOutlinerScriptingObject.SetSelection"), TEXT("5.3"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128}},"required":["nodes"]})schema")
	},
	{
		TEXT("SetNodeExpanded"), true, TEXT("NexusAnimation.Outliner.set_node_expanded"),
		TEXT("SequencerOutlinerScriptingObject.SetExpansion"), TEXT("5.8"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"expanded":{"type":"boolean"}},"required":["nodes","expanded"]})schema")
	},
	{
		TEXT("SetNodeMuted"), true, TEXT("NexusAnimation.Outliner.set_node_muted"),
		TEXT("SequencerOutlinerScriptingObject.SetMuteNodes"), TEXT("5.7"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"muted":{"type":"boolean"}},"required":["nodes","muted"]})schema")
	},
	{
		TEXT("SetNodeSolo"), true, TEXT("NexusAnimation.Outliner.set_node_solo"),
		TEXT("SequencerOutlinerScriptingObject.SetSoloNodes"), TEXT("5.7"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"soloed":{"type":"boolean"}},"required":["nodes","soloed"]})schema")
	},
	{
		TEXT("SetNodeDeactivated"), true, TEXT("NexusAnimation.Outliner.set_node_deactivated"),
		TEXT("SequencerOutlinerScriptingObject.SetDeactivatedNodes"), TEXT("5.7"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"deactivated":{"type":"boolean"}},"required":["nodes","deactivated"]})schema")
	},
	{
		TEXT("SetNodeLocked"), true, TEXT("NexusAnimation.Outliner.set_node_locked"),
		TEXT("SequencerOutlinerScriptingObject.SetLockedNodes"), TEXT("5.7"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"locked":{"type":"boolean"}},"required":["nodes","locked"]})schema")
	},
	{
		TEXT("SetNodePinned"), true, TEXT("NexusAnimation.Outliner.set_node_pinned"),
		TEXT("SequencerOutlinerScriptingObject.SetPinnedNodes"), TEXT("5.7"),
		TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"pinned":{"type":"boolean"}},"required":["nodes","pinned"]})schema")
	},
};

TMap<FString, FString> FNexusAnimationToolActions::WithOutlinerActions(TMap<FString, FString> Entries, bool bWrite)
{
	for (const FNexusAnimationOutlinerAction& Action : OutlinerActions)
	{
		if (Action.bWrite == bWrite) Entries.Add(Action.PublicName, Action.Route);
	}
	return Entries;
}

TMap<FString, FString> FNexusAnimationToolActions::WithOutlinerMetadata(
	TMap<FString, FString> Entries, const TCHAR* FNexusAnimationOutlinerAction::* Field)
{
	for (const FNexusAnimationOutlinerAction& Action : OutlinerActions)
	{
		Entries.Add(Action.Route, Action.*Field);
	}
	return Entries;
}
