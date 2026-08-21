// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusCreateAssetPaperSpriteCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "NexusMcpTool.h"

void FCreateAssetPaperSpriteCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_paper_sprite");
	Out.Description = TEXT("Create PaperSprite. optional sourceTexturePath.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("sourceTexturePath"), FNexusSchema::Str(TEXT("Source Texture2D path (optional)")))
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
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UPaperSprite>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UPaperSprite* Sprite = Cast<UPaperSprite>(Created.Asset);
		if (!Sprite)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		FString TexPath;
		if (Arguments->TryGetStringField(TEXT("sourceTexturePath"), TexPath) && !TexPath.IsEmpty())
		{
			if (UTexture2D* Tex = FNexusAssetUtils::LoadAssetWithFallback<UTexture2D>(TexPath))
			{
				Sprite->SetSourceTexture(Tex);
			}
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Sprite->GetOutermost(), Sprite, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Sprite->GetName());
		Entry->SetStringField(TEXT("path"), Sprite->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetPaperSpriteCapability)
#endif
