// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MoviePipeline/NexusManageAssetMoviePipelineConfigCapability.h"
#if WITH_MOVIE_RENDER_PIPELINE
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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

void FManageAssetMoviePipelineConfigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_movie_pipeline_config");
	Out.SearchAssetTypes = {TEXT("MoviePipelinePrimaryConfig"), TEXT("MoviePipelineMasterConfig")};
	Out.Description = TEXT("批量编辑 MoviePipeline 输出。operations[].action=set_output。不触发渲染。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"), { TEXT("set_output") }))
		.Prop(TEXT("directory"), FNexusSchema::Str(TEXT("输出目录")))
		.Prop(TEXT("width"), FNexusSchema::Int(TEXT("输出宽度")))
		.Prop(TEXT("height"), FNexusSchema::Int(TEXT("输出高度")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("MoviePipeline 配置资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mrq"), TEXT("output"), TEXT("resolution") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_movie_pipeline_config"), TEXT("create_asset_movie_pipeline_config")
	};
}

FCapabilityResult FManageAssetMoviePipelineConfigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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
		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
			return;
		}
		UMoviePipelineOutputSetting* OutSet = Cfg->FindSetting<UMoviePipelineOutputSetting>();
		if (!OutSet)
		{
			OutSet = Cast<UMoviePipelineOutputSetting>(
				Cfg->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
		}
		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpPtr;
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			Action = Action.ToLower();
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);
			if (Action != TEXT("set_output"))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			if (!OutSet)
			{
				Entry->SetStringField(TEXT("error"), TEXT("无 OutputSetting"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString Dir;
			if (Op->TryGetStringField(TEXT("directory"), Dir) && !Dir.IsEmpty())
			{
				OutSet->OutputDirectory.Path = Dir;
			}
			if (Op->HasField(TEXT("width")) || Op->HasField(TEXT("height")))
			{
				FIntPoint Res = OutSet->OutputResolution;
				if (Op->HasField(TEXT("width"))) Res.X = static_cast<int32>(Op->GetNumberField(TEXT("width")));
				if (Op->HasField(TEXT("height"))) Res.Y = static_cast<int32>(Op->GetNumberField(TEXT("height")));
				OutSet->OutputResolution = Res;
			}
			bDirty = true;
			Entry->SetStringField(TEXT("directory"), OutSet->OutputDirectory.Path);
			Entry->SetNumberField(TEXT("width"), OutSet->OutputResolution.X);
			Entry->SetNumberField(TEXT("height"), OutSet->OutputResolution.Y);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bDirty) Cfg->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetMoviePipelineConfigCapability)
#endif
