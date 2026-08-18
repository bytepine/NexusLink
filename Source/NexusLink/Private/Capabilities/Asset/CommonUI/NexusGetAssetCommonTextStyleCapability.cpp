// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/CommonUI/NexusGetAssetCommonTextStyleCapability.h"
#if WITH_COMMON_UI
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "CommonTextBlock.h"
#include "NexusMcpTool.h"

void FGetAssetCommonTextStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_common_text_style");
	Out.SearchAssetTypes = {TEXT("CommonTextStyle")};
	Out.Description = TEXT("Read CommonTextStyle metadata. WBP still uses user_widget.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("CommonTextStyle asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("text"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("manage_asset_common_text_style"), TEXT("create_asset_common_text_style"),
		TEXT("get_asset_common_button_style")
	};
}

FCapabilityResult FGetAssetCommonTextStyleCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		UCommonTextStyle* Style = FNexusAssetUtils::LoadAssetWithFallback<UCommonTextStyle>(AssetPath);
		if (!Style)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("Failed to load CommonTextStyle: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Style->GetName());
		Entry->SetStringField(TEXT("path"), Style->GetPathName());
		Entry->SetStringField(TEXT("class"), Style->GetClass()->GetName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetCommonTextStyleCapability)
#endif
