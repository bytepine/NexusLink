// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Sequencer/NexusManageAssetLevelSequenceCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneAudioTrack.h"

void FManageAssetLevelSequenceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_level_sequence");
	Out.Description = TEXT("编辑 LevelSequence：set_display_rate/set_range/remove_binding/add_master_track/remove_master_track。");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("操作类型"),
			{
				TEXT("set_display_rate"),
				TEXT("set_playback_range"),
				TEXT("remove_binding"),
				TEXT("add_master_track"),
				TEXT("remove_master_track"),
			}))
		.Prop(TEXT("numerator"),   FNexusSchema::Int(TEXT("帧率分子（set_display_rate）"), 30))
		.Prop(TEXT("denominator"), FNexusSchema::Int(TEXT("帧率分母（set_display_rate）"), 1))
		.Prop(TEXT("startFrame"),  FNexusSchema::Int(TEXT("起始帧（set_playback_range）")))
		.Prop(TEXT("endFrame"),    FNexusSchema::Int(TEXT("结束帧（set_playback_range）")))
		.Prop(TEXT("bindingGuid"), FNexusSchema::Str(TEXT("Binding GUID（remove_binding）")))
		.Prop(TEXT("trackClass"),  FNexusSchema::Enum(
			TEXT("add/remove_master_track：轨道类型"),
			{ TEXT("CameraCut"), TEXT("Audio") }))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("LevelSequence 资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("sequence"), TEXT("sequencer"), TEXT("cinematic"), TEXT("track"), TEXT("frame"), TEXT("camera") };
	Out.RelatedCapabilities = { TEXT("get_asset_level_sequence"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("改 LevelSequence 的帧率/播放范围/Binding/MasterTrack");
}

FCapabilityResult FManageAssetLevelSequenceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("manage_asset_level_sequence 仅在 Editor 版本中可用");
		return;
#else
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

		const TArray<TSharedPtr<FJsonValue>>* Ops;
		if (!Arguments->TryGetArrayField(TEXT("operations"), Ops) || !Ops)
		{
			OutError = TEXT("operations 为必填数组");
			return;
		}

		bool bDirty = false;

		for (const TSharedPtr<FJsonValue>& OpVal : *Ops)
		{
			TSharedPtr<FJsonObject> Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;

			TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			if (Action == TEXT("set_display_rate"))
			{
				int64 Num = 30, Den = 1;
				Op->TryGetNumberField(TEXT("numerator"),   Num);
				Op->TryGetNumberField(TEXT("denominator"), Den);
				if (Den <= 0) Den = 1;
				Scene->SetDisplayRate(FFrameRate(static_cast<int32>(Num), static_cast<int32>(Den)));
				OpResult->SetBoolField(TEXT("success"), true);
				OpResult->SetStringField(TEXT("displayRate"), FString::Printf(TEXT("%d/%d"), (int32)Num, (int32)Den));
				bDirty = true;
			}
			else if (Action == TEXT("set_playback_range"))
			{
				int64 StartFrame = 0, EndFrame = 0;
				bool bHasStart = Op->TryGetNumberField(TEXT("startFrame"), StartFrame);
				bool bHasEnd   = Op->TryGetNumberField(TEXT("endFrame"), EndFrame);
				if (!bHasStart && !bHasEnd)
				{
					OpResult->SetStringField(TEXT("error"), TEXT("set_playback_range 需要 startFrame 或 endFrame"));
				}
				else
				{
					TRange<FFrameNumber> Current = Scene->GetPlaybackRange();
					FFrameNumber Start = bHasStart ? FFrameNumber(static_cast<int32>(StartFrame)) : Current.GetLowerBoundValue();
					FFrameNumber End   = bHasEnd   ? FFrameNumber(static_cast<int32>(EndFrame))   : Current.GetUpperBoundValue();
					Scene->SetPlaybackRange(TRange<FFrameNumber>(Start, End));
					OpResult->SetBoolField(TEXT("success"), true);
					bDirty = true;
				}
			}
			else if (Action == TEXT("remove_binding"))
			{
				FString GuidStr;
				if (!Op->TryGetStringField(TEXT("bindingGuid"), GuidStr))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("remove_binding 需要 bindingGuid"));
				}
				else
				{
					FGuid Guid;
					if (!FGuid::Parse(GuidStr, Guid))
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("无效 GUID: %s"), *GuidStr));
					}
					else
					{
						bool bRemoved = Scene->RemovePossessable(Guid) || Scene->RemoveSpawnable(Guid);
						OpResult->SetBoolField(TEXT("success"), bRemoved);
						if (bRemoved) bDirty = true;
					}
				}
			}
			else if (Action == TEXT("add_master_track"))
			{
				FString TrackClass;
				if (!Op->TryGetStringField(TEXT("trackClass"), TrackClass))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_master_track 需要 trackClass"));
				}
				else
				{
					UClass* Class = nullptr;
					if (TrackClass == TEXT("CameraCut"))
						Class = UMovieSceneCameraCutTrack::StaticClass();
					else if (TrackClass == TEXT("Audio"))
						Class = UMovieSceneAudioTrack::StaticClass();
					else
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 trackClass: %s（支持 CameraCut/Audio）"), *TrackClass));

					if (Class)
					{
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
					// 避免重复添加同类 MasterTrack
					bool bExists = false;
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					for (UMovieSceneTrack* Existing : Scene->GetMasterTracks())
					{
						if (Existing && Existing->GetClass() == Class)
						{
							bExists = true;
							break;
						}
					}
					if (bExists)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("已存在 %s MasterTrack"), *TrackClass));
					}
					else
					{
						UMovieSceneTrack* NewTrack = Scene->AddMasterTrack(Class);
						OpResult->SetBoolField(TEXT("success"), NewTrack != nullptr);
						if (NewTrack) bDirty = true;
					}
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
						OpResult->SetStringField(TEXT("error"), TEXT("add_master_track 在 UE5.5+ 中暂不支持（API 已重构）"));
#endif
					}
				}
			}
			else if (Action == TEXT("remove_master_track"))
			{
				FString TrackClass;
				if (!Op->TryGetStringField(TEXT("trackClass"), TrackClass))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("remove_master_track 需要 trackClass"));
				}
				else
				{
					UClass* Class = nullptr;
					if (TrackClass == TEXT("CameraCut"))
						Class = UMovieSceneCameraCutTrack::StaticClass();
					else if (TrackClass == TEXT("Audio"))
						Class = UMovieSceneAudioTrack::StaticClass();

					if (!Class)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 trackClass: %s"), *TrackClass));
					}
					else
					{
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					UMovieSceneTrack* Found = Scene->FindMasterTrack(Class);
					bool bOk = Found && Scene->RemoveMasterTrack(*Found);
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
					OpResult->SetBoolField(TEXT("success"), bOk);
					if (bOk) bDirty = true;
#else
						OpResult->SetStringField(TEXT("error"), TEXT("remove_master_track 在 UE5.5+ 中暂不支持（API 已重构）"));
#endif
					}
				}
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OpResult));
		}

		if (bDirty)
		{
			LS->MarkPackageDirty();
		}
#endif // WITH_EDITOR
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetLevelSequenceCapability)
