// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Sequencer/NexusGetAssetLevelSequenceCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"

void FGetAssetLevelSequenceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_level_sequence");
	Out.SearchAssetTypes = {TEXT("LevelSequence")};
	Out.Description = TEXT("读取 LevelSequence 的时长/帧率、Binding 列表与 Track 类型概览。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("LevelSequence 资产路径")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("sequence"), TEXT("sequencer"), TEXT("cinematic"), TEXT("track"), TEXT("animation"), TEXT("cut"), TEXT("shot") };
	Out.RelatedCapabilities = { TEXT("manage_asset_level_sequence"), TEXT("search_asset"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("读 LevelSequence 的 Binding/Track 列表、时长、帧率");
}

FCapabilityResult FGetAssetLevelSequenceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("get_asset_level_sequence 仅在 Editor 版本中可用");
		return;
#else
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		ULevelSequence* LS = FNexusAssetUtils::LoadAssetWithFallback<ULevelSequence>(AssetPath);
		if (!LS)
		{
			OutError = FString::Printf(TEXT("LevelSequence 未找到: %s"), *AssetPath);
			return;
		}

		UMovieScene* Scene = LS->GetMovieScene();
		if (!Scene)
		{
			OutError = TEXT("LevelSequence 无 MovieScene 数据");
			return;
		}

		OutEntry->SetStringField(TEXT("assetType"), TEXT("LevelSequence"));
		OutEntry->SetStringField(TEXT("name"),      LS->GetName());
		OutEntry->SetStringField(TEXT("path"),      FNexusAssetUtils::PackagePathOf(LS));

		// 帧率
		const FFrameRate TickResolution  = Scene->GetTickResolution();
		const FFrameRate DisplayRate     = Scene->GetDisplayRate();
		OutEntry->SetStringField(TEXT("tickResolution"),
			FString::Printf(TEXT("%d/%d"), TickResolution.Numerator, TickResolution.Denominator));
		OutEntry->SetStringField(TEXT("displayRate"),
			FString::Printf(TEXT("%d/%d"), DisplayRate.Numerator, DisplayRate.Denominator));

		// 时长（秒，由 PlaybackRange 推算）
		TRange<FFrameNumber> PlaybackRange = Scene->GetPlaybackRange();
		if (!PlaybackRange.IsEmpty() && TickResolution.Numerator > 0)
		{
			double DurationFrames = static_cast<double>(
				(PlaybackRange.GetUpperBoundValue() - PlaybackRange.GetLowerBoundValue()).Value);
			double DurationSecs = DurationFrames / static_cast<double>(TickResolution.AsDecimal());
			OutEntry->SetNumberField(TEXT("durationSeconds"), DurationSecs);
		}

		// Bindings（Possessable + Spawnable）
		int32 BindingsCount = Scene->GetPossessableCount() + Scene->GetSpawnableCount();
		OutEntry->SetNumberField(TEXT("bindingsCount"), BindingsCount);

		TArray<TSharedPtr<FJsonValue>> BindingsArr;

		// 构建 GUID→类型/名称 的查找表（同时兼容 4.26 和 5.x）
		TMap<FGuid, TSharedPtr<FJsonObject>> GuidToInfo;
		for (int32 i = 0; i < Scene->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& Poss = Scene->GetPossessable(i);
			TSharedPtr<FJsonObject> BObj = MakeShared<FJsonObject>();
			BObj->SetStringField(TEXT("type"),  TEXT("Possessable"));
			BObj->SetStringField(TEXT("name"),  Poss.GetName());
			BObj->SetStringField(TEXT("guid"),  Poss.GetGuid().ToString());
			BObj->SetStringField(TEXT("class"), Poss.GetPossessedObjectClass() ? Poss.GetPossessedObjectClass()->GetName() : TEXT(""));
			GuidToInfo.Add(Poss.GetGuid(), BObj);
		}
		for (int32 i = 0; i < Scene->GetSpawnableCount(); ++i)
		{
			const FMovieSceneSpawnable& Spawn = Scene->GetSpawnable(i);
			TSharedPtr<FJsonObject> BObj = MakeShared<FJsonObject>();
			BObj->SetStringField(TEXT("type"), TEXT("Spawnable"));
			BObj->SetStringField(TEXT("name"), Spawn.GetName());
			BObj->SetStringField(TEXT("guid"), Spawn.GetGuid().ToString());
			GuidToInfo.Add(Spawn.GetGuid(), BObj);
		}

		// 遍历 Bindings 填充轨道列表（GetBindings 在 4.26+ 均可用，5.8 要求 const 调用）
		for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Scene)->GetBindings())
		{
			TSharedPtr<FJsonObject>* BObjPtr = GuidToInfo.Find(Binding.GetObjectGuid());
			if (!BObjPtr) continue;

			TArray<TSharedPtr<FJsonValue>> TrackArr;
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				if (Track)
				{
					TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
					TObj->SetStringField(TEXT("trackClass"),  Track->GetClass()->GetName());
					TObj->SetStringField(TEXT("displayName"), Track->GetDisplayName().ToString());
					TObj->SetNumberField(TEXT("sectionsCount"), Track->GetAllSections().Num());
					TrackArr.Add(MakeShared<FJsonValueObject>(TObj));
				}
			}
			(*BObjPtr)->SetArrayField(TEXT("tracks"), TrackArr);
		}

		for (auto& KV : GuidToInfo)
		{
			if (!KV.Value->HasField(TEXT("tracks")))
			{
				KV.Value->SetArrayField(TEXT("tracks"), TArray<TSharedPtr<FJsonValue>>());
			}
			BindingsArr.Add(MakeShared<FJsonValueObject>(KV.Value));
		}
		OutEntry->SetArrayField(TEXT("bindings"), BindingsArr);

		// Master tracks（Camera Cut、Audio 等无 Binding 的全局 Track，UE < 5.5）
		TArray<TSharedPtr<FJsonValue>> MasterTrackArr;
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		for (UMovieSceneTrack* MasterTrack : Scene->GetMasterTracks())
		{
			if (MasterTrack)
			{
				TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
				TObj->SetStringField(TEXT("trackClass"),  MasterTrack->GetClass()->GetName());
				TObj->SetStringField(TEXT("displayName"), MasterTrack->GetDisplayName().ToString());
				MasterTrackArr.Add(MakeShared<FJsonValueObject>(TObj));
			}
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
		OutEntry->SetArrayField(TEXT("masterTracks"), MasterTrackArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
#endif // WITH_EDITOR
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetLevelSequenceCapability)
