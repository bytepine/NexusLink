// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/CommonUI/NexusCreateAssetCommonButtonStyleCapability.h"
#if WITH_COMMON_UI
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "CommonButtonBase.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetCommonButtonStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetCommonButtonStyle");
	Out.Description = TEXT("Create CommonButtonStyle. WBP widget tree still uses user_widget.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("button"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("GetAssetCommonButtonStyle"), TEXT("ManageAssetCommonButtonStyle"),
		TEXT("CreateAssetCommonTextStyle")
	};
}

FCapabilityResult FNexusCreateAssetCommonButtonStyleCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		// UCommonButtonStyle 是 UCLASS(Abstract, Blueprintable)：只能建 Blueprint 子类，
		// 直接 NewObject 会在 StaticAllocateObject 触发抽象类 ensure。
		const FNexusAssetEditorUtils::FAssetCreateOutcome Created =
			FNexusAssetEditorUtils::CreateBlueprintAsset(
				AssetPath, TEXT("CommonButtonStyle"), UCommonButtonStyle::StaticClass());
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UObject* Style = Created.Asset;
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, Style);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetCommonButtonStyleCapability)
#endif
