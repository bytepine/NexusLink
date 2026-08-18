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

void FGetAssetBehaviorTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_behavior_tree");
	Out.SearchAssetTypes = {TEXT("BehaviorTree")};
	Out.Description = TEXT("Inspect BT structure snapshot. Path index and node props; read-only.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("BehaviorTree asset path")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("bt"), TEXT("behaviortree"), TEXT("blackboard"), TEXT("node"), TEXT("task") };
	Out.RelatedCapabilities = { TEXT("manage_asset_behavior_tree"), TEXT("create_asset_behavior_tree"), TEXT("get_asset_blackboard") };
	Out.WhenToUse = TEXT("Read BT path index/properties/decorator params");
}

FCapabilityResult FGetAssetBehaviorTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("section"), TEXT("overview"));

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutEntry->SetStringField(TEXT("error"), TEXT("assetPath is required"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		UBehaviorTree* BT = FNexusAssetUtils::LoadAssetWithFallback<UBehaviorTree>(AssetPath);
		if (!BT)
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		OutEntry->SetStringField(TEXT("name"), BT->GetName());

		if (BT->BlackboardAsset)
		{
			OutEntry->SetStringField(TEXT("blackboard"), BT->BlackboardAsset->GetPathName());
		}

		if (BT->RootNode)
		{
			TSharedPtr<FJsonObject> RootInfo = FNexusBehaviorTreeInspectUtils::BuildBTNodeInfo(BT->RootNode);
			if (RootInfo.IsValid())
			{
				OutEntry->SetObjectField(TEXT("rootNode"), RootInfo);
			}
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetBehaviorTreeCapability)
