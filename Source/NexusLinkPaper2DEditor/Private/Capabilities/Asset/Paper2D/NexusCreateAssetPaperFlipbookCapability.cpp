// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusCreateAssetPaperFlipbookCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "PaperFlipbook.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetPaperFlipbookCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetPaperFlipbook");
	Out.Description = TEXT("Create PaperFlipbook. Add frames via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("flipbook"), TEXT("2d"), TEXT("Sprite") };
	Out.RelatedCapabilities = { TEXT("GetAssetPaperFlipbook"), TEXT("ManageAssetPaperFlipbook") };
}

FCapabilityResult FNexusCreateAssetPaperFlipbookCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UPaperFlipbook>(AssetPath);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UPaperFlipbook* Book = Cast<UPaperFlipbook>(Created.Asset);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, Book);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetPaperFlipbookCapability)
#endif
