// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MetaSound/NexusManageAssetMetaSoundCapability.h"

#if WITH_METASOUND

#include "Utils/NexusArgs.h"
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
	Out.Description = TEXT("Edit MetaSound Source/Patch graph: IO, nodes, edges (≥UE5.3 Document API).");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Operation type"), {
			TEXT("add_input"), TEXT("remove_input"),
			TEXT("add_output"), TEXT("remove_output"),
			TEXT("add_node"), TEXT("remove_node"),
			TEXT("add_edge"), TEXT("remove_edge")
		}))
		.Prop(TEXT("name"), FNexusSchema::Str(TEXT("Interface input/output name (add/remove_input|output)")))
		.Prop(TEXT("typeName"), FNexusSchema::Str(TEXT("Type name, e.g. Audio/Float/Trigger (add_input/add_output)")))
		.Prop(TEXT("classID"), FNexusSchema::Str(TEXT("Node class GUID (add_node; from get_asset_meta_sound dependencies)")))
		.Prop(TEXT("nodeName"), FNexusSchema::Str(TEXT("Node display name (add_node, optional)")))
		.Prop(TEXT("nodeID"), FNexusSchema::Str(TEXT("node GUID (remove_node)")))
		.Prop(TEXT("fromNodeID"), FNexusSchema::Str(TEXT("Edge source node GUID (add/remove_edge)")))
		.Prop(TEXT("fromPin"), FNexusSchema::Str(TEXT("Edge source pin name (add/remove_edge)")))
		.Prop(TEXT("toNodeID"), FNexusSchema::Str(TEXT("Edge target node GUID (add/remove_edge)")))
		.Prop(TEXT("toPin"), FNexusSchema::Str(TEXT("Edge target pin name (add/remove_edge)")))
		.Required({ TEXT("action") })
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("MetaSound Source or Patch asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
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
	Out.WhenToUse = TEXT("Edit MetaSound IO/nodes/edges; edges use fromNodeID/fromPin/toNodeID/toPin");
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
	TSharedPtr<FJsonObject>& Result)
{
	if (!Doc)
	{
		Result->SetStringField(TEXT("error"), TEXT("Unable to access FMetasoundFrontendDocument (property mismatch or version unsupported)"));
		return;
	}
	const FNexusArgs A(Op);
	FMetasoundFrontendClassInterface& Iface = Doc->RootGraph.Interface;

	const FString Action = A.Str(TEXT("action")).ToLower();
	if (Action == TEXT("add_input"))
	{
		const FString Name = A.Str(TEXT("name"));
		const FString TypeName = A.Str(TEXT("typeName"));
		if (Name.IsEmpty() || TypeName.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_input requires name and typeName"));
			return;
		}
		const FName InputName(*Name);
		for (const FMetasoundFrontendClassInput& Existing : Iface.Inputs)
		{
			if (Existing.Name == InputName)
			{
				Result->SetBoolField(TEXT("alreadyExists"), true);
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
		const FString Name = A.Str(TEXT("name"));
		const FName InputName(*Name);
		const int32 Removed = Iface.Inputs.RemoveAll([&](const FMetasoundFrontendClassInput& I) {
			return I.Name == InputName;
		});
		if (Removed == 0)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("input '%s' not found"), *Name));
		}
	}
	else if (Action == TEXT("add_output"))
	{
		const FString Name = A.Str(TEXT("name"));
		const FString TypeName = A.Str(TEXT("typeName"));
		if (Name.IsEmpty() || TypeName.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_output requires name and typeName"));
			return;
		}
		const FName OutputName(*Name);
		for (const FMetasoundFrontendClassOutput& Existing : Iface.Outputs)
		{
			if (Existing.Name == OutputName)
			{
				Result->SetBoolField(TEXT("alreadyExists"), true);
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
		const FString Name = A.Str(TEXT("name"));
		const FName OutputName(*Name);
		const int32 Removed = Iface.Outputs.RemoveAll([&](const FMetasoundFrontendClassOutput& O) {
			return O.Name == OutputName;
		});
		if (Removed == 0)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("output '%s' not found"), *Name));
		}
	}
	else if (Action == TEXT("add_node"))
	{
		const FString ClassIDStr = A.Str(TEXT("classID"));
		const FString NodeName = A.Str(TEXT("nodeName"));
		FGuid ClassGuid;
		if (!FGuid::Parse(ClassIDStr, ClassGuid))
		{
			Result->SetStringField(TEXT("error"), TEXT("add_node requires valid classID (GUID)"));
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
		const FString NodeIDStr = A.Str(TEXT("nodeID"));
		FGuid NodeGuid;
		if (!FGuid::Parse(NodeIDStr, NodeGuid))
		{
			Result->SetStringField(TEXT("error"), TEXT("remove_node requires valid nodeID"));
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
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Node '%s' not found"), *NodeIDStr));
		}
	}
	else if (Action == TEXT("add_edge"))
	{
		const FString FromNodeIDStr = A.Str(TEXT("fromNodeID"));
		const FString FromPin = A.Str(TEXT("fromPin"));
		const FString ToNodeIDStr = A.Str(TEXT("toNodeID"));
		const FString ToPin = A.Str(TEXT("toPin"));
		if (FromNodeIDStr.IsEmpty() || FromPin.IsEmpty() || ToNodeIDStr.IsEmpty() || ToPin.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_edge requires fromNodeID/fromPin/toNodeID/toPin"));
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
		const FString FromNodeIDStr = A.Str(TEXT("fromNodeID"));
		const FString FromPin = A.Str(TEXT("fromPin"));
		const FString ToNodeIDStr = A.Str(TEXT("toNodeID"));
		const FString ToPin = A.Str(TEXT("toPin"));
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
			Result->SetStringField(TEXT("error"), TEXT("Edge not found"));
		}
	}
}

