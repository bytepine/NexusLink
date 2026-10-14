// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusCreateAssetPaperSpriteCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "PaperSprite.h"
#include "SpriteEditorOnlyTypes.h"
#include "Engine/Texture2D.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetPaperSpriteCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetPaperSprite");
	Out.Description = TEXT("Create PaperSprite. optional SourceTexturePath.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("SourceTexturePath"), FNexusSchema::Str(TEXT("Source Texture2D path (optional)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("Sprite"), TEXT("2d"), TEXT("paper") };
	Out.RelatedCapabilities = { TEXT("GetAssetPaperSprite"), TEXT("ManageAssetPaperSprite") };
}

FCapabilityResult FNexusCreateAssetPaperSpriteCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
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
		if (Arguments->TryGetStringField(TEXT("SourceTexturePath"), TexPath) && !TexPath.IsEmpty())
		{
			if (UTexture2D* Tex = FNexusAssetUtils::LoadAssetWithFallback<UTexture2D>(TexPath))
			{
				FSpriteAssetInitParameters Init;
				Init.SetTextureAndFill(Tex);
				Sprite->InitializeSprite(Init);
			}
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Sprite->GetOutermost(), Sprite, AssetPath);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, Sprite);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetPaperSpriteCapability)
#endif
