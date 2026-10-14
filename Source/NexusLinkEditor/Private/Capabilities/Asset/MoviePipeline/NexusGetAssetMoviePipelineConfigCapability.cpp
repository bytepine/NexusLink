// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MoviePipeline/NexusGetAssetMoviePipelineConfigCapability.h"
#if WITH_MOVIE_RENDER_PIPELINE
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
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

void FNexusGetAssetMoviePipelineConfigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetMoviePipelineConfig");
	Out.SearchAssetTypes = {TEXT("MoviePipelinePrimaryConfig"), TEXT("MoviePipelineMasterConfig")};
	Out.Description = TEXT("Read MoviePipeline config: output/resolution, settings[], AA/HighRes summary.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("MoviePipeline config asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mrq"), TEXT("Output"), TEXT("resolution"), TEXT("aa"), TEXT("setting") };
	Out.RelatedCapabilities = {
		TEXT("ManageAssetMoviePipelineConfig"), TEXT("CreateAssetMoviePipelineConfig"),
		TEXT("ControlMoviePipeline")
	};
	Out.WhenToUse = TEXT("Read MRQ config structure; use ManageAssetMoviePipelineConfig for writes");
}

FCapabilityResult FNexusGetAssetMoviePipelineConfigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FString AssetPath = FNexusArgs(Arguments).Str(TEXT("AssetPath"));
		FNexusMoviePipelineConfig* Cfg = FNexusAssetUtils::LoadAssetWithFallback<FNexusMoviePipelineConfig>(AssetPath);
		if (!Cfg)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Failed to load MoviePipeline config: %s"), *AssetPath));
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Cfg->GetName());
		Entry->SetStringField(TEXT("Path"), Cfg->GetPathName());
		if (UMoviePipelineOutputSetting* OutSet = Cfg->FindSetting<UMoviePipelineOutputSetting>())
		{
			Entry->SetStringField(TEXT("OutputDirectory"), OutSet->OutputDirectory.Path);
			Entry->SetNumberField(TEXT("Width"), OutSet->OutputResolution.X);
			Entry->SetNumberField(TEXT("Height"), OutSet->OutputResolution.Y);
			Entry->SetStringField(TEXT("FileNameFormat"), OutSet->FileNameFormat);
		}
		if (UMoviePipelineAntiAliasingSetting* AA = Cfg->FindSetting<UMoviePipelineAntiAliasingSetting>())
		{
			TSharedPtr<FJsonObject> AAObj = MakeShared<FJsonObject>();
			AAObj->SetNumberField(TEXT("SpatialSampleCount"), AA->SpatialSampleCount);
			AAObj->SetNumberField(TEXT("TemporalSampleCount"), AA->TemporalSampleCount);
			AAObj->SetBoolField(TEXT("bEnabled"), AA->IsEnabled());
			Entry->SetObjectField(TEXT("AntiAliasing"), AAObj);
		}
		if (UMoviePipelineHighResSetting* Hi = Cfg->FindSetting<UMoviePipelineHighResSetting>())
		{
			TSharedPtr<FJsonObject> HiObj = MakeShared<FJsonObject>();
			HiObj->SetNumberField(TEXT("TileCount"), Hi->TileCount);
			HiObj->SetBoolField(TEXT("bEnabled"), Hi->IsEnabled());
			Entry->SetObjectField(TEXT("HighRes"), HiObj);
		}

		TArray<TSharedPtr<FJsonValue>> SettingsArr;
		for (UMoviePipelineSetting* Setting : Cfg->GetUserSettings())
		{
			if (!Setting) continue;
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("ClassName"), Setting->GetClass()->GetName());
			S->SetBoolField(TEXT("bEnabled"), Setting->IsEnabled());
			SettingsArr.Add(MakeShared<FJsonValueObject>(S));
		}
		Entry->SetArrayField(TEXT("Settings"), SettingsArr);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetMoviePipelineConfigCapability)
#endif
