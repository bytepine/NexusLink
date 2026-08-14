// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Sequencer/NexusCreateAssetLevelSequenceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "NexusMcpTool.h"

void FCreateAssetLevelSequenceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_level_sequence");
	Out.Description = TEXT("创建空白 LevelSequence 并初始化 MovieScene。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("sequence"), TEXT("sequencer"), TEXT("cinematic"), TEXT("levelsequence") };
	Out.RelatedCapabilities = { TEXT("get_asset_level_sequence"), TEXT("manage_asset_level_sequence") };
	Out.WhenToUse = TEXT("新建 LevelSequence；再用 manage 加 binding/轨/关键帧");
}

FCapabilityResult FCreateAssetLevelSequenceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<ULevelSequence>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("LevelSequence already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		ULevelSequence* LS = NewObject<ULevelSequence>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!LS) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("LevelSequence 创建失败")); return; }
		LS->Initialize();

		FNexusAssetUtils::NotifyAndSaveCreated(Package, LS, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), LS->GetName());
		Entry->SetStringField(TEXT("path"), LS->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("LevelSequence"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetLevelSequenceCapability)
