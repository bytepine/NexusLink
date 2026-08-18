// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Sequencer/NexusManageAssetLevelSequenceCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
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
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneLevelVisibilityTrack.h"
#include "Tracks/MovieSceneSlomoTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneParticleTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneIntegerTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"
#include "GameFramework/Actor.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieScene3DTransformSection.h"
#if NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "KeyParams.h"
#endif

namespace
{
	/** 解析 trackClass → UClass；未知返回 nullptr 并写 OutError。 */
	UClass* ResolveLevelSequenceTrackClass(const FString& TrackClass, FString& OutError)
	{
		if (TrackClass.IsEmpty() || TrackClass.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
			return UMovieSceneFloatTrack::StaticClass();
		if (TrackClass.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
			return UMovieScene3DTransformTrack::StaticClass();
		if (TrackClass.Equals(TEXT("Audio"), ESearchCase::IgnoreCase))
			return UMovieSceneAudioTrack::StaticClass();
		if (TrackClass.Equals(TEXT("CameraCut"), ESearchCase::IgnoreCase))
			return UMovieSceneCameraCutTrack::StaticClass();
		if (TrackClass.Equals(TEXT("CinematicShot"), ESearchCase::IgnoreCase))
			return UMovieSceneCinematicShotTrack::StaticClass();
		if (TrackClass.Equals(TEXT("Fade"), ESearchCase::IgnoreCase))
			return UMovieSceneFadeTrack::StaticClass();
		if (TrackClass.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
			return UMovieSceneEventTrack::StaticClass();
		if (TrackClass.Equals(TEXT("LevelVisibility"), ESearchCase::IgnoreCase))
			return UMovieSceneLevelVisibilityTrack::StaticClass();
		if (TrackClass.Equals(TEXT("Slomo"), ESearchCase::IgnoreCase))
			return UMovieSceneSlomoTrack::StaticClass();
		if (TrackClass.Equals(TEXT("SkeletalAnimation"), ESearchCase::IgnoreCase))
			return UMovieSceneSkeletalAnimationTrack::StaticClass();
		if (TrackClass.Equals(TEXT("Particle"), ESearchCase::IgnoreCase))
			return UMovieSceneParticleTrack::StaticClass();
		if (TrackClass.Equals(TEXT("Visibility"), ESearchCase::IgnoreCase))
			return UMovieSceneVisibilityTrack::StaticClass();
		if (TrackClass.Equals(TEXT("Color"), ESearchCase::IgnoreCase))
			return UMovieSceneColorTrack::StaticClass();
		if (TrackClass.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
			return UMovieSceneBoolTrack::StaticClass();
		if (TrackClass.Equals(TEXT("Integer"), ESearchCase::IgnoreCase))
			return UMovieSceneIntegerTrack::StaticClass();
		if (TrackClass.Equals(TEXT("Vector"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("FloatVector"), ESearchCase::IgnoreCase))
#if NX_UE_HAS_MOVIE_SCENE_FLOAT_VECTOR_TRACK
			return UMovieSceneFloatVectorTrack::StaticClass();
#else
			return UMovieSceneVectorTrack::StaticClass();
#endif
		if (TrackClass.Equals(TEXT("DoubleVector"), ESearchCase::IgnoreCase))
#if NX_UE_HAS_MOVIE_SCENE_FLOAT_VECTOR_TRACK
			return UMovieSceneDoubleVectorTrack::StaticClass();
#else
		{
			OutError = TEXT("DoubleVector UE5+ only");
			return nullptr;
		}
#endif

		OutError = FString::Printf(
			TEXT("Unknown trackClass: %s (Master: CameraCut/Audio/...; Binding: Float/Transform/...)"),
			*TrackClass);
		return nullptr;
	}

	bool IsMasterTrackClassName(const FString& TrackClass)
	{
		return TrackClass.Equals(TEXT("CameraCut"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Audio"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("CinematicShot"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Fade"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Event"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("LevelVisibility"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Slomo"), ESearchCase::IgnoreCase);
	}

	bool IsBindingTrackClassName(const FString& TrackClass)
	{
		return TrackClass.IsEmpty()
			|| TrackClass.Equals(TEXT("Float"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Transform"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Audio"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("SkeletalAnimation"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Particle"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Visibility"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Color"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Bool"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Integer"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Vector"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("FloatVector"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("DoubleVector"), ESearchCase::IgnoreCase)
			|| TrackClass.Equals(TEXT("Event"), ESearchCase::IgnoreCase);
	}
}

#if WITH_EDITOR && NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
static void WriteFloatChannelKey(UMovieScene* Scene, FMovieSceneFloatChannel* Channel, double TimeSec, float Value)
{
	if (!Scene || !Channel) return;
	const FFrameRate Tick = Scene->GetTickResolution();
	const FFrameNumber Frame = Tick.AsFrameNumber(TimeSec);
	AddKeyToChannel(Channel, Frame, Value, EMovieSceneKeyInterpolation::Auto);
}

static bool AddKeyToFloatTrack(UMovieScene* Scene, UMovieSceneFloatTrack* FloatTrack, double TimeSec, float KeyVal, FString& OutError)
{
	if (!Scene || !FloatTrack)
	{
		OutError = TEXT("Invalid FloatTrack");
		return false;
	}
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
		OutError = TEXT("Unable to create FloatSection");
		return false;
	}
	const FFrameRate Tick = Scene->GetTickResolution();
	const FFrameNumber Frame = Tick.AsFrameNumber(TimeSec);
	Section->SetRange(TRange<FFrameNumber>::Hull(Section->GetRange(), TRange<FFrameNumber>(Frame, Frame + 1)));
	TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	if (Channels.Num() == 0)
	{
		OutError = TEXT("FloatSection has no FloatChannel");
		return false;
	}
	WriteFloatChannelKey(Scene, Channels[0], TimeSec, KeyVal);
	return true;
}
#endif

void FManageAssetLevelSequenceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_level_sequence");
	Out.SearchAssetTypes = {TEXT("LevelSequence")};
	Out.Description = TEXT("Edit LevelSequence: rate/range/bindings/tracks/keys.");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("Operation type"),
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
		.Prop(TEXT("numerator"),   FNexusSchema::Int(TEXT("Frame rate numerator (set_display_rate)"), 30))
		.Prop(TEXT("denominator"), FNexusSchema::Int(TEXT("Frame rate denominator (set_display_rate)"), 1))
		.Prop(TEXT("startFrame"),  FNexusSchema::Int(TEXT("Start frame (set_playback_range)")))
		.Prop(TEXT("endFrame"),    FNexusSchema::Int(TEXT("End frame (set_playback_range)")))
		.Prop(TEXT("bindingGuid"), FNexusSchema::Str(TEXT("Binding GUID (required for add_track/add_float_key/set_transform_key)")))
		.Prop(TEXT("possessableName"), FNexusSchema::Str(TEXT("Possessable display name")))
		.Prop(TEXT("className"),   FNexusSchema::Str(TEXT("Possessable/Spawnable class (default Actor)")))
		.Prop(TEXT("trackClass"),  FNexusSchema::Enum(
			TEXT("Track type"),
			{
				TEXT("CameraCut"), TEXT("Audio"), TEXT("CinematicShot"), TEXT("Fade"),
				TEXT("Event"), TEXT("LevelVisibility"), TEXT("Slomo"),
				TEXT("Float"), TEXT("Transform"), TEXT("SkeletalAnimation"), TEXT("Particle"),
				TEXT("Visibility"), TEXT("Color"), TEXT("Bool"), TEXT("Integer"),
				TEXT("Vector"), TEXT("FloatVector"), TEXT("DoubleVector")
			}))
		.Prop(TEXT("time"),        FNexusSchema::Num(TEXT("Keyframe time in seconds")))
		.Prop(TEXT("keyValue"),    FNexusSchema::Num(TEXT("Float keyframe value")))
		.Prop(TEXT("x"), FNexusSchema::Num(TEXT("Transform location X")))
		.Prop(TEXT("y"), FNexusSchema::Num(TEXT("Transform location Y")))
		.Prop(TEXT("z"), FNexusSchema::Num(TEXT("Transform location Z")))
		.Prop(TEXT("pitch"), FNexusSchema::Num(TEXT("Transform rotation Pitch (optional)")))
		.Prop(TEXT("yaw"), FNexusSchema::Num(TEXT("Transform rotation Yaw (optional)")))
		.Prop(TEXT("roll"), FNexusSchema::Num(TEXT("Transform rotation Roll (optional)")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("LevelSequence asset path")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("sequence"), TEXT("sequencer"), TEXT("cinematic"), TEXT("track"), TEXT("frame"), TEXT("camera") };
	Out.RelatedCapabilities = { TEXT("get_asset_level_sequence"), TEXT("create_asset_level_sequence"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("Edit LevelSequence frame rate/bindings/tracks/keys");
}

FCapabilityResult FManageAssetLevelSequenceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("manage_asset_level_sequence only available in Editor builds");
		return;
#else
		const FString AssetPath = A.Str(TEXT("assetPath"));

		ULevelSequence* LS = FNexusAssetUtils::LoadAssetWithFallback<ULevelSequence>(AssetPath);
		if (!LS)
		{
			OutError = FString::Printf(TEXT("LevelSequence not found: %s"), *AssetPath);
			return;
		}

		UMovieScene* Scene = LS->GetMovieScene();
		if (!Scene)
		{
			OutError = TEXT("LevelSequence has no MovieScene data");
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("operations is a required array");
			return;
		}

		bool bDirty = false;

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
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
					OpResult->SetStringField(TEXT("error"), TEXT("set_playback_range requires startFrame or endFrame"));
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
					OpResult->SetStringField(TEXT("error"), TEXT("remove_binding requires bindingGuid"));
				}
				else
				{
					FGuid Guid;
					if (!FGuid::Parse(GuidStr, Guid))
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid GUID: %s"), *GuidStr));
					}
					else
					{
					bool bRemoved = Scene->RemovePossessable(Guid) || Scene->RemoveSpawnable(Guid);
					if (bRemoved) bDirty = true;
					else OpResult->SetStringField(TEXT("error"), TEXT("remove_binding: matching binding not found"));
					}
				}
			}
			else if (Action == TEXT("add_master_track"))
			{
				FString TrackClass;
				if (!Op->TryGetStringField(TEXT("trackClass"), TrackClass) || TrackClass.IsEmpty())
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_master_track requires trackClass"));
				}
				else if (!IsMasterTrackClassName(TrackClass))
				{
					OpResult->SetStringField(TEXT("error"), FString::Printf(
						TEXT("trackClass '%s' cannot be MasterTrack (supports CameraCut/Audio/…/Slomo)"),
						*TrackClass));
				}
				else
				{
					FString ResolveErr;
					UClass* Class = ResolveLevelSequenceTrackClass(TrackClass, ResolveErr);
					if (!Class)
					{
						OpResult->SetStringField(TEXT("error"), ResolveErr);
					}
					else
					{
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
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
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("%s MasterTrack already exists"), *TrackClass));
					}
					else
					{
						UMovieSceneTrack* NewTrack = Scene->AddMasterTrack(Class);
						if (NewTrack)
						{
							OpResult->SetStringField(TEXT("trackClass"), TrackClass);
							OpResult->SetStringField(TEXT("trackType"), NewTrack->GetClass()->GetName());
							bDirty = true;
						}
						else OpResult->SetStringField(TEXT("error"), TEXT("add_master_track failed"));
					}
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
						UMovieSceneTrack* NewTrack = Scene->AddTrack(Class);
						if (NewTrack)
						{
							OpResult->SetStringField(TEXT("trackClass"), TrackClass);
							OpResult->SetStringField(TEXT("trackType"), NewTrack->GetClass()->GetName());
							bDirty = true;
						}
						else OpResult->SetStringField(TEXT("error"), TEXT("add_master_track failed"));
#endif
					}
				}
			}
			else if (Action == TEXT("remove_master_track"))
			{
				FString TrackClass;
				if (!Op->TryGetStringField(TEXT("trackClass"), TrackClass) || TrackClass.IsEmpty())
				{
					OpResult->SetStringField(TEXT("error"), TEXT("remove_master_track requires trackClass"));
				}
				else if (!IsMasterTrackClassName(TrackClass))
				{
					OpResult->SetStringField(TEXT("error"), FString::Printf(
						TEXT("trackClass '%s' is not a MasterTrack type"), *TrackClass));
				}
				else
				{
					FString ResolveErr;
					UClass* Class = ResolveLevelSequenceTrackClass(TrackClass, ResolveErr);
					if (!Class)
					{
						OpResult->SetStringField(TEXT("error"), ResolveErr);
					}
					else
					{
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					UMovieSceneTrack* Found = Scene->FindMasterTrack(Class);
					bool bOk = Found && Scene->RemoveMasterTrack(*Found);
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
					if (bOk)
					{
						OpResult->SetStringField(TEXT("trackClass"), TrackClass);
						bDirty = true;
					}
					else OpResult->SetStringField(TEXT("error"), TEXT("remove_master_track: matching Track not found"));
#else
						UMovieSceneTrack* Found = nullptr;
						for (UMovieSceneTrack* T : Scene->GetTracks())
						{
							if (T && T->GetClass() == Class) { Found = T; break; }
						}
						bool bOk = Found && Scene->RemoveTrack(*Found);
						if (bOk)
						{
							OpResult->SetStringField(TEXT("trackClass"), TrackClass);
							bDirty = true;
						}
						else OpResult->SetStringField(TEXT("error"), TEXT("remove_master_track: matching Track not found"));
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
					OpResult->SetStringField(TEXT("error"), TEXT("Unable to create spawnable template"));
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
					OpResult->SetStringField(TEXT("error"), TEXT("add_track requires valid bindingGuid"));
				}
				else if (!IsBindingTrackClassName(TrackClass))
				{
					OpResult->SetStringField(TEXT("error"), FString::Printf(
						TEXT("trackClass '%s' cannot be Binding track (see schema)"),
						*TrackClass));
				}
				else
				{
					FString ResolveErr;
					UClass* Class = ResolveLevelSequenceTrackClass(TrackClass, ResolveErr);
					if (!Class)
					{
						OpResult->SetStringField(TEXT("error"), ResolveErr);
					}
					else
					{
						UMovieSceneTrack* NewTrack = Scene->AddTrack(Class, Guid);
						if (NewTrack)
						{
							OpResult->SetStringField(TEXT("bindingGuid"), Guid.ToString());
							OpResult->SetStringField(TEXT("trackClass"), TrackClass.IsEmpty() ? TEXT("Float") : TrackClass);
							OpResult->SetStringField(TEXT("trackType"), NewTrack->GetClass()->GetName());
							bDirty = true;
						}
						else OpResult->SetStringField(TEXT("error"), TEXT("add_track failed"));
					}
				}
			}
			else if (Action == TEXT("add_float_key"))
			{
				FString GuidStr;
				Op->TryGetStringField(TEXT("bindingGuid"), GuidStr);
				double TimeSec = 0.0, KeyVal = 0.0;
				Op->TryGetNumberField(TEXT("time"), TimeSec);
				Op->TryGetNumberField(TEXT("keyValue"), KeyVal);
				FGuid Guid;
				if (!FGuid::Parse(GuidStr, Guid))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_float_key requires valid bindingGuid"));
				}
				else
				{
					UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(
						Scene->FindTrack(UMovieSceneFloatTrack::StaticClass(), Guid));
					if (!FloatTrack)
					{
						OpResult->SetStringField(TEXT("error"), TEXT("This Binding has no Float track; add_track first"));
					}
					else
					{
#if NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
						FString KeyErr;
						if (!AddKeyToFloatTrack(Scene, FloatTrack, TimeSec, static_cast<float>(KeyVal), KeyErr))
						{
							OpResult->SetStringField(TEXT("error"), KeyErr);
						}
						else
						{
							OpResult->SetStringField(TEXT("bindingGuid"), Guid.ToString());
							OpResult->SetNumberField(TEXT("time"), TimeSec);
							OpResult->SetNumberField(TEXT("keyValue"), KeyVal);
							bDirty = true;
						}
#else
						OpResult->SetStringField(TEXT("error"), TEXT("No FloatChannel on this engine; cannot key"));
#endif
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
				const bool bHasPitch = Op->HasField(TEXT("pitch"));
				const bool bHasYaw   = Op->HasField(TEXT("yaw"));
				const bool bHasRoll  = Op->HasField(TEXT("roll"));
				double Pitch = 0, Yaw = 0, Roll = 0;
				if (bHasPitch) Op->TryGetNumberField(TEXT("pitch"), Pitch);
				if (bHasYaw)   Op->TryGetNumberField(TEXT("yaw"), Yaw);
				if (bHasRoll)  Op->TryGetNumberField(TEXT("roll"), Roll);
				FGuid Guid;
				if (!FGuid::Parse(GuidStr, Guid))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("set_transform_key requires bindingGuid"));
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
						OpResult->SetStringField(TEXT("error"), TEXT("Unable to add TransformTrack"));
					}
					else
					{
						UMovieScene3DTransformSection* Section = nullptr;
						for (UMovieSceneSection* S : TTrack->GetAllSections())
						{
							Section = Cast<UMovieScene3DTransformSection>(S);
							if (Section) break;
						}
						if (!Section)
						{
							Section = Cast<UMovieScene3DTransformSection>(TTrack->CreateNewSection());
							if (Section) TTrack->AddSection(*Section);
						}
						if (!Section)
						{
							OpResult->SetStringField(TEXT("error"), TEXT("Unable to create TransformSection"));
						}
						else
						{
#if NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
							const FFrameRate Tick = Scene->GetTickResolution();
							const FFrameNumber Frame = Tick.AsFrameNumber(TimeSec);
							Section->SetRange(TRange<FFrameNumber>::Hull(Section->GetRange(), TRange<FFrameNumber>(Frame, Frame + 1)));
							TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
							// 通道顺序：Location XYZ（0-2），Rotation Roll/Pitch/Yaw（3-5）
							if (Channels.Num() >= 3)
							{
								WriteFloatChannelKey(Scene, Channels[0], TimeSec, static_cast<float>(X));
								WriteFloatChannelKey(Scene, Channels[1], TimeSec, static_cast<float>(Y));
								WriteFloatChannelKey(Scene, Channels[2], TimeSec, static_cast<float>(Z));
							}
							if (Channels.Num() >= 6 && (bHasRoll || bHasPitch || bHasYaw))
							{
								if (bHasRoll)  WriteFloatChannelKey(Scene, Channels[3], TimeSec, static_cast<float>(Roll));
								if (bHasPitch) WriteFloatChannelKey(Scene, Channels[4], TimeSec, static_cast<float>(Pitch));
								if (bHasYaw)   WriteFloatChannelKey(Scene, Channels[5], TimeSec, static_cast<float>(Yaw));
							}
							else if (bHasRoll || bHasPitch || bHasYaw)
							{
								OpResult->SetStringField(TEXT("rotationNote"), TEXT("Rotation channels missing; ignored pitch/yaw/roll"));
							}
							OpResult->SetNumberField(TEXT("x"), X);
							OpResult->SetNumberField(TEXT("y"), Y);
							OpResult->SetNumberField(TEXT("z"), Z);
							if (bHasPitch) OpResult->SetNumberField(TEXT("pitch"), Pitch);
							if (bHasYaw)   OpResult->SetNumberField(TEXT("yaw"), Yaw);
							if (bHasRoll)  OpResult->SetNumberField(TEXT("roll"), Roll);
							bDirty = true;
#else
							OpResult->SetStringField(TEXT("error"), TEXT("No FloatChannel; cannot write Transform keys"));
#endif
						}
					}
				}
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
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
