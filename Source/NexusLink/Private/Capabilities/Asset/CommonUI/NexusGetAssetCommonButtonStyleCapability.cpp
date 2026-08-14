// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/CommonUI/NexusGetAssetCommonButtonStyleCapability.h"
#if WITH_COMMON_UI
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "CommonButtonBase.h"
#include "NexusMcpTool.h"

void FGetAssetCommonButtonStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_common_button_style");
	Out.SearchAssetTypes = {TEXT("CommonButtonStyle")};
	Out.Description = TEXT("读取 CommonButtonStyle 元数据。WBP 仍走 user_widget。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("CommonButtonStyle 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("button"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("manage_asset_common_button_style"), TEXT("create_asset_common_button_style"),
		TEXT("get_asset_common_text_style")
	};
}

FCapabilityResult FGetAssetCommonButtonStyleCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		UCommonButtonStyle* Style = FNexusAssetUtils::LoadAssetWithFallback<UCommonButtonStyle>(AssetPath);
		if (!Style)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载 CommonButtonStyle 失败: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Style->GetName());
		Entry->SetStringField(TEXT("path"), Style->GetPathName());
		Entry->SetStringField(TEXT("class"), Style->GetClass()->GetName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetCommonButtonStyleCapability)
#endif
