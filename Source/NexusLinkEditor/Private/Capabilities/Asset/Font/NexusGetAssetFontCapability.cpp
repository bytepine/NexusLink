// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Font/NexusGetAssetFontCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Engine/Font.h"
#include "NexusMcpTool.h"

void FNexusGetAssetFontCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetFont");
	Out.SearchAssetTypes = {TEXT("Font")};
	Out.Description = TEXT("Read Font: size/char count/cache type. Use ReimportAsset for empty fonts.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Font asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("typeface"), TEXT("ttf"), TEXT("otf"), TEXT("glyph") };
	Out.RelatedCapabilities = { TEXT("ManageAssetFont"), TEXT("CreateAssetFont"), TEXT("ReimportAsset") };
}

FCapabilityResult FNexusGetAssetFontCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;

		UFont* Font = FNexusAssetUtils::LoadAssetWithFallback<UFont>(AssetPath);
		if (!Font)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Failed to load Font: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Font->GetName());
		Entry->SetStringField(TEXT("Path"), Font->GetPathName());
		Entry->SetNumberField(TEXT("ScalingFactor"), Font->ScalingFactor);
		Entry->SetNumberField(TEXT("LegacyFontSize"), Font->LegacyFontSize);
		Entry->SetNumberField(TEXT("CharacterCount"), Font->Characters.Num());
		Entry->SetNumberField(TEXT("TextureCount"), Font->Textures.Num());
		Entry->SetStringField(TEXT("CacheType"),
			Font->FontCacheType == EFontCacheType::Runtime ? TEXT("Runtime") : TEXT("Offline"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetFontCapability)
