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

void FCreateAssetStateTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_state_tree");
	Out.Description = TEXT("Create empty StateTree. UE 5.5+.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write };
	Out.ExtraSearchKeywords = { TEXT("statetree"), TEXT("state"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("get_asset_state_tree"), TEXT("manage_asset_state_tree") };
	Out.WhenToUse = TEXT("Create StateTree; structure via manage_asset_state_tree");
}

FCapabilityResult FCreateAssetStateTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("create_asset_state_tree only available in editor builds");
		return;
#else
		const FString AssetPath = A.Str(TEXT("assetPath"));
		if (LoadObject<UStateTree>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("StateTree already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UStateTree* ST = NewObject<UStateTree>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!ST) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Creation failed")); return; }
		UStateTreeEditorData* Ed = NewObject<UStateTreeEditorData>(ST, NAME_None, RF_Transactional);
		ST->EditorData = Ed;
		FNexusAssetUtils::NotifyAndSaveCreated(Package, ST, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), ST->GetName());
		Entry->SetStringField(TEXT("path"), ST->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetStateTreeCapability)

#endif
