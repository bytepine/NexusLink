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

static const FMetasoundFrontendNode* FindMSNode(const FMetasoundFrontendGraph& Graph, const FGuid& NodeID)
{
	return Graph.Nodes.FindByPredicate([&](const FMetasoundFrontendNode& N) { return N.GetID() == NodeID; });
}

static FGuid FindOutputVertexID(const FMetasoundFrontendNode& Node, const FName PinName)
{
	for (const FMetasoundFrontendVertex& V : Node.Interface.Outputs)
	{
		if (V.Name == PinName) return V.VertexID;
	}
	return FGuid();
}

static FGuid FindInputVertexID(const FMetasoundFrontendNode& Node, const FName PinName)
{
	for (const FMetasoundFrontendVertex& V : Node.Interface.Inputs)
	{
		if (V.Name == PinName) return V.VertexID;
	}
	return FGuid();
}

void FNexusManageAssetMetaSoundCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetMetaSound");
	Out.SearchAssetTypes = {TEXT("MetaSoundSource"), TEXT("MetaSoundPatch")};
	Out.Description = TEXT("Edit MetaSound Source/Patch graph: IO, nodes, edges (≥UE5.3 Document API).");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Operation type"), {
			TEXT("AddInput"), TEXT("RemoveInput"),
			TEXT("AddOutput"), TEXT("RemoveOutput"),
			TEXT("AddNode"), TEXT("RemoveNode"),
			TEXT("AddEdge"), TEXT("RemoveEdge")
		}))
		.Prop(TEXT("name"), FNexusSchema::Str(TEXT("Interface input/output name (add/RemoveInput|output)")))
		.Prop(TEXT("TypeName"), FNexusSchema::Str(TEXT("Type name, e.g. Audio/Float/Trigger (AddInput/AddOutput)")))
		.Prop(TEXT("ClassId"), FNexusSchema::Str(TEXT("Node class GUID (AddNode; from GetAssetMetaSound dependencies)")))
		.Prop(TEXT("NodeName"), FNexusSchema::Str(TEXT("Node display name (AddNode, optional)")))
		.Prop(TEXT("NodeId"), FNexusSchema::Str(TEXT("node GUID (RemoveNode)")))
		.Prop(TEXT("FromNodeId"), FNexusSchema::Str(TEXT("Edge source node GUID (add/RemoveEdge)")))
		.Prop(TEXT("FromPin"), FNexusSchema::Str(TEXT("Edge source pin name (add/RemoveEdge)")))
		.Prop(TEXT("ToNodeId"), FNexusSchema::Str(TEXT("Edge target node GUID (add/RemoveEdge)")))
		.Prop(TEXT("ToPin"), FNexusSchema::Str(TEXT("Edge target pin name (add/RemoveEdge)")))
		.Required({ TEXT("Action") })
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("MetaSound Source or Patch asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = {
		TEXT("metasound"), TEXT("audio"), TEXT("Sound"), TEXT("input"), TEXT("Output"),
		TEXT("Node"), TEXT("edge"), TEXT("wire"), TEXT("Connect"), TEXT("patch")
	};
	Out.RelatedCapabilities = {
		TEXT("GetAssetMetaSound"), TEXT("CreateAssetMetaSound"), TEXT("CreateAssetMetaSoundPatch")
	};
	Out.WhenToUse = TEXT("Edit MetaSound IO/nodes/edges; edges use FromNodeId/FromPin/ToNodeId/ToPin");
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
		Result->SetStringField(TEXT("Error"), TEXT("Unable to access FMetasoundFrontendDocument (property mismatch or version unsupported)"));
		return;
	}
	const FNexusArgs A(Op);
#if NX_UE_HAS_METASOUND_CLASS_GET_DEFAULT_INTERFACE
	FMetasoundFrontendClassInterface& Iface = Doc->RootGraph.GetDefaultInterface();
#else
	FMetasoundFrontendClassInterface& Iface = Doc->RootGraph.Interface;
