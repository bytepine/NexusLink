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
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineHighResSetting.h"
#include "MoviePipelineCameraSetting.h"
#include "MoviePipelineSetting.h"
#include "NexusMcpTool.h"

void FGetAssetMoviePipelineConfigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_movie_pipeline_config");
	Out.SearchAssetTypes = {TEXT("MoviePipelinePrimaryConfig"), TEXT("MoviePipelineMasterConfig")};
	Out.Description = TEXT("Read MoviePipeline config: output/resolution, settings[], AA/HighRes summary.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("MoviePipeline config asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mrq"), TEXT("output"), TEXT("resolution"), TEXT("aa"), TEXT("setting") };
	Out.RelatedCapabilities = {
		TEXT("manage_asset_movie_pipeline_config"), TEXT("create_asset_movie_pipeline_config")
	};
	Out.WhenToUse = TEXT("Read MRQ config structure; use manage_asset_movie_pipeline_config for writes");
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
				FString::Printf(TEXT("Failed to load MoviePipeline config: %s"), *AssetPath));
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
		if (UMoviePipelineAntiAliasingSetting* AA = Cfg->FindSetting<UMoviePipelineAntiAliasingSetting>())
		{
			TSharedPtr<FJsonObject> AAObj = MakeShared<FJsonObject>();
			AAObj->SetNumberField(TEXT("spatialSampleCount"), AA->SpatialSampleCount);
			AAObj->SetNumberField(TEXT("temporalSampleCount"), AA->TemporalSampleCount);
			AAObj->SetBoolField(TEXT("enabled"), AA->IsEnabled());
			Entry->SetObjectField(TEXT("antiAliasing"), AAObj);
		}
		if (UMoviePipelineHighResSetting* Hi = Cfg->FindSetting<UMoviePipelineHighResSetting>())
		{
			TSharedPtr<FJsonObject> HiObj = MakeShared<FJsonObject>();
			HiObj->SetNumberField(TEXT("tileCount"), Hi->TileCount);
			HiObj->SetBoolField(TEXT("enabled"), Hi->IsEnabled());
			Entry->SetObjectField(TEXT("highRes"), HiObj);
		}

		TArray<TSharedPtr<FJsonValue>> SettingsArr;
		for (UMoviePipelineSetting* Setting : Cfg->GetUserSettings())
		{
			if (!Setting) continue;
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("className"), Setting->GetClass()->GetName());
			S->SetBoolField(TEXT("enabled"), Setting->IsEnabled());
			SettingsArr.Add(MakeShared<FJsonValueObject>(S));
		}
		Entry->SetArrayField(TEXT("settings"), SettingsArr);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetMoviePipelineConfigCapability)
#endif
