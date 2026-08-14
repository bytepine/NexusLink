// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/CommonUI/NexusCreateAssetCommonButtonStyleCapability.h"
#if WITH_COMMON_UI
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "CommonButtonBase.h"
#include "NexusMcpTool.h"

void FCreateAssetCommonButtonStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_common_button_style");
	Out.Description = TEXT("创建 CommonButtonStyle。WBP 控件树仍走 user_widget。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("button"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_common_button_style"), TEXT("manage_asset_common_button_style"),
		TEXT("create_asset_common_text_style")
	};
}

FCapabilityResult FCreateAssetCommonButtonStyleCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UCommonButtonStyle>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("CommonButtonStyle already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UCommonButtonStyle* Style = NewObject<UCommonButtonStyle>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Style) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败")); return; }
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Style, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Style->GetName());
		Entry->SetStringField(TEXT("path"), Style->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetCommonButtonStyleCapability)
#endif
