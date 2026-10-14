// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusCreateAssetBehaviorTreeCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "BehaviorTree/BehaviorTree.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetBehaviorTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetBehaviorTree");
	Out.Description = TEXT("Create empty BT. Link BB via manage SetBlackboard then add nodes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("BehaviorTree package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("bt"), TEXT("new"), TEXT("behaviortree"), TEXT("ai"), TEXT("task") };
	Out.RelatedCapabilities = { TEXT("ManageAssetBehaviorTree"), TEXT("CreateAssetBlackboard") };
	Out.WhenToUse = TEXT("Create empty BT; no nodes, no linked BB");
}

FCapabilityResult FNexusCreateAssetBehaviorTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString BTPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UBehaviorTree>(BTPath);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UBehaviorTree* BT = Cast<UBehaviorTree>(Created.Asset);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, BT);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetBehaviorTreeCapability)
