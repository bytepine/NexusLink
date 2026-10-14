// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Sequencer/NexusCreateAssetLevelSequenceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetLevelSequenceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetLevelSequence");
	Out.Description = TEXT("Create empty LevelSequence and initialize MovieScene.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("Sequence"), TEXT("sequencer"), TEXT("cinematic"), TEXT("levelsequence") };
	Out.RelatedCapabilities = { TEXT("GetAssetLevelSequence"), TEXT("ManageAssetLevelSequence") };
	Out.WhenToUse = TEXT("Create LevelSequence; add bindings/tracks/keys via manage");
}

FCapabilityResult FNexusCreateAssetLevelSequenceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<ULevelSequence>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		ULevelSequence* LS = Cast<ULevelSequence>(Created.Asset);
		if (!LS)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		LS->Initialize();

		FNexusAssetUtils::NotifyAndSaveCreated(LS->GetOutermost(), LS, AssetPath);

		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(LS);
		Entry->SetStringField(TEXT("AssetType"), TEXT("LevelSequence"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetLevelSequenceCapability)
