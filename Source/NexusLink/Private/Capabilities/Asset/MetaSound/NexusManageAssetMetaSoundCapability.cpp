// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MetaSound/NexusManageAssetMetaSoundCapability.h"

#if WITH_METASOUND

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "MetasoundSource.h"
#include "MetasoundFrontendDocument.h"
#include "UObject/UnrealType.h"
#if NX_UE_HAS_METASOUND_PATCH
#include "Metasound.h"
#endif

void FManageAssetMetaSoundCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_meta_sound");
	Out.SearchAssetTypes = {TEXT("MetaSoundSource"), TEXT("MetaSoundPatch")};
	Out.Description = TEXT("编辑 MetaSound Source/Patch 图：接口 IO、节点与边（≥UE5.3 Document API）。");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作类型"), {
			TEXT("add_input"), TEXT("remove_input"),
			TEXT("add_output"), TEXT("remove_output"),
			TEXT("add_node"), TEXT("remove_node"),
			TEXT("add_edge"), TEXT("remove_edge")
		}))
		.Prop(TEXT("name"), FNexusSchema::Str(TEXT("接口输入/输出名（add/remove_input|output）")))
		.Prop(TEXT("typeName"), FNexusSchema::Str(TEXT("类型名，如 Audio/Float/Trigger（add_input/add_output）")))
		.Prop(TEXT("classID"), FNexusSchema::Str(TEXT("节点类 GUID（add_node；来自 get_asset_meta_sound dependencies）")))
		.Prop(TEXT("nodeName"), FNexusSchema::Str(TEXT("节点显示名（add_node 可选）")))
		.Prop(TEXT("nodeID"), FNexusSchema::Str(TEXT("节点 GUID（remove_node）")))
		.Prop(TEXT("fromNodeID"), FNexusSchema::Str(TEXT("边源节点 GUID（add/remove_edge）")))
		.Prop(TEXT("fromPin"), FNexusSchema::Str(TEXT("边源引脚名（add/remove_edge）")))
		.Prop(TEXT("toNodeID"), FNexusSchema::Str(TEXT("边目标节点 GUID（add/remove_edge）")))
		.Prop(TEXT("toPin"), FNexusSchema::Str(TEXT("边目标引脚名（add/remove_edge）")))
		.Required({ TEXT("action") })
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("MetaSound Source 或 Patch 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = {
		TEXT("metasound"), TEXT("audio"), TEXT("sound"), TEXT("input"), TEXT("output"),
		TEXT("node"), TEXT("edge"), TEXT("wire"), TEXT("connect"), TEXT("patch")
	};
	Out.RelatedCapabilities = {
		TEXT("get_asset_meta_sound"), TEXT("create_asset_meta_sound"), TEXT("create_asset_meta_sound_patch")
	};
	Out.WhenToUse = TEXT("改 MetaSound 接口/节点/边；边用 fromNodeID/fromPin/toNodeID/toPin");
}

#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT

/** 经反射取 FMetasoundFrontendDocument（属性名在 Source/Patch 上大小写不同）。 */
static FMetasoundFrontendDocument* GetMutableDocumentByProp(UObject* Asset, const TCHAR* PropName)
{
	if (!Asset) return nullptr;
	FProperty* Prop = Asset->GetClass()->FindPropertyByName(PropName);
	FStructProperty* StructProp = CastField<FStructProperty>(Prop);
	if (!StructProp) return nullptr;
	return StructProp->ContainerPtrToValuePtr<FMetasoundFrontendDocument>(Asset);
}

static FMetasoundFrontendDocument* GetMutableDocument(UMetaSoundSource* Source)
{
	return GetMutableDocumentByProp(Source, TEXT("RootMetasoundDocument"));
}
#if NX_UE_HAS_METASOUND_PATCH
static FMetasoundFrontendDocument* GetMutableDocumentPatch(UMetaSoundPatch* Patch)
{
	return GetMutableDocumentByProp(Patch, TEXT("RootMetaSoundDocument"));
}
#endif

