// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/CommonUI/NexusCreateAssetCommonTextStyleCapability.h"
#if WITH_COMMON_UI
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "CommonTextBlock.h"
#include "NexusMcpTool.h"

void FCreateAssetCommonTextStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_common_text_style");
	Out.Description = TEXT("Create CommonTextStyle. WBP widget tree still uses user_widget.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("text"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_common_text_style"), TEXT("manage_asset_common_text_style"),
		TEXT("create_asset_common_button_style")
	};
}

FCapabilityResult FCreateAssetCommonTextStyleCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UCommonTextStyle>(AssetPath);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UCommonTextStyle* Style = Cast<UCommonTextStyle>(Created.Asset);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Style->GetName());
		Entry->SetStringField(TEXT("path"), Style->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetCommonTextStyleCapability)
#endif
