// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/CommonUI/NexusGetAssetCommonTextStyleCapability.h"
#if WITH_COMMON_UI
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "CommonTextBlock.h"
#include "NexusMcpTool.h"

void FNexusGetAssetCommonTextStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetCommonTextStyle");
	Out.SearchAssetTypes = {TEXT("CommonTextStyle")};
	Out.Description = TEXT("Read CommonTextStyle metadata. WBP still uses user_widget.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("CommonTextStyle asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("text"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("ManageAssetCommonTextStyle"), TEXT("CreateAssetCommonTextStyle"),
		TEXT("GetAssetCommonButtonStyle")
	};
}

FCapabilityResult FNexusGetAssetCommonTextStyleCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;
		UCommonTextStyle* Style = FNexusAssetUtils::LoadAssetOrBlueprintCDO<UCommonTextStyle>(AssetPath);
		if (!Style)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Failed to load CommonTextStyle: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Style->GetName());
		Entry->SetStringField(TEXT("Path"), Style->GetPathName());
		Entry->SetStringField(TEXT("Class"), Style->GetClass()->GetName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetCommonTextStyleCapability)
#endif