static void ApplyOperation(const TSharedPtr<FJsonObject>& Op, FMetasoundFrontendDocument* Doc,
	const FString& AssetPath, TArray<TSharedPtr<FJsonValue>>& OutEntries)
{
	FString Action;
	Op->TryGetStringField(TEXT("action"), Action);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("action"), Action);

	FMetasoundFrontendClassInterface& Iface = Doc->RootGraph.Interface;

	if (Action == TEXT("add_input"))
	{
		FString Name, TypeName;
		if (!Op->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() ||
		    !Op->TryGetStringField(TEXT("typeName"), TypeName) || TypeName.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_input 需要 name 与 typeName"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		const FName InputName(*Name);
		for (const FMetasoundFrontendClassInput& Existing : Iface.Inputs)
		{
			if (Existing.Name == InputName)
			{
				Result->SetBoolField(TEXT("alreadyExists"), true);
				OutEntries.Add(MakeShared<FJsonValueObject>(Result));
				return;
			}
		}
		FMetasoundFrontendClassInput NewInput;
		NewInput.Name = InputName;
		NewInput.TypeName = FName(*TypeName);
		NewInput.VertexID = FGuid::NewGuid();
		Iface.Inputs.Add(NewInput);
	}
	else if (Action == TEXT("remove_input"))
	{
		FString Name;
		Op->TryGetStringField(TEXT("name"), Name);
		const FName InputName(*Name);
		const int32 Removed = Iface.Inputs.RemoveAll([&](const FMetasoundFrontendClassInput& I) {
			return I.Name == InputName;
		});
		if (Removed == 0)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("input '%s' 未找到"), *Name));
		}
	}
	else if (Action == TEXT("add_output"))
	{
		FString Name, TypeName;
		if (!Op->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() ||
		    !Op->TryGetStringField(TEXT("typeName"), TypeName) || TypeName.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_output 需要 name 与 typeName"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		const FName OutputName(*Name);
		for (const FMetasoundFrontendClassOutput& Existing : Iface.Outputs)
		{
			if (Existing.Name == OutputName)
			{
				Result->SetBoolField(TEXT("alreadyExists"), true);
				OutEntries.Add(MakeShared<FJsonValueObject>(Result));
				return;
			}
		}
		FMetasoundFrontendClassOutput NewOutput;
		NewOutput.Name = OutputName;
		NewOutput.TypeName = FName(*TypeName);
		NewOutput.VertexID = FGuid::NewGuid();
		Iface.Outputs.Add(NewOutput);
	}
	else if (Action == TEXT("remove_output"))
	{
		FString Name;
		Op->TryGetStringField(TEXT("name"), Name);
		const FName OutputName(*Name);
		const int32 Removed = Iface.Outputs.RemoveAll([&](const FMetasoundFrontendClassOutput& O) {
			return O.Name == OutputName;
		});
		if (Removed == 0)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("output '%s' 未找到"), *Name));
		}
	}
	else if (Action == TEXT("add_node"))
	{
		FString ClassIDStr, NodeName;
		Op->TryGetStringField(TEXT("classID"), ClassIDStr);
		Op->TryGetStringField(TEXT("nodeName"), NodeName);
		FGuid ClassGuid;
		if (!FGuid::Parse(ClassIDStr, ClassGuid))
		{
			Result->SetStringField(TEXT("error"), TEXT("add_node 需要合法 classID（GUID）"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		FMetasoundFrontendNode NewNode;
		NewNode.ID = FGuid::NewGuid();
		NewNode.ClassID = ClassGuid;
		if (!NodeName.IsEmpty())
		{
			NewNode.Name = FName(*NodeName);
		}
		Doc->RootGraph.Graph.Nodes.Add(NewNode);
		Result->SetStringField(TEXT("nodeID"), NewNode.ID.ToString());
	}
	else if (Action == TEXT("remove_node"))
	{
		FString NodeIDStr;
		Op->TryGetStringField(TEXT("nodeID"), NodeIDStr);
		FGuid NodeGuid;
		if (!FGuid::Parse(NodeIDStr, NodeGuid))
		{
			Result->SetStringField(TEXT("error"), TEXT("remove_node 需要合法 nodeID"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		const int32 RemovedNodes = Doc->RootGraph.Graph.Nodes.RemoveAll(
			[&](const FMetasoundFrontendNode& N) { return N.GetID() == NodeGuid; });
		const int32 RemovedEdges = Doc->RootGraph.Graph.Edges.RemoveAll(
			[&](const FMetasoundFrontendEdge& E) {
				return E.From.NodeID == NodeGuid || E.To.NodeID == NodeGuid;
			});
		Result->SetNumberField(TEXT("removedEdges"), RemovedEdges);
		if (RemovedNodes == 0)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("节点 '%s' 未找到"), *NodeIDStr));
		}
	}
	else if (Action == TEXT("add_edge"))
	{
		FString FromNodeIDStr, FromPin, ToNodeIDStr, ToPin;
		if (!Op->TryGetStringField(TEXT("fromNodeID"), FromNodeIDStr) ||
			!Op->TryGetStringField(TEXT("fromPin"), FromPin) ||
			!Op->TryGetStringField(TEXT("toNodeID"), ToNodeIDStr) ||
			!Op->TryGetStringField(TEXT("toPin"), ToPin))
		{
			Result->SetStringField(TEXT("error"), TEXT("add_edge 需要 fromNodeID/fromPin/toNodeID/toPin"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		FMetasoundFrontendEdge NewEdge;
		FGuid::Parse(FromNodeIDStr, NewEdge.From.NodeID);
		NewEdge.From.VertexName = FName(*FromPin);
		FGuid::Parse(ToNodeIDStr, NewEdge.To.NodeID);
		NewEdge.To.VertexName = FName(*ToPin);
		const bool bExists = Doc->RootGraph.Graph.Edges.ContainsByPredicate(
			[&](const FMetasoundFrontendEdge& E) {
				return E.From.NodeID == NewEdge.From.NodeID &&
				       E.From.VertexName == NewEdge.From.VertexName &&
				       E.To.NodeID == NewEdge.To.NodeID &&
				       E.To.VertexName == NewEdge.To.VertexName;
			});
		if (bExists)
		{
			Result->SetBoolField(TEXT("alreadyExists"), true);
		}
		else
		{
			Doc->RootGraph.Graph.Edges.Add(NewEdge);
		}
	}
	else if (Action == TEXT("remove_edge"))
	{
		FString FromNodeIDStr, FromPin, ToNodeIDStr, ToPin;
		Op->TryGetStringField(TEXT("fromNodeID"), FromNodeIDStr);
		Op->TryGetStringField(TEXT("fromPin"), FromPin);
		Op->TryGetStringField(TEXT("toNodeID"), ToNodeIDStr);
		Op->TryGetStringField(TEXT("toPin"), ToPin);
		FGuid FromGuid, ToGuid;
		FGuid::Parse(FromNodeIDStr, FromGuid);
		FGuid::Parse(ToNodeIDStr, ToGuid);
		const FName FromPinName(*FromPin), ToPinName(*ToPin);
		const int32 Removed = Doc->RootGraph.Graph.Edges.RemoveAll(
			[&](const FMetasoundFrontendEdge& E) {
				return E.From.NodeID == FromGuid && E.From.VertexName == FromPinName &&
				       E.To.NodeID == ToGuid && E.To.VertexName == ToPinName;
			});
		if (Removed == 0)
		{
			Result->SetStringField(TEXT("error"), TEXT("边未找到"));
		}
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("未知 action '%s'；支持: add_input/remove_input/add_output/remove_output/add_node/remove_node/add_edge/remove_edge"),
			*Action));
	}

	OutEntries.Add(MakeShared<FJsonValueObject>(Result));
}

#endif // NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT

FCapabilityResult FManageAssetMetaSoundCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UObject* SoundAsset = nullptr;
		UMetaSoundSource* Source = FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundSource>(AssetPath);
		if (Source) { SoundAsset = Source; }
#if NX_UE_HAS_METASOUND_PATCH
		UMetaSoundPatch* Patch = nullptr;
		if (!SoundAsset)
		{
			Patch = FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundPatch>(AssetPath);
			if (Patch) { SoundAsset = Patch; }
		}
#endif
		if (!SoundAsset)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("MetaSound Source / Patch 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("operations"), OpsArr) || !OpsArr || OpsArr->IsEmpty())
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				TEXT("operations 必填且不可为空"));
			return;
		}

#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT
		FMetasoundFrontendDocument* Doc = Source
			? GetMutableDocument(Source)
#if NX_UE_HAS_METASOUND_PATCH
			: GetMutableDocumentPatch(Patch);
#else
			: nullptr;
#endif
		if (!Doc)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				TEXT("无法访问 FMetasoundFrontendDocument（属性名不匹配或版本不支持）"));
			return;
		}

		for (const TSharedPtr<FJsonValue>& Val : *OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Val->TryGetObject(OpObj) || !OpObj) continue;
			ApplyOperation(*OpObj, Doc, AssetPath, OutEntries);
		}

		SoundAsset->MarkPackageDirty();
#else
		FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
			TEXT("manage_asset_meta_sound 图编辑需要 UE 5.3+（NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT）"));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetMetaSoundCapability)

#endif // WITH_METASOUND
