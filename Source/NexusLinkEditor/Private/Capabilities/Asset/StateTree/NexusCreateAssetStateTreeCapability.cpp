// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/StateTree/NexusCreateAssetStateTreeCapability.h"

#if WITH_STATETREE

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "StateTree.h"
#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "StateTreeEditorData.h"
#endif

void FNexusCreateAssetStateTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetStateTree");
	Out.Description = TEXT("Create empty StateTree. UE 5.5+.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write };
	Out.ExtraSearchKeywords = { TEXT("statetree"), TEXT("State"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("GetAssetStateTree"), TEXT("ManageAssetStateTree") };
	Out.WhenToUse = TEXT("Create StateTree; structure via ManageAssetStateTree");
}

FCapabilityResult FNexusCreateAssetStateTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("CreateAssetStateTree only available in editor builds");
		return;
#else
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UStateTree>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UStateTree* ST = Cast<UStateTree>(Created.Asset);
		if (!ST)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		UStateTreeEditorData* Ed = NewObject<UStateTreeEditorData>(ST, NAME_None, RF_Transactional);
		ST->EditorData = Ed;
		FNexusAssetUtils::NotifyAndSaveCreated(ST->GetOutermost(), ST, AssetPath);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, ST);
#endif
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetStateTreeCapability)

#endif
