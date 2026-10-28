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

void FNexusGetAssetPCGGraphCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("GetAssetPCGGraph");
	Out.SearchAssetTypes = {TEXT("PCGGraph")};
	Out.Description = TEXT("Read PCG Graph node list and pin overview. Use ManageAssetPCGGraph for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("PCG Graph asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pcg"), TEXT("procedural"), TEXT("generation"), TEXT("Node"), TEXT("graph") };
	Out.RelatedCapabilities = { TEXT("ManageAssetPCGGraph"), TEXT("CreateAssetPCGGraph"), TEXT("SearchAsset") };
	Out.WhenToUse = TEXT("Read PCG Graph node structure; use ManageAssetPCGGraph for writes");
}

FCapabilityResult FNexusGetAssetPCGGraphCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));

		UPCGGraph* Graph = FNexusAssetUtils::LoadAssetWithFallback<UPCGGraph>(AssetPath);
		if (!Graph)
		{
			TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
			ErrObj->SetStringField(TEXT("Path"), AssetPath);
			ErrObj->SetStringField(TEXT("Error"), TEXT("PCG Graph not found"));
			OutEntries.Add(MakeShared<FJsonValueObject>(ErrObj));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Graph->GetPathName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("PCGGraph"));
		Entry->SetStringField(TEXT("name"),      Graph->GetName());

		// 节点列表
		const TArray<UPCGNode*>& Nodes = Graph->GetNodes();
		TArray<TSharedPtr<FJsonValue>> NodesArr;
		auto NodeInfo = [](const UPCGNode* Node)
		{
			TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
			NObj->SetStringField(TEXT("Id"),    Node->GetName());
			NObj->SetStringField(TEXT("Path"), Node->GetPathName());
			NObj->SetStringField(TEXT("Title"), Node->GetNodeTitle(EPCGNodeTitleType::FullTitle).ToString());

			if (const UPCGSettings* Settings = Node->GetSettings())
				NObj->SetStringField(TEXT("SettingsClass"), Settings->GetClass()->GetName());

			// 输入/输出 pin
			TArray<FPCGPinProperties> InPins  = Node->InputPinProperties();
			TArray<FPCGPinProperties> OutPins = Node->OutputPinProperties();
			TArray<TSharedPtr<FJsonValue>> InArr, OutArr;
			for (const FPCGPinProperties& P : InPins)
				InArr.Add(MakeShared<FJsonValueString>(P.Label.ToString()));
			for (const FPCGPinProperties& P : OutPins)
				OutArr.Add(MakeShared<FJsonValueString>(P.Label.ToString()));
			NObj->SetArrayField(TEXT("InputPins"),  InArr);
			NObj->SetArrayField(TEXT("OutputPins"), OutArr);

			return NObj;
		};
		for (const UPCGNode* Node : Nodes) if (Node) NodesArr.Add(MakeShared<FJsonValueObject>(NodeInfo(Node)));
		Entry->SetArrayField(TEXT("Nodes"), NodesArr);
		Entry->SetNumberField(TEXT("NodeCount"), Nodes.Num());
		if (Graph->GetInputNode()) Entry->SetObjectField(TEXT("InputNode"), NodeInfo(Graph->GetInputNode()));
		if (Graph->GetOutputNode()) Entry->SetObjectField(TEXT("OutputNode"), NodeInfo(Graph->GetOutputNode()));

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetPCGGraphCapability)

#endif // WITH_PCG
