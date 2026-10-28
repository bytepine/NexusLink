// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/PCG/NexusManageAssetPCGGraphCapability.h"

#if WITH_PCG

#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

void FNexusManageAssetPCGGraphCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("ManageAssetPCGGraph");
	Out.SearchAssetTypes = {TEXT("PCGGraph")};
	Out.Description = TEXT("Manage PCG Graph: AddNode/RemoveNode/AddEdge/RemoveEdge (UE 5.4+).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("PCG Graph asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Operation list"),
			FNexusSchema::Object()
			.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
				{ TEXT("AddNode"), TEXT("RemoveNode"), TEXT("AddEdge"), TEXT("RemoveEdge") }))
			.Prop(TEXT("SettingsClass"), FNexusSchema::Str(TEXT("UPCGSettings subclass name (AddNode)")))
			.Prop(TEXT("NodeId"),        FNexusSchema::Str(TEXT("Node id (RemoveNode)")))
			.Prop(TEXT("FromNodeId"),    FNexusSchema::Str(TEXT("Edge source node id (add/RemoveEdge)")))
			.Prop(TEXT("ToNodeId"),      FNexusSchema::Str(TEXT("Edge target node id (add/RemoveEdge)")))
			.Prop(TEXT("FromPin"),       FNexusSchema::Str(TEXT("Edge source pin name, default first output pin")))
			.Prop(TEXT("ToPin"),         FNexusSchema::Str(TEXT("Edge target pin name, default first input pin")))
			.Required({ TEXT("Action") })
			.Build().ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pcg"), TEXT("procedural"), TEXT("Node"), TEXT("edge"), TEXT("Connect") };
	Out.RelatedCapabilities = { TEXT("GetAssetPCGGraph"), TEXT("CreateAssetPCGGraph") };
	Out.WhenToUse = TEXT("Add/remove PCG Graph nodes or connect pins");
}

static UPCGGraph* PCGFrom(FNexusActionContext& Ctx)
{
	return static_cast<UPCGGraph*>(Ctx.Target);
}

static UPCGNode* FindPCGNodeById(UPCGGraph* Graph, const FString& NodeId)
{
	if (!Graph) return nullptr;
	if (Graph->GetInputNode() && Graph->GetInputNode()->GetName() == NodeId) return Graph->GetInputNode();
	if (Graph->GetOutputNode() && Graph->GetOutputNode()->GetName() == NodeId) return Graph->GetOutputNode();
	for (UPCGNode* Node : Graph->GetNodes())
	{
		if (Node && Node->GetName() == NodeId) return Node;
	}
	return nullptr;
}

static void HandlePCG_AddNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPCGGraph* Graph = PCGFrom(Ctx);
	const FString SettingsClassName = FNexusArgs(Op).Str(TEXT("SettingsClass"));
	if (SettingsClassName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddNode requires SettingsClass (UPCGSettings subclass name)"));
		return;
	}

#if NX_UE_HAS_FIND_FIRST_OBJECT
	UClass* SettingsClass = FindFirstObject<UClass>(*SettingsClassName, EFindFirstObjectOptions::NativeFirst);
#else
	UClass* SettingsClass = FindObject<UClass>(ANY_PACKAGE, *SettingsClassName);
#endif
	if (!SettingsClass || !SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("SettingsClass '%s' not found or not UPCGSettings subclass"), *SettingsClassName));
		return;
	}

	UPCGSettings* NewSettings = NewObject<UPCGSettings>(Graph, SettingsClass, NAME_None, RF_Transactional);
	if (!NewSettings)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Failed to create PCGSettings instance"));
		return;
	}

	UPCGNode* NewNode = Graph->AddNode(NewSettings);
	if (!NewNode)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddNode failed"));
		return;
	}

	Ctx.Entry->SetStringField(TEXT("NodeId"), NewNode->GetName());
	Ctx.Entry->SetStringField(TEXT("SettingsClass"), SettingsClassName);
}