#endif

	const FString Action = A.Str(TEXT("Action")).ToLower();
	if (Action == TEXT("AddInput"))
	{
		const FString Name = A.Str(TEXT("name"));
		const FString TypeName = A.Str(TEXT("TypeName"));
		if (Name.IsEmpty() || TypeName.IsEmpty())
		{
			Result->SetStringField(TEXT("Error"), TEXT("AddInput requires name and TypeName"));
			return;
		}
		const FName InputName(*Name);
		for (const FMetasoundFrontendClassInput& Existing : Iface.Inputs)
		{
			if (Existing.Name == InputName)
			{
				Result->SetBoolField(TEXT("bAlreadyExists"), true);
				return;
			}
		}
		FMetasoundFrontendClassInput NewInput;
		NewInput.Name = InputName;
		NewInput.TypeName = FName(*TypeName);
		NewInput.VertexID = FGuid::NewGuid();
		Iface.Inputs.Add(NewInput);
	}
	else if (Action == TEXT("RemoveInput"))
	{
		const FString Name = A.Str(TEXT("name"));
		const FName InputName(*Name);
		const int32 Removed = Iface.Inputs.RemoveAll([&](const FMetasoundFrontendClassInput& I) {
			return I.Name == InputName;
		});
		if (Removed == 0)
		{
			Result->SetStringField(TEXT("Error"), FString::Printf(TEXT("input '%s' not found"), *Name));
		}
	}
	else if (Action == TEXT("AddOutput"))
	{
		const FString Name = A.Str(TEXT("name"));
		const FString TypeName = A.Str(TEXT("TypeName"));
		if (Name.IsEmpty() || TypeName.IsEmpty())
		{
			Result->SetStringField(TEXT("Error"), TEXT("AddOutput requires name and TypeName"));
			return;
		}
		const FName OutputName(*Name);
		for (const FMetasoundFrontendClassOutput& Existing : Iface.Outputs)
		{
			if (Existing.Name == OutputName)
			{
				Result->SetBoolField(TEXT("bAlreadyExists"), true);
				return;
			}
		}
		FMetasoundFrontendClassOutput NewOutput;
		NewOutput.Name = OutputName;
		NewOutput.TypeName = FName(*TypeName);
		NewOutput.VertexID = FGuid::NewGuid();
		Iface.Outputs.Add(NewOutput);
	}
	else if (Action == TEXT("RemoveOutput"))
	{
		const FString Name = A.Str(TEXT("name"));
		const FName OutputName(*Name);
		const int32 Removed = Iface.Outputs.RemoveAll([&](const FMetasoundFrontendClassOutput& O) {
			return O.Name == OutputName;
		});
		if (Removed == 0)
		{
			Result->SetStringField(TEXT("Error"), FString::Printf(TEXT("output '%s' not found"), *Name));
		}
	}
	else if (Action == TEXT("AddNode"))
	{
		const FString ClassIDStr = A.Str(TEXT("ClassId"));
		const FString NodeName = A.Str(TEXT("NodeName"));
		FGuid ClassGuid;
		if (!FGuid::Parse(ClassIDStr, ClassGuid))
		{
			Result->SetStringField(TEXT("Error"), TEXT("AddNode requires valid ClassId (GUID)"));
			return;
		}
		FMetasoundFrontendNode NewNode;
		NewNode.UpdateID(FGuid::NewGuid());
		NewNode.ClassID = ClassGuid;
		if (!NodeName.IsEmpty())
		{
			NewNode.Name = FName(*NodeName);
		}
		Doc->RootGraph.Graph.Nodes.Add(NewNode);
		Result->SetStringField(TEXT("NodeId"), NewNode.GetID().ToString());
	}
	else if (Action == TEXT("RemoveNode"))
	{
		const FString NodeIDStr = A.Str(TEXT("NodeId"));
		FGuid NodeGuid;
		if (!FGuid::Parse(NodeIDStr, NodeGuid))
		{
			Result->SetStringField(TEXT("Error"), TEXT("RemoveNode requires valid NodeId"));
			return;
		}
		const int32 RemovedNodes = Doc->RootGraph.Graph.Nodes.RemoveAll(
			[&](const FMetasoundFrontendNode& N) { return N.GetID() == NodeGuid; });
		const int32 RemovedEdges = Doc->RootGraph.Graph.Edges.RemoveAll(
			[&](const FMetasoundFrontendEdge& E) {
				return E.FromNodeID == NodeGuid || E.ToNodeID == NodeGuid;
			});
		Result->SetNumberField(TEXT("RemovedEdges"), RemovedEdges);
		if (RemovedNodes == 0)
		{
			Result->SetStringField(TEXT("Error"), FString::Printf(TEXT("Node '%s' not found"), *NodeIDStr));
		}
	}
	else if (Action == TEXT("AddEdge"))
	{
		const FString FromNodeIDStr = A.Str(TEXT("FromNodeId"));
		const FString FromPin = A.Str(TEXT("FromPin"));
		const FString ToNodeIDStr = A.Str(TEXT("ToNodeId"));
		const FString ToPin = A.Str(TEXT("ToPin"));
		if (FromNodeIDStr.IsEmpty() || FromPin.IsEmpty() || ToNodeIDStr.IsEmpty() || ToPin.IsEmpty())
		{
			Result->SetStringField(TEXT("Error"), TEXT("AddEdge requires FromNodeId/FromPin/ToNodeId/ToPin"));
			return;
		}
		FMetasoundFrontendEdge NewEdge;
		FGuid::Parse(FromNodeIDStr, NewEdge.FromNodeID);
		FGuid::Parse(ToNodeIDStr, NewEdge.ToNodeID);
		const FMetasoundFrontendNode* FromNode = FindMSNode(Doc->RootGraph.Graph, NewEdge.FromNodeID);
		const FMetasoundFrontendNode* ToNode = FindMSNode(Doc->RootGraph.Graph, NewEdge.ToNodeID);
		if (!FromNode || !ToNode)
		{
			Result->SetStringField(TEXT("Error"), TEXT("AddEdge: FromNodeId or ToNodeId not found"));
			return;
		}
		NewEdge.FromVertexID = FindOutputVertexID(*FromNode, FName(*FromPin));
		NewEdge.ToVertexID = FindInputVertexID(*ToNode, FName(*ToPin));
		if (!NewEdge.FromVertexID.IsValid() || !NewEdge.ToVertexID.IsValid())
		{
			Result->SetStringField(TEXT("Error"), TEXT("AddEdge: FromPin/ToPin not found on node"));
			return;
		}
		const bool bExists = Doc->RootGraph.Graph.Edges.ContainsByPredicate(
			[&](const FMetasoundFrontendEdge& E) {
				return E.FromNodeID == NewEdge.FromNodeID &&
				       E.FromVertexID == NewEdge.FromVertexID &&
				       E.ToNodeID == NewEdge.ToNodeID &&
				       E.ToVertexID == NewEdge.ToVertexID;
			});
		if (bExists)
		{
			Result->SetBoolField(TEXT("bAlreadyExists"), true);
		}
		else
		{
			Doc->RootGraph.Graph.Edges.Add(NewEdge);
		}
	}
	else if (Action == TEXT("RemoveEdge"))
	{
		const FString FromNodeIDStr = A.Str(TEXT("FromNodeId"));
		const FString FromPin = A.Str(TEXT("FromPin"));
		const FString ToNodeIDStr = A.Str(TEXT("ToNodeId"));
		const FString ToPin = A.Str(TEXT("ToPin"));
		FGuid FromGuid, ToGuid;
		FGuid::Parse(FromNodeIDStr, FromGuid);
		FGuid::Parse(ToNodeIDStr, ToGuid);
		const FName FromPinName(*FromPin), ToPinName(*ToPin);
		const FMetasoundFrontendNode* FromNode = FindMSNode(Doc->RootGraph.Graph, FromGuid);
		const FMetasoundFrontendNode* ToNode = FindMSNode(Doc->RootGraph.Graph, ToGuid);
		const FGuid FromVid = FromNode ? FindOutputVertexID(*FromNode, FromPinName) : FGuid();
		const FGuid ToVid = ToNode ? FindInputVertexID(*ToNode, ToPinName) : FGuid();
		const int32 Removed = Doc->RootGraph.Graph.Edges.RemoveAll(
			[&](const FMetasoundFrontendEdge& E) {
				return E.FromNodeID == FromGuid && E.FromVertexID == FromVid &&
				       E.ToNodeID == ToGuid && E.ToVertexID == ToVid;
			});
		if (Removed == 0)
		{
			Result->SetStringField(TEXT("Error"), TEXT("Edge not found"));
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

bool FNexusManageAssetMetaSoundCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);

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
	OutError = TEXT("ManageAssetMetaSound graph edit requires UE 5.3+ (NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT)");
	return false;
#endif
}

void FNexusManageAssetMetaSoundCapability::FinalizeTarget(void* Target) const
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

void FNexusManageAssetMetaSoundCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT
	OutHandlers.Add(TEXT("AddInput"),     &HandleMS_AddInput);
	OutHandlers.Add(TEXT("RemoveInput"),  &HandleMS_RemoveInput);
	OutHandlers.Add(TEXT("AddOutput"),    &HandleMS_AddOutput);
	OutHandlers.Add(TEXT("RemoveOutput"), &HandleMS_RemoveOutput);
	OutHandlers.Add(TEXT("AddNode"),      &HandleMS_AddNode);
	OutHandlers.Add(TEXT("RemoveNode"),   &HandleMS_RemoveNode);
	OutHandlers.Add(TEXT("AddEdge"),      &HandleMS_AddEdge);
	OutHandlers.Add(TEXT("RemoveEdge"),   &HandleMS_RemoveEdge);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetMetaSoundCapability)

#endif // WITH_METASOUND
