// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Font/NexusCreateAssetFontCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Engine/Font.h"
#include "NexusMcpTool.h"

void FCreateAssetFontCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_font");
	Out.Description = TEXT("创建空白 Font 资产（默认 Runtime 缓存；字形数据需后续导入/编辑）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Font 包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("font"), TEXT("typeface"), TEXT("ttf") };
	Out.RelatedCapabilities = { TEXT("get_asset_font"), TEXT("manage_asset_font"), TEXT("reimport_asset") };
	Out.WhenToUse = TEXT("从零建 Font 壳；属性用 manage_asset_font，字形走 reimport");
}

FCapabilityResult FCreateAssetFontCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UFont>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Font already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UFont* Font = NewObject<UFont>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Font) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Font 创建失败")); return; }
		Font->FontCacheType = EFontCacheType::Runtime;
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Font, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Font->GetName());
		Entry->SetStringField(TEXT("path"), Font->GetPathName());
		Entry->SetStringField(TEXT("fontCacheType"), TEXT("Runtime"));
		Entry->SetStringField(TEXT("note"), TEXT("空白 Font；字形需导入或编辑器填充"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetFontCapability)
