// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MoviePipeline/NexusCreateAssetMoviePipelineConfigCapability.h"
#if WITH_MOVIE_RENDER_PIPELINE
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#if NX_UE_HAS_MOVIE_PIPELINE_PRIMARY_CONFIG
#include "MoviePipelinePrimaryConfig.h"
using FNexusMoviePipelineConfig = UMoviePipelinePrimaryConfig;
#else
#include "MoviePipelineMasterConfig.h"
using FNexusMoviePipelineConfig = UMoviePipelineMasterConfig;
#endif
#include "NexusMcpTool.h"

void FNexusCreateAssetMoviePipelineConfigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetMoviePipelineConfig");
	Out.Description = TEXT("Create empty MoviePipeline config. Does not trigger render.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mrq"), TEXT("render"), TEXT("movie"), TEXT("pipeline") };
	Out.RelatedCapabilities = {
		TEXT("GetAssetMoviePipelineConfig"), TEXT("ManageAssetMoviePipelineConfig"),
		TEXT("ControlMoviePipeline")
	};
}

FCapabilityResult FNexusCreateAssetMoviePipelineConfigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset(AssetPath, FNexusMoviePipelineConfig::StaticClass());
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		FNexusMoviePipelineConfig* Cfg = Cast<FNexusMoviePipelineConfig>(Created.Asset);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, Cfg);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetMoviePipelineConfigCapability)
#endif