static void HandlePCG_RemoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPCGGraph* Graph = PCGFrom(Ctx);
	const FString NodeId = FNexusArgs(Op).Str(TEXT("NodeId"));
	if (NodeId.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveNode requires NodeId"));
		return;
	}
	UPCGNode* TargetNode = FindPCGNodeById(Graph, NodeId);
	if (!TargetNode)
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("Node '%s' not found"), *NodeId));
		return;
	}
	if (TargetNode == Graph->GetInputNode() || TargetNode == Graph->GetOutputNode()) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Built-in graph input/output nodes cannot be removed")); return; }
	Graph->RemoveNode(TargetNode);
	Ctx.Entry->SetStringField(TEXT("NodeId"), NodeId);
}

static bool ResolvePCGEdgeEnds(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx,
	UPCGNode*& OutFrom, UPCGNode*& OutTo, FName& OutFromLabel, FName& OutToLabel, const TCHAR* MissingErr)
{
	UPCGGraph* Graph = PCGFrom(Ctx);
	const FNexusArgs A(Op);
	const FString FromId = A.Str(TEXT("FromNodeId"));
	const FString ToId   = A.Str(TEXT("ToNodeId"));
	const FString FromPin = A.Str(TEXT("FromPin"));
	const FString ToPin   = A.Str(TEXT("ToPin"));
	if (FromId.IsEmpty() || ToId.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), MissingErr);
		return false;
	}
	OutFrom = FindPCGNodeById(Graph, FromId);
	OutTo   = FindPCGNodeById(Graph, ToId);
	if (!OutFrom || !OutTo)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Source or target node not found"));
		return false;
	}
	const auto Outputs = OutFrom->OutputPinProperties(), Inputs = OutTo->InputPinProperties();
	OutFromLabel = FromPin.IsEmpty() ? (Outputs.Num() ? Outputs[0].Label : NAME_None) : FName(*FromPin);
	OutToLabel = ToPin.IsEmpty() ? (Inputs.Num() ? Inputs[0].Label : NAME_None) : FName(*ToPin);
	if (!OutFrom->GetOutputPin(OutFromLabel) || !OutTo->GetInputPin(OutToLabel)) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Source output pin or destination input pin not found")); return false; }
	return true;
}

static void HandlePCG_AddEdge(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPCGNode* FromNode = nullptr;
	UPCGNode* ToNode = nullptr;
	FName FromLabel, ToLabel;
	if (!ResolvePCGEdgeEnds(Op, Ctx, FromNode, ToNode, FromLabel, ToLabel,
		TEXT("AddEdge requires FromNodeId and ToNodeId")))
	{
		return;
	}
	if (!PCGFrom(Ctx)->AddEdge(FromNode, FromLabel, ToNode, ToLabel)) Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PCG rejected the edge connection"));
}

static void HandlePCG_RemoveEdge(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPCGNode* FromNode = nullptr;
	UPCGNode* ToNode = nullptr;
	FName FromLabel, ToLabel;
	if (!ResolvePCGEdgeEnds(Op, Ctx, FromNode, ToNode, FromLabel, ToLabel,
		TEXT("RemoveEdge requires FromNodeId and ToNodeId")))
	{
		return;
	}
	PCGFrom(Ctx)->RemoveEdge(FromNode, FromLabel, ToNode, ToLabel);
}

bool FNexusManageAssetPCGGraphCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UPCGGraph* Graph = FNexusAssetUtils::LoadAssetWithFallback<UPCGGraph>(AssetPath);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("PCG Graph not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Graph;
	return true;
}

void FNexusManageAssetPCGGraphCapability::FinalizeTarget(void* Target) const
{
	if (UPCGGraph* Graph = static_cast<UPCGGraph*>(Target))
	{
		Graph->MarkPackageDirty();
	}
}

void FNexusManageAssetPCGGraphCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("AddNode"),    &HandlePCG_AddNode);
	OutHandlers.Add(TEXT("RemoveNode"), &HandlePCG_RemoveNode);
	OutHandlers.Add(TEXT("AddEdge"),    &HandlePCG_AddEdge);
	OutHandlers.Add(TEXT("RemoveEdge"), &HandlePCG_RemoveEdge);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetPCGGraphCapability)

#endif // WITH_PCG
