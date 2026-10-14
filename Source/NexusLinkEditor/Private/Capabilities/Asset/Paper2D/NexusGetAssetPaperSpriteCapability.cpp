// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusGetAssetPaperSpriteCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "PaperSprite.h"
#include "NexusMcpTool.h"

void FNexusGetAssetPaperSpriteCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetPaperSprite");
	Out.SearchAssetTypes = {TEXT("PaperSprite")};
	Out.Description = TEXT("Read PaperSprite: source texture/pixel region/pivot.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("PaperSprite asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("Sprite"), TEXT("2d"), TEXT("pivot") };
	Out.RelatedCapabilities = { TEXT("ManageAssetPaperSprite"), TEXT("CreateAssetPaperSprite") };
}

FCapabilityResult FNexusGetAssetPaperSpriteCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;
		UPaperSprite* Sprite = FNexusAssetUtils::LoadAssetWithFallback<UPaperSprite>(AssetPath);
		if (!Sprite)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Failed to load PaperSprite: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Sprite->GetName());
		Entry->SetStringField(TEXT("Path"), Sprite->GetPathName());
		UTexture2D* Tex = Sprite->GetSourceTexture();
		Entry->SetStringField(TEXT("SourceTexture"), Tex ? Tex->GetPathName() : FString());
		const FVector2D Dim = Sprite->GetSourceSize();
		Entry->SetNumberField(TEXT("SourceWidth"), Dim.X);
		Entry->SetNumberField(TEXT("SourceHeight"), Dim.Y);
		const FVector2D Pivot = Sprite->GetPivotPosition();
		Entry->SetNumberField(TEXT("PivotX"), Pivot.X);
		Entry->SetNumberField(TEXT("PivotY"), Pivot.Y);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetPaperSpriteCapability)
#endif
