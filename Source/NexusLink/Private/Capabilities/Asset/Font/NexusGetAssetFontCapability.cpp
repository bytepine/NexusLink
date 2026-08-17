// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Font/NexusGetAssetFontCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Engine/Font.h"
#include "NexusMcpTool.h"

void FGetAssetFontCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_font");
	Out.SearchAssetTypes = {TEXT("Font")};
	Out.Description = TEXT("读取 Font：字号/字符数/缓存类型。空白字体请用 reimport_asset 导入 TTF。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Font 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("typeface"), TEXT("ttf"), TEXT("otf"), TEXT("glyph") };
	Out.RelatedCapabilities = { TEXT("manage_asset_font"), TEXT("create_asset_font"), TEXT("reimport_asset") };
}

FCapabilityResult FGetAssetFontCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UFont* Font = FNexusAssetUtils::LoadAssetWithFallback<UFont>(AssetPath);
		if (!Font)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载 Font 失败: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Font->GetName());
		Entry->SetStringField(TEXT("path"), Font->GetPathName());
		Entry->SetNumberField(TEXT("scalingFactor"), Font->ScalingFactor);
		Entry->SetNumberField(TEXT("legacyFontSize"), Font->LegacyFontSize);
		Entry->SetNumberField(TEXT("characterCount"), Font->Characters.Num());
		Entry->SetNumberField(TEXT("textureCount"), Font->Textures.Num());
		Entry->SetStringField(TEXT("cacheType"),
			Font->FontCacheType == EFontCacheType::Runtime ? TEXT("Runtime") : TEXT("Offline"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetFontCapability)
