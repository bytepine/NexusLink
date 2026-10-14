// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusCreateAssetBlackboardCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "BehaviorTree/BlackboardData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetBlackboardCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetBlackboard");
	Out.Description = TEXT("Create keyless BB. Link via manage BT SetBlackboard.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("BlackboardData package path, e.g. '/Game/AI/BB_Enemy'")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("bb"), TEXT("new"), TEXT("ai"), TEXT("Blackboard"), TEXT("Keys") };
	Out.RelatedCapabilities = { TEXT("ManageAssetBlackboard"), TEXT("GetAssetBlackboard"), TEXT("CreateAssetBehaviorTree") };
	Out.WhenToUse = TEXT("Create empty BB; add keys via ManageAssetBlackboard");
}

FCapabilityResult FNexusCreateAssetBlackboardCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UBlackboardData>(AssetPath);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UBlackboardData* BB = Cast<UBlackboardData>(Created.Asset);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, BB);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetBlackboardCapability)
