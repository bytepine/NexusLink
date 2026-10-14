// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusGetAssetBehaviorTreeCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusBehaviorTreeInspectUtils.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "NexusMcpTool.h"

void FNexusGetAssetBehaviorTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetBehaviorTree");
	Out.SearchAssetTypes = {TEXT("BehaviorTree")};
	Out.Description = TEXT("Inspect BT structure snapshot. Path index and node props; read-only.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("BehaviorTree asset path")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("bt"), TEXT("behaviortree"), TEXT("Blackboard"), TEXT("Node"), TEXT("task") };
	Out.RelatedCapabilities = { TEXT("ManageAssetBehaviorTree"), TEXT("CreateAssetBehaviorTree"), TEXT("GetAssetBlackboard") };
	Out.WhenToUse = TEXT("Read BT path index/properties/decorator params");
}

FCapabilityResult FNexusGetAssetBehaviorTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("Section"), TEXT("overview"));

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("AssetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutEntry->SetStringField(TEXT("Error"), TEXT("AssetPath is required"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		UBehaviorTree* BT = FNexusAssetUtils::LoadAssetWithFallback<UBehaviorTree>(AssetPath);
		if (!BT)
		{
			OutEntry->SetStringField(TEXT("Error"), FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		OutEntry->SetStringField(TEXT("name"), BT->GetName());

		if (BT->BlackboardAsset)
		{
			OutEntry->SetStringField(TEXT("Blackboard"), BT->BlackboardAsset->GetPathName());
		}

		if (BT->RootNode)
		{
			TSharedPtr<FJsonObject> RootInfo = FNexusBehaviorTreeInspectUtils::BuildBTNodeInfo(BT->RootNode);
			if (RootInfo.IsValid())
			{
				OutEntry->SetObjectField(TEXT("RootNode"), RootInfo);
			}
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetBehaviorTreeCapability)
