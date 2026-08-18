// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/PCG/NexusManageAssetPCGGraphCapability.h"

#if WITH_PCG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "UObject/UObjectGlobals.h"

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

static void ApplyPCGOperation(const TSharedPtr<FJsonObject>& Op, UPCGGraph* Graph,
	const FString& AssetPath, TArray<TSharedPtr<FJsonValue>>& OutEntries)
{
	FString Action;
	Op->TryGetStringField(TEXT("action"), Action);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("action"), Action);

	if (Action == TEXT("add_node"))
	{
		FString SettingsClassName;
		if (!Op->TryGetStringField(TEXT("settingsClass"), SettingsClassName) || SettingsClassName.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_node requires settingsClass (UPCGSettings subclass name)"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

#if NX_UE_HAS_FIND_FIRST_OBJECT
		UClass* SettingsClass = FindFirstObject<UClass>(*SettingsClassName, EFindFirstObjectOptions::NativeFirst);
#else
		UClass* SettingsClass = FindObject<UClass>(ANY_PACKAGE, *SettingsClassName);
#endif
		if (!SettingsClass || !SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
		{
			Result->SetStringField(TEXT("error"),
				FString::Printf(TEXT("settingsClass '%s' not found or not UPCGSettings subclass"), *SettingsClassName));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		UPCGSettings* NewSettings = NewObject<UPCGSettings>(Graph, SettingsClass, NAME_None, RF_Transactional);
		if (!NewSettings)
		{
			Result->SetStringField(TEXT("error"), TEXT("Failed to create PCGSettings instance"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		UPCGNode* NewNode = Graph->AddNode(NewSettings);
		if (!NewNode)
		{
			Result->SetStringField(TEXT("error"), TEXT("AddNode failed"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		Result->SetStringField(TEXT("nodeId"), NewNode->GetName());
		Result->SetStringField(TEXT("settingsClass"), SettingsClassName);
	}
	else if (Action == TEXT("remove_node"))
	{
		FString NodeId;
		Op->TryGetStringField(TEXT("nodeId"), NodeId);
		if (NodeId.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("remove_node requires nodeId"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		UPCGNode* TargetNode = nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && Node->GetName() == NodeId) { TargetNode = Node; break; }
		}
		if (!TargetNode)
		{
			Result->SetStringField(TEXT("error"),
				FString::Printf(TEXT("Node '%s' not found"), *NodeId));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		Graph->RemoveNode(TargetNode);
		Result->SetStringField(TEXT("nodeId"), NodeId);
	}
	else if (Action == TEXT("add_edge"))
	{
		FString FromId, FromPin, ToId, ToPin;
		Op->TryGetStringField(TEXT("fromNodeId"), FromId);
		Op->TryGetStringField(TEXT("fromPin"),    FromPin);
		Op->TryGetStringField(TEXT("toNodeId"),   ToId);
		Op->TryGetStringField(TEXT("toPin"),      ToPin);

		if (FromId.IsEmpty() || ToId.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_edge requires fromNodeId and toNodeId"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		UPCGNode* FromNode = nullptr;
		UPCGNode* ToNode   = nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (!Node) continue;
			if (Node->GetName() == FromId) FromNode = Node;
			if (Node->GetName() == ToId)   ToNode   = Node;
		}

		if (!FromNode || !ToNode)
		{
			Result->SetStringField(TEXT("error"), TEXT("Source or target node not found"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		const FName FromLabel = FromPin.IsEmpty() ? NAME_None : FName(*FromPin);
		const FName ToLabel   = ToPin.IsEmpty()   ? NAME_None : FName(*ToPin);
		Graph->AddEdge(FromNode, FromLabel, ToNode, ToLabel);
	}
	else if (Action == TEXT("remove_edge"))
	{
		FString FromId, FromPin, ToId, ToPin;
		Op->TryGetStringField(TEXT("fromNodeId"), FromId);
		Op->TryGetStringField(TEXT("fromPin"),    FromPin);
		Op->TryGetStringField(TEXT("toNodeId"),   ToId);
		Op->TryGetStringField(TEXT("toPin"),      ToPin);
		if (FromId.IsEmpty() || ToId.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("remove_edge requires fromNodeId and toNodeId"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		UPCGNode* FromNode = nullptr;
		UPCGNode* ToNode   = nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (!Node) continue;
			if (Node->GetName() == FromId) FromNode = Node;
			if (Node->GetName() == ToId)   ToNode   = Node;
		}
		if (!FromNode || !ToNode)
		{
			Result->SetStringField(TEXT("error"), TEXT("Source or target node not found"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		const FName FromLabel = FromPin.IsEmpty() ? NAME_None : FName(*FromPin);
		const FName ToLabel   = ToPin.IsEmpty()   ? NAME_None : FName(*ToPin);
		Graph->RemoveEdge(FromNode, FromLabel, ToNode, ToLabel);
	}
	else
	{
		Result->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Unknown action '%s'; supported: add_node/remove_node/add_edge/remove_edge"), *Action));
	}

	OutEntries.Add(MakeShared<FJsonValueObject>(Result));
}

FCapabilityResult FManageAssetPCGGraphCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UPCGGraph* Graph = FNexusAssetUtils::LoadAssetWithFallback<UPCGGraph>(AssetPath);
		if (!Graph)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("PCG Graph not found: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> OpsArr = FNexusJsonUtils::ExtractOperations(Arguments);
		if (OpsArr.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				TEXT("operations array is empty"));
			return;
		}

		for (const TSharedPtr<FJsonValue>& Val : OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Val->TryGetObject(OpObj) || !OpObj) continue;
			ApplyPCGOperation(*OpObj, Graph, AssetPath, OutEntries);
		}

		Graph->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetPCGGraphCapability)

#endif // WITH_PCG
