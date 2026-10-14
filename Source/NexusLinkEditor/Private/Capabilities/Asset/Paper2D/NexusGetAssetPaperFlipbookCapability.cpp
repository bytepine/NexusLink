// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusGetAssetPaperFlipbookCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "NexusMcpTool.h"

void FNexusGetAssetPaperFlipbookCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetPaperFlipbook");
	Out.SearchAssetTypes = {TEXT("PaperFlipbook")};
	Out.Description = TEXT("Read PaperFlipbook: frame rate/keyframe summary.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("PaperFlipbook asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("flipbook"), TEXT("fps"), TEXT("frame") };
	Out.RelatedCapabilities = { TEXT("ManageAssetPaperFlipbook"), TEXT("CreateAssetPaperFlipbook") };
}

FCapabilityResult FNexusGetAssetPaperFlipbookCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;
		UPaperFlipbook* Book = FNexusAssetUtils::LoadAssetWithFallback<UPaperFlipbook>(AssetPath);
		if (!Book)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Failed to load PaperFlipbook: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Book->GetName());
		Entry->SetStringField(TEXT("Path"), Book->GetPathName());
		Entry->SetNumberField(TEXT("FramesPerSecond"), Book->GetFramesPerSecond());
		Entry->SetNumberField(TEXT("KeyCount"), Book->GetNumKeyFrames());
		TArray<TSharedPtr<FJsonValue>> Keys;
		const int32 N = Book->GetNumKeyFrames();
		for (int32 i = 0; i < N && i < 50; ++i)
		{
			const FPaperFlipbookKeyFrame& Kf = Book->GetKeyFrameChecked(i);
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("Index"), i);
			Row->SetNumberField(TEXT("FrameRun"), Kf.FrameRun);
			Row->SetStringField(TEXT("Sprite"), Kf.Sprite ? Kf.Sprite->GetPathName() : FString());
			Keys.Add(MakeShared<FJsonValueObject>(Row));
		}
		Entry->SetArrayField(TEXT("Keys"), Keys);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetPaperFlipbookCapability)
#endif
