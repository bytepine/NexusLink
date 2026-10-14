// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/CommonUI/NexusGetAssetCommonButtonStyleCapability.h"
#if WITH_COMMON_UI
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "CommonButtonBase.h"
#include "NexusMcpTool.h"

void FNexusGetAssetCommonButtonStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetCommonButtonStyle");
	Out.SearchAssetTypes = {TEXT("CommonButtonStyle")};
	Out.Description = TEXT("Read CommonButtonStyle metadata. WBP still uses user_widget.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("CommonButtonStyle asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("button"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("ManageAssetCommonButtonStyle"), TEXT("CreateAssetCommonButtonStyle"),
		TEXT("GetAssetCommonTextStyle")
	};
}

FCapabilityResult FNexusGetAssetCommonButtonStyleCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;
		UCommonButtonStyle* Style = FNexusAssetUtils::LoadAssetOrBlueprintCDO<UCommonButtonStyle>(AssetPath);
		if (!Style)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Failed to load CommonButtonStyle: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Style->GetName());
		Entry->SetStringField(TEXT("Path"), Style->GetPathName());
		Entry->SetStringField(TEXT("Class"), Style->GetClass()->GetName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetCommonButtonStyleCapability)
#endif
