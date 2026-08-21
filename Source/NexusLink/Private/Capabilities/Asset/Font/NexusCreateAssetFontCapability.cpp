// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Font/NexusCreateAssetFontCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Engine/Font.h"
#include "NexusMcpTool.h"

void FCreateAssetFontCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_font");
	Out.Description = TEXT("Create empty Font asset (Runtime cache default; glyphs need import/edit).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Font package path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("font"), TEXT("typeface"), TEXT("ttf") };
	Out.RelatedCapabilities = { TEXT("get_asset_font"), TEXT("manage_asset_font"), TEXT("reimport_asset") };
	Out.WhenToUse = TEXT("Create Font shell; props via manage_asset_font, glyphs via reimport");
}

FCapabilityResult FCreateAssetFontCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
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
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Font->GetName());
		Entry->SetStringField(TEXT("path"), Font->GetPathName());
		Entry->SetStringField(TEXT("fontCacheType"), TEXT("Runtime"));
		Entry->SetStringField(TEXT("note"), TEXT("Empty Font; glyphs need import or editor fill"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetFontCapability)
