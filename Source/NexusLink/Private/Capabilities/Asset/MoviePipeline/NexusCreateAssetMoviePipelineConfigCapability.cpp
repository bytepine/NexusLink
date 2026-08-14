// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MoviePipeline/NexusCreateAssetMoviePipelineConfigCapability.h"
#if WITH_MOVIE_RENDER_PIPELINE
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusVersionCompat.h"
#if NX_UE_HAS_MOVIE_PIPELINE_PRIMARY_CONFIG
#include "MoviePipelinePrimaryConfig.h"
using FNexusMoviePipelineConfig = UMoviePipelinePrimaryConfig;
#else
#include "MoviePipelineMasterConfig.h"
using FNexusMoviePipelineConfig = UMoviePipelineMasterConfig;
#endif
#include "NexusMcpTool.h"

void FCreateAssetMoviePipelineConfigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_movie_pipeline_config");
	Out.Description = TEXT("创建空白 MoviePipeline 配置。不触发渲染。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mrq"), TEXT("render"), TEXT("movie"), TEXT("pipeline") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_movie_pipeline_config"), TEXT("manage_asset_movie_pipeline_config")
	};
}

FCapabilityResult FCreateAssetMoviePipelineConfigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<FNexusMoviePipelineConfig>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("MoviePipeline config already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		FNexusMoviePipelineConfig* Cfg = NewObject<FNexusMoviePipelineConfig>(
			Package, *AssetName, RF_Public | RF_Standalone);
		if (!Cfg) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败")); return; }
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Cfg, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Cfg->GetName());
		Entry->SetStringField(TEXT("path"), Cfg->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetMoviePipelineConfigCapability)
#endif
