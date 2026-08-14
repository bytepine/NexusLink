// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusCreateAssetPaperSpriteCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "NexusMcpTool.h"

void FCreateAssetPaperSpriteCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_paper_sprite");
	Out.Description = TEXT("创建 PaperSprite。可选 sourceTexturePath。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Prop(TEXT("sourceTexturePath"), FNexusSchema::Str(TEXT("源 Texture2D 路径（可选）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("sprite"), TEXT("2d"), TEXT("paper") };
	Out.RelatedCapabilities = { TEXT("get_asset_paper_sprite"), TEXT("manage_asset_paper_sprite") };
}

FCapabilityResult FCreateAssetPaperSpriteCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UPaperSprite>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("PaperSprite already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UPaperSprite* Sprite = NewObject<UPaperSprite>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Sprite) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败")); return; }
		FString TexPath;
		if (Arguments->TryGetStringField(TEXT("sourceTexturePath"), TexPath) && !TexPath.IsEmpty())
		{
			if (UTexture2D* Tex = FNexusAssetUtils::LoadAssetWithFallback<UTexture2D>(TexPath))
			{
				Sprite->SetSourceTexture(Tex);
			}
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Sprite, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Sprite->GetName());
		Entry->SetStringField(TEXT("path"), Sprite->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetPaperSpriteCapability)
#endif
