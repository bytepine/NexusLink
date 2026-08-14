// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusCreateAssetPaperFlipbookCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "PaperFlipbook.h"
#include "NexusMcpTool.h"

void FCreateAssetPaperFlipbookCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_paper_flipbook");
	Out.Description = TEXT("创建 PaperFlipbook。用 manage 加帧。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("flipbook"), TEXT("2d"), TEXT("sprite") };
	Out.RelatedCapabilities = { TEXT("get_asset_paper_flipbook"), TEXT("manage_asset_paper_flipbook") };
}

FCapabilityResult FCreateAssetPaperFlipbookCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UPaperFlipbook>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("PaperFlipbook already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UPaperFlipbook* Book = NewObject<UPaperFlipbook>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Book) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败")); return; }
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Book, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Book->GetName());
		Entry->SetStringField(TEXT("path"), Book->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetPaperFlipbookCapability)
#endif