struct FMetaSoundActionState
{
	UObject* Asset = nullptr;
	FMetasoundFrontendDocument* Doc = nullptr;
};

static FMetasoundFrontendDocument* DocFrom(FNexusActionContext& Ctx)
{
	FMetaSoundActionState* S = static_cast<FMetaSoundActionState*>(Ctx.Target);
	return S ? S->Doc : nullptr;
}

#define NX_MS_HANDLER(Name) \
static void HandleMS_##Name(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx) \
{ \
	ApplyOperation(Op, DocFrom(Ctx), Ctx.Entry); \
}

NX_MS_HANDLER(AddInput)
NX_MS_HANDLER(RemoveInput)
NX_MS_HANDLER(AddOutput)
NX_MS_HANDLER(RemoveOutput)
NX_MS_HANDLER(AddNode)
NX_MS_HANDLER(RemoveNode)
NX_MS_HANDLER(AddEdge)
NX_MS_HANDLER(RemoveEdge)
#undef NX_MS_HANDLER

#endif // NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT

bool FManageAssetMetaSoundCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);

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
		OutError = FString::Printf(TEXT("MetaSound Source / Patch not found: %s"), *AssetPath);
		return false;
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
		OutError = TEXT("Unable to access FMetasoundFrontendDocument (property mismatch or version unsupported)");
		return false;
	}
	FMetaSoundActionState* State = new FMetaSoundActionState();
	State->Asset = SoundAsset;
	State->Doc = Doc;
	OutTarget = State;
	return true;
#else
	OutError = TEXT("manage_asset_meta_sound graph edit requires UE 5.3+ (NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT)");
	return false;
#endif
}

void FManageAssetMetaSoundCapability::FinalizeTarget(void* Target) const
{
#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT
	FMetaSoundActionState* State = static_cast<FMetaSoundActionState*>(Target);
	if (!State)
	{
		return;
	}
	if (State->Asset)
	{
		State->Asset->MarkPackageDirty();
	}
	delete State;
#else
	(void)Target;
#endif
}

void FManageAssetMetaSoundCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT
	OutHandlers.Add(TEXT("add_input"),     &HandleMS_AddInput);
	OutHandlers.Add(TEXT("remove_input"),  &HandleMS_RemoveInput);
	OutHandlers.Add(TEXT("add_output"),    &HandleMS_AddOutput);
	OutHandlers.Add(TEXT("remove_output"), &HandleMS_RemoveOutput);
	OutHandlers.Add(TEXT("add_node"),      &HandleMS_AddNode);
	OutHandlers.Add(TEXT("remove_node"),   &HandleMS_RemoveNode);
	OutHandlers.Add(TEXT("add_edge"),      &HandleMS_AddEdge);
	OutHandlers.Add(TEXT("remove_edge"),   &HandleMS_RemoveEdge);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetMetaSoundCapability)

#endif // WITH_METASOUND
