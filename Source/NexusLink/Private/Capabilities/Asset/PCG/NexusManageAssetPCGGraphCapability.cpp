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

void FManageAssetPCGGraphCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_pcg_graph");
	Out.SearchAssetTypes = {TEXT("PCGGraph")};
	Out.Description = TEXT("Manage PCG Graph: add_node/remove_node/add_edge/remove_edge (UE 5.4+).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("PCG Graph asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"),
			FNexusSchema::Object()
			.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
				{ TEXT("add_node"), TEXT("remove_node"), TEXT("add_edge"), TEXT("remove_edge") }))
			.Build().ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pcg"), TEXT("procedural"), TEXT("node"), TEXT("edge"), TEXT("connect") };
	Out.RelatedCapabilities = { TEXT("get_asset_pcg_graph"), TEXT("create_asset_pcg_graph") };
	Out.WhenToUse = TEXT("Add/remove PCG Graph nodes or connect pins");
}

static UPCGGraph* PCGFrom(FNexusActionContext& Ctx)
{
	return static_cast<UPCGGraph*>(Ctx.Target);
}

static UPCGNode* FindPCGNodeById(UPCGGraph* Graph, const FString& NodeId)
{
	if (!Graph) return nullptr;
	for (UPCGNode* Node : Graph->GetNodes())
	{
		if (Node && Node->GetName() == NodeId) return Node;
	}
	return nullptr;
}

static void HandlePCG_AddNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPCGGraph* Graph = PCGFrom(Ctx);
	const FString SettingsClassName = FNexusArgs(Op).Str(TEXT("settingsClass"));
	if (SettingsClassName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_node requires settingsClass (UPCGSettings subclass name)"));
		return;
	}

#if NX_UE_HAS_FIND_FIRST_OBJECT
	UClass* SettingsClass = FindFirstObject<UClass>(*SettingsClassName, EFindFirstObjectOptions::NativeFirst);
#else
	UClass* SettingsClass = FindObject<UClass>(ANY_PACKAGE, *SettingsClassName);
#endif
	if (!SettingsClass || !SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("settingsClass '%s' not found or not UPCGSettings subclass"), *SettingsClassName));
		return;
	}

	UPCGSettings* NewSettings = NewObject<UPCGSettings>(Graph, SettingsClass, NAME_None, RF_Transactional);
	if (!NewSettings)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Failed to create PCGSettings instance"));
		return;
	}

	UPCGNode* NewNode = Graph->AddNode(NewSettings);
	if (!NewNode)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("AddNode failed"));
		return;
	}

	Ctx.Entry->SetStringField(TEXT("nodeId"), NewNode->GetName());
	Ctx.Entry->SetStringField(TEXT("settingsClass"), SettingsClassName);
}

static void HandlePCG_RemoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPCGGraph* Graph = PCGFrom(Ctx);
	const FString NodeId = FNexusArgs(Op).Str(TEXT("nodeId"));
	if (NodeId.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_node requires nodeId"));
		return;
	}
	UPCGNode* TargetNode = FindPCGNodeById(Graph, NodeId);
	if (!TargetNode)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Node '%s' not found"), *NodeId));
		return;
	}
	Graph->RemoveNode(TargetNode);
	Ctx.Entry->SetStringField(TEXT("nodeId"), NodeId);
}

static bool ResolvePCGEdgeEnds(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx,
	UPCGNode*& OutFrom, UPCGNode*& OutTo, FName& OutFromLabel, FName& OutToLabel, const TCHAR* MissingErr)
{
	UPCGGraph* Graph = PCGFrom(Ctx);
	const FNexusArgs A(Op);
	const FString FromId = A.Str(TEXT("fromNodeId"));
	const FString ToId   = A.Str(TEXT("toNodeId"));
	const FString FromPin = A.Str(TEXT("fromPin"));
	const FString ToPin   = A.Str(TEXT("toPin"));
	if (FromId.IsEmpty() || ToId.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), MissingErr);
		return false;
	}
	OutFrom = FindPCGNodeById(Graph, FromId);
	OutTo   = FindPCGNodeById(Graph, ToId);
	if (!OutFrom || !OutTo)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Source or target node not found"));
		return false;
	}
	OutFromLabel = FromPin.IsEmpty() ? NAME_None : FName(*FromPin);
	OutToLabel   = ToPin.IsEmpty()   ? NAME_None : FName(*ToPin);
	return true;
}

static void HandlePCG_AddEdge(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPCGNode* FromNode = nullptr;
	UPCGNode* ToNode = nullptr;
	FName FromLabel, ToLabel;
	if (!ResolvePCGEdgeEnds(Op, Ctx, FromNode, ToNode, FromLabel, ToLabel,
		TEXT("add_edge requires fromNodeId and toNodeId")))
	{
		return;
	}
	PCGFrom(Ctx)->AddEdge(FromNode, FromLabel, ToNode, ToLabel);
}

static void HandlePCG_RemoveEdge(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPCGNode* FromNode = nullptr;
	UPCGNode* ToNode = nullptr;
	FName FromLabel, ToLabel;
	if (!ResolvePCGEdgeEnds(Op, Ctx, FromNode, ToNode, FromLabel, ToLabel,
		TEXT("remove_edge requires fromNodeId and toNodeId")))
	{
		return;
	}
	PCGFrom(Ctx)->RemoveEdge(FromNode, FromLabel, ToNode, ToLabel);
}

bool FManageAssetPCGGraphCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UPCGGraph* Graph = FNexusAssetUtils::LoadAssetWithFallback<UPCGGraph>(AssetPath);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("PCG Graph not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Graph;
	return true;
}

void FManageAssetPCGGraphCapability::FinalizeTarget(void* Target) const
{
	if (UPCGGraph* Graph = static_cast<UPCGGraph*>(Target))
	{
		Graph->MarkPackageDirty();
	}
}

void FManageAssetPCGGraphCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add_node"),    &HandlePCG_AddNode);
	OutHandlers.Add(TEXT("remove_node"), &HandlePCG_RemoveNode);
	OutHandlers.Add(TEXT("add_edge"),    &HandlePCG_AddEdge);
	OutHandlers.Add(TEXT("remove_edge"), &HandlePCG_RemoveEdge);
}

REGISTER_MCP_CAPABILITY(FManageAssetPCGGraphCapability)

#endif // WITH_PCG
