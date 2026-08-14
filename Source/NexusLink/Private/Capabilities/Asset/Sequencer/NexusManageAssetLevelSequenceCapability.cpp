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
#include "GameFramework/Actor.h"
#include "Sections/MovieSceneFloatSection.h"
#if NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "KeyParams.h"
#endif

void FManageAssetLevelSequenceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_level_sequence");
	Out.SearchAssetTypes = {TEXT("LevelSequence")};
	Out.Description = TEXT("编辑 LevelSequence：帧率/范围/binding/轨/关键帧。");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("操作类型"),
			{
				TEXT("set_display_rate"),
				TEXT("set_playback_range"),
				TEXT("remove_binding"),
				TEXT("add_master_track"),
				TEXT("remove_master_track"),
				TEXT("add_possessable"),
				TEXT("add_spawnable"),
				TEXT("add_track"),
				TEXT("add_float_key"),
				TEXT("set_transform_key"),
			}))
		.Prop(TEXT("numerator"),   FNexusSchema::Int(TEXT("帧率分子（set_display_rate）"), 30))
		.Prop(TEXT("denominator"), FNexusSchema::Int(TEXT("帧率分母（set_display_rate）"), 1))
		.Prop(TEXT("startFrame"),  FNexusSchema::Int(TEXT("起始帧（set_playback_range）")))
		.Prop(TEXT("endFrame"),    FNexusSchema::Int(TEXT("结束帧（set_playback_range）")))
		.Prop(TEXT("bindingGuid"), FNexusSchema::Str(TEXT("Binding GUID")))
		.Prop(TEXT("possessableName"), FNexusSchema::Str(TEXT("Possessable 显示名")))
		.Prop(TEXT("className"),   FNexusSchema::Str(TEXT("Possessable/Spawnable 类名（默认 Actor）")))
		.Prop(TEXT("trackClass"),  FNexusSchema::Enum(
			TEXT("轨道类型"),
			{ TEXT("CameraCut"), TEXT("Audio"), TEXT("Float"), TEXT("Transform") }))
		.Prop(TEXT("time"),        FNexusSchema::Num(TEXT("关键帧时间秒")))
		.Prop(TEXT("keyValue"),    FNexusSchema::Num(TEXT("Float 关键帧值")))
		.Prop(TEXT("x"), FNexusSchema::Num(TEXT("Transform X")))
		.Prop(TEXT("y"), FNexusSchema::Num(TEXT("Transform Y")))
		.Prop(TEXT("z"), FNexusSchema::Num(TEXT("Transform Z")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("LevelSequence 资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("sequence"), TEXT("sequencer"), TEXT("cinematic"), TEXT("track"), TEXT("frame"), TEXT("camera") };
	Out.RelatedCapabilities = { TEXT("get_asset_level_sequence"), TEXT("create_asset_level_sequence"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("改 LevelSequence 的帧率/Binding/轨/关键帧");
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
					if (bRemoved) bDirty = true;
					else OpResult->SetStringField(TEXT("error"), TEXT("remove_binding 未找到对应绑定"));
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
						if (NewTrack) bDirty = true;
						else OpResult->SetStringField(TEXT("error"), TEXT("add_master_track 失败"));
					}
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
						UMovieSceneTrack* NewTrack = Scene->AddTrack(Class);
						if (NewTrack) bDirty = true;
						else OpResult->SetStringField(TEXT("error"), TEXT("add_master_track 失败"));
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
					if (bOk) bDirty = true;
					else OpResult->SetStringField(TEXT("error"), TEXT("remove_master_track 未找到对应 Track"));
#else
						UMovieSceneTrack* Found = nullptr;
						for (UMovieSceneTrack* T : Scene->GetTracks())
						{
							if (T && T->GetClass() == Class) { Found = T; break; }
						}
						bool bOk = Found && Scene->RemoveTrack(*Found);
						if (bOk) bDirty = true;
						else OpResult->SetStringField(TEXT("error"), TEXT("remove_master_track 未找到对应 Track"));
#endif
					}
				}
			}
			else if (Action == TEXT("add_possessable"))
			{
				FString PossessName, ClassName;
				Op->TryGetStringField(TEXT("possessableName"), PossessName);
				Op->TryGetStringField(TEXT("className"), ClassName);
				if (PossessName.IsEmpty()) PossessName = TEXT("Possessable");
				UClass* Cls = AActor::StaticClass();
				if (!ClassName.IsEmpty())
				{
					UClass* Found = FNexusAssetUtils::FindClassWithUPrefix(ClassName);
					if (Found) Cls = Found;
				}
				const FGuid Guid = Scene->AddPossessable(PossessName, Cls);
				OpResult->SetStringField(TEXT("bindingGuid"), Guid.ToString());
				OpResult->SetStringField(TEXT("possessableName"), PossessName);
				bDirty = true;
			}
			else if (Action == TEXT("add_spawnable"))
			{
				FString SpawnName, ClassName;
				Op->TryGetStringField(TEXT("possessableName"), SpawnName);
				Op->TryGetStringField(TEXT("className"), ClassName);
				if (SpawnName.IsEmpty()) SpawnName = TEXT("Spawnable");
				UClass* Cls = AActor::StaticClass();
				if (!ClassName.IsEmpty())
				{
					UClass* Found = FNexusAssetUtils::FindClassWithUPrefix(ClassName);
					if (Found) Cls = Found;
				}
				UObject* Template = NewObject<UObject>(Scene, Cls, NAME_None, RF_Transactional);
				if (!Template)
				{
					OpResult->SetStringField(TEXT("error"), TEXT("无法创建 spawnable 模板"));
				}
				else
				{
					const FGuid Guid = Scene->AddSpawnable(SpawnName, *Template);
					OpResult->SetStringField(TEXT("bindingGuid"), Guid.ToString());
					bDirty = true;
				}
			}
			else if (Action == TEXT("add_track"))
			{
				FString GuidStr, TrackClass;
				Op->TryGetStringField(TEXT("bindingGuid"), GuidStr);
				Op->TryGetStringField(TEXT("trackClass"), TrackClass);
				FGuid Guid;
				if (!FGuid::Parse(GuidStr, Guid))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_track 需要有效 bindingGuid"));
				}
				else
				{
					UClass* Class = UMovieSceneFloatTrack::StaticClass();
					if (TrackClass.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
						Class = UMovieScene3DTransformTrack::StaticClass();
					else if (TrackClass.Equals(TEXT("Audio"), ESearchCase::IgnoreCase))
						Class = UMovieSceneAudioTrack::StaticClass();
					UMovieSceneTrack* NewTrack = Scene->AddTrack(Class, Guid);
					if (NewTrack) bDirty = true;
					else OpResult->SetStringField(TEXT("error"), TEXT("add_track 失败"));
				}
			}
			else if (Action == TEXT("add_float_key"))
			{
				double TimeSec = 0.0, KeyVal = 0.0;
				Op->TryGetNumberField(TEXT("time"), TimeSec);
				Op->TryGetNumberField(TEXT("keyValue"), KeyVal);
				UMovieSceneFloatTrack* FloatTrack = nullptr;
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
				for (UMovieSceneTrack* T : Scene->GetMasterTracks())
#else
				for (UMovieSceneTrack* T : Scene->GetTracks())
#endif
				{
					FloatTrack = Cast<UMovieSceneFloatTrack>(T);
					if (FloatTrack) break;
				}
				if (!FloatTrack)
				{
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
					FloatTrack = Scene->AddMasterTrack<UMovieSceneFloatTrack>();
#else
					FloatTrack = Scene->AddTrack<UMovieSceneFloatTrack>();
#endif
				}
				if (!FloatTrack)
				{
					OpResult->SetStringField(TEXT("error"), TEXT("无法获取 FloatTrack"));
				}
				else
				{
					UMovieSceneFloatSection* Section = nullptr;
					for (UMovieSceneSection* S : FloatTrack->GetAllSections())
					{
						Section = Cast<UMovieSceneFloatSection>(S);
						if (Section) break;
					}
					if (!Section)
					{
						Section = Cast<UMovieSceneFloatSection>(FloatTrack->CreateNewSection());
						if (Section) FloatTrack->AddSection(*Section);
					}
					if (!Section)
					{
						OpResult->SetStringField(TEXT("error"), TEXT("无法创建 FloatSection"));
					}
					else
					{
						const FFrameRate Tick = Scene->GetTickResolution();
						const FFrameNumber Frame = Tick.AsFrameNumber(TimeSec);
#if NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
						TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
						if (Channels.Num() > 0) AddKeyToChannel(Channels[0], Frame, static_cast<float>(KeyVal), EMovieSceneKeyInterpolation::Auto);
#else
						Section->FloatCurve.AddKey(static_cast<float>(TimeSec), static_cast<float>(KeyVal));
#endif
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("set_transform_key"))
			{
				FString GuidStr;
				Op->TryGetStringField(TEXT("bindingGuid"), GuidStr);
				double TimeSec = 0, X = 0, Y = 0, Z = 0;
				Op->TryGetNumberField(TEXT("time"), TimeSec);
				Op->TryGetNumberField(TEXT("x"), X);
				Op->TryGetNumberField(TEXT("y"), Y);
				Op->TryGetNumberField(TEXT("z"), Z);
				FGuid Guid;
				if (!FGuid::Parse(GuidStr, Guid))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("set_transform_key 需要 bindingGuid"));
				}
				else
				{
					UMovieScene3DTransformTrack* TTrack = Cast<UMovieScene3DTransformTrack>(Scene->FindTrack(UMovieScene3DTransformTrack::StaticClass(), Guid));
					if (!TTrack)
					{
						TTrack = Cast<UMovieScene3DTransformTrack>(Scene->AddTrack(UMovieScene3DTransformTrack::StaticClass(), Guid));
					}
					if (!TTrack)
					{
						OpResult->SetStringField(TEXT("error"), TEXT("无法添加 TransformTrack"));
					}
					else
					{
						OpResult->SetStringField(TEXT("note"), TEXT("已确保 Transform 轨存在；完整 9 通道关键帧视引擎版本而定"));
						OpResult->SetNumberField(TEXT("x"), X);
						OpResult->SetNumberField(TEXT("y"), Y);
						OpResult->SetNumberField(TEXT("z"), Z);
						bDirty = true;
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
