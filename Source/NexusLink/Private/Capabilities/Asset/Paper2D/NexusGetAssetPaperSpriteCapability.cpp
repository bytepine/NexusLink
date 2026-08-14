// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusGetAssetPaperSpriteCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "PaperSprite.h"
#include "NexusMcpTool.h"

void FGetAssetPaperSpriteCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_paper_sprite");
	Out.SearchAssetTypes = {TEXT("PaperSprite")};
	Out.Description = TEXT("读取 PaperSprite：源纹理 / 像素区域 / 轴心。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("PaperSprite 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("sprite"), TEXT("2d"), TEXT("pivot") };
	Out.RelatedCapabilities = { TEXT("manage_asset_paper_sprite"), TEXT("create_asset_paper_sprite") };
}

FCapabilityResult FGetAssetPaperSpriteCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		UPaperSprite* Sprite = FNexusAssetUtils::LoadAssetWithFallback<UPaperSprite>(AssetPath);
		if (!Sprite)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载 PaperSprite 失败: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Sprite->GetName());
		Entry->SetStringField(TEXT("path"), Sprite->GetPathName());
		UTexture2D* Tex = Sprite->GetSourceTexture();
		Entry->SetStringField(TEXT("sourceTexture"), Tex ? Tex->GetPathName() : FString());
		const FVector2D Dim = Sprite->GetSourceSize();
		Entry->SetNumberField(TEXT("sourceWidth"), Dim.X);
		Entry->SetNumberField(TEXT("sourceHeight"), Dim.Y);
		const FVector2D Pivot = Sprite->GetPivotPosition();
		Entry->SetNumberField(TEXT("pivotX"), Pivot.X);
		Entry->SetNumberField(TEXT("pivotY"), Pivot.Y);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetPaperSpriteCapability)
#endif
