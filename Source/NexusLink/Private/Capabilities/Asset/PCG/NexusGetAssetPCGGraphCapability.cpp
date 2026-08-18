// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/PCG/NexusGetAssetPCGGraphCapability.h"

#if WITH_PCG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "PCGGraph.h"
#include "PCGNode.h"

void FGetAssetPCGGraphCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("get_asset_pcg_graph");
	Out.SearchAssetTypes = {TEXT("PCGGraph")};
	Out.Description = TEXT("Read PCG Graph node list and pin overview. Use manage_asset_pcg_graph for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("PCG Graph asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pcg"), TEXT("procedural"), TEXT("generation"), TEXT("node"), TEXT("graph") };
	Out.RelatedCapabilities = { TEXT("manage_asset_pcg_graph"), TEXT("create_asset_pcg_graph"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("Read PCG Graph node structure; use manage_asset_pcg_graph for writes");
}

FCapabilityResult FGetAssetPCGGraphCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));

		UPCGGraph* Graph = FNexusAssetUtils::LoadAssetWithFallback<UPCGGraph>(AssetPath);
		if (!Graph)
		{
			TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
			ErrObj->SetStringField(TEXT("path"), AssetPath);
			ErrObj->SetStringField(TEXT("error"), TEXT("PCG Graph not found"));
			OutEntries.Add(MakeShared<FJsonValueObject>(ErrObj));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Graph->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("PCGGraph"));
		Entry->SetStringField(TEXT("name"),      Graph->GetName());

		// 节点列表
		const TArray<UPCGNode*>& Nodes = Graph->GetNodes();
		TArray<TSharedPtr<FJsonValue>> NodesArr;
		for (const UPCGNode* Node : Nodes)
		{
			if (!Node) continue;
			TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
			NObj->SetStringField(TEXT("id"),    Node->GetName());
			NObj->SetStringField(TEXT("title"), Node->GetNodeTitle(EPCGNodeTitleType::FullTitle).ToString());

			if (const UPCGSettings* Settings = Node->GetSettings())
				NObj->SetStringField(TEXT("settingsClass"), Settings->GetClass()->GetName());

			// 输入/输出 pin
			TArray<FPCGPinProperties> InPins  = Node->InputPinProperties();
			TArray<FPCGPinProperties> OutPins = Node->OutputPinProperties();
			TArray<TSharedPtr<FJsonValue>> InArr, OutArr;
			for (const FPCGPinProperties& P : InPins)
				InArr.Add(MakeShared<FJsonValueString>(P.Label.ToString()));
			for (const FPCGPinProperties& P : OutPins)
				OutArr.Add(MakeShared<FJsonValueString>(P.Label.ToString()));
			NObj->SetArrayField(TEXT("inputPins"),  InArr);
			NObj->SetArrayField(TEXT("outputPins"), OutArr);

			NodesArr.Add(MakeShared<FJsonValueObject>(NObj));
		}
		Entry->SetArrayField(TEXT("nodes"), NodesArr);
		Entry->SetNumberField(TEXT("nodeCount"), Nodes.Num());

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetPCGGraphCapability)

#endif // WITH_PCG
