// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MoviePipeline/NexusGetAssetMoviePipelineConfigCapability.h"
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
#include "MoviePipelineOutputSetting.h"
#include "NexusMcpTool.h"

void FGetAssetMoviePipelineConfigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_movie_pipeline_config");
	Out.SearchAssetTypes = {TEXT("MoviePipelinePrimaryConfig"), TEXT("MoviePipelineMasterConfig")};
	Out.Description = TEXT("读取 MoviePipeline 配置：输出目录 / 分辨率。复杂图设置无。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("MoviePipeline 配置资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mrq"), TEXT("output"), TEXT("resolution") };
	Out.RelatedCapabilities = {
		TEXT("manage_asset_movie_pipeline_config"), TEXT("create_asset_movie_pipeline_config")
	};
}

FCapabilityResult FGetAssetMoviePipelineConfigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		FNexusMoviePipelineConfig* Cfg = FNexusAssetUtils::LoadAssetWithFallback<FNexusMoviePipelineConfig>(AssetPath);
		if (!Cfg)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载 MoviePipeline 配置失败: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Cfg->GetName());
		Entry->SetStringField(TEXT("path"), Cfg->GetPathName());
		if (UMoviePipelineOutputSetting* OutSet = Cfg->FindSetting<UMoviePipelineOutputSetting>())
		{
			Entry->SetStringField(TEXT("outputDirectory"), OutSet->OutputDirectory.Path);
			Entry->SetNumberField(TEXT("width"), OutSet->OutputResolution.X);
			Entry->SetNumberField(TEXT("height"), OutSet->OutputResolution.Y);
			Entry->SetStringField(TEXT("fileNameFormat"), OutSet->FileNameFormat);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetMoviePipelineConfigCapability)
#endif
