// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Font/NexusCreateAssetFontCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Engine/Font.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetFontCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetFont");
	Out.Description = TEXT("Create empty Font asset (Runtime cache default; glyphs need import/edit).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Font package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("font"), TEXT("typeface"), TEXT("ttf") };
	Out.RelatedCapabilities = { TEXT("GetAssetFont"), TEXT("ManageAssetFont"), TEXT("ReimportAsset") };
	Out.WhenToUse = TEXT("Create Font shell; props via ManageAssetFont, glyphs via reimport");
}

FCapabilityResult FNexusCreateAssetFontCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UFont>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UFont* Font = Cast<UFont>(Created.Asset);
		if (!Font)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		Font->FontCacheType = EFontCacheType::Runtime;
		FNexusAssetUtils::NotifyAndSaveCreated(Font->GetOutermost(), Font, AssetPath);
		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(Font);
		Entry->SetStringField(TEXT("FontCacheType"), TEXT("Runtime"));
		Entry->SetStringField(TEXT("Note"), TEXT("Empty Font; glyphs need import or editor fill"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetFontCapability)
