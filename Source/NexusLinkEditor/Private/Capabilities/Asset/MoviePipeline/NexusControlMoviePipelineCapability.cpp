// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MoviePipeline/NexusControlMoviePipelineCapability.h"
#if WITH_MOVIE_RENDER_PIPELINE && WITH_EDITOR

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
#include "MoviePipelineQueueSubsystem.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelinePIEExecutor.h"
#include "LevelSequence.h"
#include "Editor.h"
#include "NexusMcpTool.h"

void FControlMoviePipelineCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("control_movie_pipeline");
	Out.Description = TEXT("Control MRQ queue. enqueue/status/cancel. Does not wait for render. UE5+.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Queue operation"),
			{ TEXT("enqueue"), TEXT("status"), TEXT("cancel") }))
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("MoviePipeline config path (enqueue)")))
		.Prop(TEXT("sequencePath"), FNexusSchema::Str(TEXT("LevelSequence path (enqueue)")))
		.Required({ TEXT("action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mrq"), TEXT("render"), TEXT("queue"), TEXT("enqueue") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_movie_pipeline_config"),
		TEXT("manage_asset_movie_pipeline_config"),
		TEXT("create_asset_movie_pipeline_config")
	};
	Out.WhenToUse = TEXT("Enqueue MRQ from config+sequence; disk settings via manage_asset_movie_pipeline_config");
}

static UMoviePipelineQueueSubsystem* GetMovieQueueSubsystem(FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor unavailable");
		return nullptr;
	}
	UMoviePipelineQueueSubsystem* Sub = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	if (!Sub)
	{
		OutError = TEXT("MoviePipelineQueueSubsystem unavailable");
	}
	return Sub;
}

FCapabilityResult FControlMoviePipelineCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Action = A.Str(TEXT("action"));
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString SubErr;
		UMoviePipelineQueueSubsystem* Sub = GetMovieQueueSubsystem(SubErr);
		if (!Sub)
		{
			OutEntry->SetStringField(TEXT("error"), SubErr);
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		if (Action.Equals(TEXT("status"), ESearchCase::IgnoreCase))
		{
			OutEntry->SetStringField(TEXT("action"), TEXT("status"));
			OutEntry->SetBoolField(TEXT("isRendering"), Sub->IsRendering());
			UMoviePipelineQueue* Queue = Sub->GetQueue();
			const int32 JobCount = Queue ? Queue->GetJobs().Num() : 0;
			OutEntry->SetNumberField(TEXT("jobCount"), JobCount);
			if (UMoviePipelineExecutorBase* Ex = Sub->GetActiveExecutor())
			{
				OutEntry->SetStringField(TEXT("executor"), Ex->GetClass()->GetName());
			}
		}
		else if (Action.Equals(TEXT("cancel"), ESearchCase::IgnoreCase))
		{
			UMoviePipelineExecutorBase* Ex = Sub->GetActiveExecutor();
			if (!Ex || !Sub->IsRendering())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("No active MoviePipeline render"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			Ex->CancelAllJobs();
			OutEntry->SetStringField(TEXT("action"), TEXT("cancel"));
			OutEntry->SetStringField(TEXT("note"), TEXT("Cancel requested."));
		}
		else if (Action.Equals(TEXT("enqueue"), ESearchCase::IgnoreCase))
		{
			const FString AssetPath = A.Str(TEXT("assetPath"));
			const FString SequencePath = A.Str(TEXT("sequencePath"));
			if (AssetPath.IsEmpty() || SequencePath.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("enqueue requires assetPath and sequencePath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			if (Sub->IsRendering())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("MoviePipeline already rendering; cancel first"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			FNexusMoviePipelineConfig* Cfg = FNexusAssetUtils::LoadAssetWithFallback<FNexusMoviePipelineConfig>(AssetPath);
			if (!Cfg)
			{
				OutEntry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("MoviePipeline config not found: %s"), *AssetPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			ULevelSequence* Sequence = FNexusAssetUtils::LoadAssetWithFallback<ULevelSequence>(SequencePath);
			if (!Sequence)
			{
				OutEntry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			UMoviePipelineQueue* Queue = Sub->GetQueue();
			if (!Queue)
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("MoviePipeline queue unavailable"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			UMoviePipelineExecutorJob* Job = Queue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());
			if (!Job)
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Failed to allocate MoviePipeline job"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			const FString JobId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
			Job->JobName = JobId;
			Job->SetSequence(FSoftObjectPath(Sequence));
			Job->SetConfiguration(Cfg);
			if (UWorld* World = GEditor->GetEditorWorldContext().World())
			{
				Job->Map = FSoftObjectPath(World);
			}

			Sub->RenderQueueWithExecutor(UMoviePipelinePIEExecutor::StaticClass());
			OutEntry->SetStringField(TEXT("action"), TEXT("enqueue"));
			OutEntry->SetStringField(TEXT("jobId"), JobId);
			OutEntry->SetStringField(TEXT("note"), TEXT("Queued; render is asynchronous."));
		}
		else
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Unknown action: %s; supported: enqueue / status / cancel"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FControlMoviePipelineCapability)
#endif
