// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Sequencer/NexusManageAssetLevelSequenceCapability.h"
#include "Utils/NexusArgs.h"
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

/** 解析 trackClass → UClass；未知返回 nullptr 并写 OutError。 */
static UClass* ResolveLevelSequenceTrackClass(const FString& TrackClass, FString& OutError)
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

static bool IsMasterTrackClassName(const FString& TrackClass)
{
	return TrackClass.Equals(TEXT("CameraCut"), ESearchCase::IgnoreCase)
		|| TrackClass.Equals(TEXT("Audio"), ESearchCase::IgnoreCase)
		|| TrackClass.Equals(TEXT("CinematicShot"), ESearchCase::IgnoreCase)
		|| TrackClass.Equals(TEXT("Fade"), ESearchCase::IgnoreCase)
		|| TrackClass.Equals(TEXT("Event"), ESearchCase::IgnoreCase)
		|| TrackClass.Equals(TEXT("LevelVisibility"), ESearchCase::IgnoreCase)
		|| TrackClass.Equals(TEXT("Slomo"), ESearchCase::IgnoreCase);
}

static bool IsBindingTrackClassName(const FString& TrackClass)
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

#if WITH_EDITOR
static ULevelSequence* LSFrom(FNexusActionContext& Ctx)
{
	return static_cast<ULevelSequence*>(Ctx.Target);
}

static UMovieScene* SceneFrom(FNexusActionContext& Ctx)
{
	ULevelSequence* LS = LSFrom(Ctx);
	return LS ? LS->GetMovieScene() : nullptr;
}

static void MarkLSDirty(FNexusActionContext& Ctx)
{
	if (ULevelSequence* LS = LSFrom(Ctx))
	{
		LS->MarkPackageDirty();
	}
}

static void HandleLS_SetDisplayRate(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMovieScene* Scene = SceneFrom(Ctx);
	if (!Scene) { Ctx.Entry->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene data")); return; }
	int64 Num = 30, Den = 1;
	Op->TryGetNumberField(TEXT("numerator"),   Num);
	Op->TryGetNumberField(TEXT("denominator"), Den);
	if (Den <= 0) Den = 1;
	Scene->SetDisplayRate(FFrameRate(static_cast<int32>(Num), static_cast<int32>(Den)));
	Ctx.Entry->SetStringField(TEXT("displayRate"), FString::Printf(TEXT("%d/%d"), (int32)Num, (int32)Den));
	MarkLSDirty(Ctx);
}

static void HandleLS_SetPlaybackRange(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMovieScene* Scene = SceneFrom(Ctx);
	if (!Scene) { Ctx.Entry->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene data")); return; }
	int64 StartFrame = 0, EndFrame = 0;
	const bool bHasStart = Op->TryGetNumberField(TEXT("startFrame"), StartFrame);
	const bool bHasEnd   = Op->TryGetNumberField(TEXT("endFrame"), EndFrame);
	if (!bHasStart && !bHasEnd)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_playback_range requires startFrame or endFrame"));
		return;
	}
	TRange<FFrameNumber> Current = Scene->GetPlaybackRange();
	FFrameNumber Start = bHasStart ? FFrameNumber(static_cast<int32>(StartFrame)) : Current.GetLowerBoundValue();
	FFrameNumber End   = bHasEnd   ? FFrameNumber(static_cast<int32>(EndFrame))   : Current.GetUpperBoundValue();
	Scene->SetPlaybackRange(TRange<FFrameNumber>(Start, End));
	MarkLSDirty(Ctx);
}

static void HandleLS_RemoveBinding(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMovieScene* Scene = SceneFrom(Ctx);
	if (!Scene) { Ctx.Entry->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene data")); return; }
	const FString GuidStr = FNexusArgs(Op).Str(TEXT("bindingGuid"));
	if (GuidStr.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_binding requires bindingGuid"));
		return;
	}
	FGuid Guid;
	if (!FGuid::Parse(GuidStr, Guid))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid GUID: %s"), *GuidStr));
		return;
	}
	if (Scene->RemovePossessable(Guid) || Scene->RemoveSpawnable(Guid))
	{
		MarkLSDirty(Ctx);
	}
	else
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_binding: matching binding not found"));
	}
}

static void HandleLS_AddMasterTrack(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMovieScene* Scene = SceneFrom(Ctx);
	if (!Scene) { Ctx.Entry->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene data")); return; }
	const FString TrackClass = FNexusArgs(Op).Str(TEXT("trackClass"));
	if (TrackClass.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_master_track requires trackClass"));
		return;
	}
	if (!IsMasterTrackClassName(TrackClass))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(
			TEXT("trackClass '%s' cannot be MasterTrack (supports CameraCut/Audio/…/Slomo)"),
			*TrackClass));
		return;
	}
	FString ResolveErr;
	UClass* Class = ResolveLevelSequenceTrackClass(TrackClass, ResolveErr);
	if (!Class)
	{
		Ctx.Entry->SetStringField(TEXT("error"), ResolveErr);
		return;
	}
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
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("%s MasterTrack already exists"), *TrackClass));
	}
	else
	{
		UMovieSceneTrack* NewTrack = Scene->AddMasterTrack(Class);
		if (NewTrack)
		{
			Ctx.Entry->SetStringField(TEXT("trackClass"), TrackClass);
			Ctx.Entry->SetStringField(TEXT("trackType"), NewTrack->GetClass()->GetName());
			MarkLSDirty(Ctx);
		}
		else Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_master_track failed"));
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
	UMovieSceneTrack* NewTrack = Scene->AddTrack(Class);
	if (NewTrack)
	{
		Ctx.Entry->SetStringField(TEXT("trackClass"), TrackClass);
		Ctx.Entry->SetStringField(TEXT("trackType"), NewTrack->GetClass()->GetName());
		MarkLSDirty(Ctx);
	}
	else Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_master_track failed"));
#endif
}

static void HandleLS_RemoveMasterTrack(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMovieScene* Scene = SceneFrom(Ctx);
	if (!Scene) { Ctx.Entry->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene data")); return; }
	const FString TrackClass = FNexusArgs(Op).Str(TEXT("trackClass"));
	if (TrackClass.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_master_track requires trackClass"));
		return;
	}
	if (!IsMasterTrackClassName(TrackClass))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(
			TEXT("trackClass '%s' is not a MasterTrack type"), *TrackClass));
		return;
	}
	FString ResolveErr;
	UClass* Class = ResolveLevelSequenceTrackClass(TrackClass, ResolveErr);
	if (!Class)
	{
		Ctx.Entry->SetStringField(TEXT("error"), ResolveErr);
		return;
	}
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UMovieSceneTrack* Found = Scene->FindMasterTrack(Class);
	const bool bOk = Found && Scene->RemoveMasterTrack(*Found);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (bOk)
	{
		Ctx.Entry->SetStringField(TEXT("trackClass"), TrackClass);
		MarkLSDirty(Ctx);
	}
	else Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_master_track: matching Track not found"));
#else
	UMovieSceneTrack* Found = nullptr;
	for (UMovieSceneTrack* T : Scene->GetTracks())
	{
		if (T && T->GetClass() == Class) { Found = T; break; }
	}
	const bool bOk = Found && Scene->RemoveTrack(*Found);
	if (bOk)
	{
		Ctx.Entry->SetStringField(TEXT("trackClass"), TrackClass);
		MarkLSDirty(Ctx);
	}
	else Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_master_track: matching Track not found"));
#endif
}

static UClass* ResolveActorClass(const FString& ClassName)
{
	UClass* Cls = AActor::StaticClass();
	if (!ClassName.IsEmpty())
	{
		if (UClass* Found = FNexusAssetUtils::FindClassWithUPrefix(ClassName))
		{
			Cls = Found;
		}
	}
	return Cls;
}

static void HandleLS_AddPossessable(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMovieScene* Scene = SceneFrom(Ctx);
	if (!Scene) { Ctx.Entry->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene data")); return; }
	FString PossessName = FNexusArgs(Op).Str(TEXT("possessableName"));
	if (PossessName.IsEmpty()) PossessName = TEXT("Possessable");
	const FGuid Guid = Scene->AddPossessable(PossessName, ResolveActorClass(FNexusArgs(Op).Str(TEXT("className"))));
	Ctx.Entry->SetStringField(TEXT("bindingGuid"), Guid.ToString());
	Ctx.Entry->SetStringField(TEXT("possessableName"), PossessName);
	MarkLSDirty(Ctx);
}

static void HandleLS_AddSpawnable(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMovieScene* Scene = SceneFrom(Ctx);
	if (!Scene) { Ctx.Entry->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene data")); return; }
	FString SpawnName = FNexusArgs(Op).Str(TEXT("possessableName"));
	if (SpawnName.IsEmpty()) SpawnName = TEXT("Spawnable");
	UClass* Cls = ResolveActorClass(FNexusArgs(Op).Str(TEXT("className")));
	UObject* Template = NewObject<UObject>(Scene, Cls, NAME_None, RF_Transactional);
	if (!Template)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to create spawnable template"));
		return;
	}
	const FGuid Guid = Scene->AddSpawnable(SpawnName, *Template);
	Ctx.Entry->SetStringField(TEXT("bindingGuid"), Guid.ToString());
	MarkLSDirty(Ctx);
}

static void HandleLS_AddTrack(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMovieScene* Scene = SceneFrom(Ctx);
	if (!Scene) { Ctx.Entry->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene data")); return; }
	const FString GuidStr = FNexusArgs(Op).Str(TEXT("bindingGuid"));
	const FString TrackClass = FNexusArgs(Op).Str(TEXT("trackClass"));
	FGuid Guid;
	if (!FGuid::Parse(GuidStr, Guid))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_track requires valid bindingGuid"));
		return;
	}
	if (!IsBindingTrackClassName(TrackClass))
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(
			TEXT("trackClass '%s' cannot be Binding track (see schema)"),
			*TrackClass));
		return;
	}
	FString ResolveErr;
	UClass* Class = ResolveLevelSequenceTrackClass(TrackClass, ResolveErr);
	if (!Class)
	{
		Ctx.Entry->SetStringField(TEXT("error"), ResolveErr);
		return;
	}
	UMovieSceneTrack* NewTrack = Scene->AddTrack(Class, Guid);
	if (!NewTrack)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_track failed"));
		return;
	}
	Ctx.Entry->SetStringField(TEXT("bindingGuid"), Guid.ToString());
	Ctx.Entry->SetStringField(TEXT("trackClass"), TrackClass.IsEmpty() ? TEXT("Float") : TrackClass);
	Ctx.Entry->SetStringField(TEXT("trackType"), NewTrack->GetClass()->GetName());
	MarkLSDirty(Ctx);
}

static void HandleLS_AddFloatKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMovieScene* Scene = SceneFrom(Ctx);
	if (!Scene) { Ctx.Entry->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene data")); return; }
	const FString GuidStr = FNexusArgs(Op).Str(TEXT("bindingGuid"));
	const double TimeSec = FNexusArgs(Op).Num(TEXT("time"));
	const double KeyVal = FNexusArgs(Op).Num(TEXT("keyValue"));
	FGuid Guid;
	if (!FGuid::Parse(GuidStr, Guid))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_float_key requires valid bindingGuid"));
		return;
	}
	UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(
		Scene->FindTrack(UMovieSceneFloatTrack::StaticClass(), Guid));
	if (!FloatTrack)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("This Binding has no Float track; add_track first"));
		return;
	}
#if NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
	FString KeyErr;
	if (!AddKeyToFloatTrack(Scene, FloatTrack, TimeSec, static_cast<float>(KeyVal), KeyErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), KeyErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("bindingGuid"), Guid.ToString());
	Ctx.Entry->SetNumberField(TEXT("time"), TimeSec);
	Ctx.Entry->SetNumberField(TEXT("keyValue"), KeyVal);
	MarkLSDirty(Ctx);
#else
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("No FloatChannel on this engine; cannot key"));
#endif
}

static void HandleLS_SetTransformKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMovieScene* Scene = SceneFrom(Ctx);
	if (!Scene) { Ctx.Entry->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene data")); return; }
	const FString GuidStr = FNexusArgs(Op).Str(TEXT("bindingGuid"));
	const double TimeSec = FNexusArgs(Op).Num(TEXT("time"));
	const double X = FNexusArgs(Op).Num(TEXT("x"));
	const double Y = FNexusArgs(Op).Num(TEXT("y"));
	const double Z = FNexusArgs(Op).Num(TEXT("z"));
	const bool bHasPitch = Op->HasField(TEXT("pitch"));
	const bool bHasYaw   = Op->HasField(TEXT("yaw"));
	const bool bHasRoll  = Op->HasField(TEXT("roll"));
	const double Pitch = bHasPitch ? FNexusArgs(Op).Num(TEXT("pitch")) : 0.0;
	const double Yaw   = bHasYaw   ? FNexusArgs(Op).Num(TEXT("yaw"))   : 0.0;
	const double Roll  = bHasRoll  ? FNexusArgs(Op).Num(TEXT("roll"))  : 0.0;
	FGuid Guid;
	if (!FGuid::Parse(GuidStr, Guid))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_transform_key requires bindingGuid"));
		return;
	}
	UMovieScene3DTransformTrack* TTrack = Cast<UMovieScene3DTransformTrack>(
		Scene->FindTrack(UMovieScene3DTransformTrack::StaticClass(), Guid));
	if (!TTrack)
	{
		TTrack = Cast<UMovieScene3DTransformTrack>(Scene->AddTrack(UMovieScene3DTransformTrack::StaticClass(), Guid));
	}
	if (!TTrack)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to add TransformTrack"));
		return;
	}
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
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to create TransformSection"));
		return;
	}
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
		Ctx.Entry->SetStringField(TEXT("rotationNote"), TEXT("Rotation channels missing; ignored pitch/yaw/roll"));
	}
	Ctx.Entry->SetNumberField(TEXT("x"), X);
	Ctx.Entry->SetNumberField(TEXT("y"), Y);
	Ctx.Entry->SetNumberField(TEXT("z"), Z);
	if (bHasPitch) Ctx.Entry->SetNumberField(TEXT("pitch"), Pitch);
	if (bHasYaw)   Ctx.Entry->SetNumberField(TEXT("yaw"), Yaw);
	if (bHasRoll)  Ctx.Entry->SetNumberField(TEXT("roll"), Roll);
	MarkLSDirty(Ctx);
#else
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("No FloatChannel; cannot write Transform keys"));
#endif
}
#endif // WITH_EDITOR

bool FManageAssetLevelSequenceCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if WITH_EDITOR
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	ULevelSequence* LS = FNexusAssetUtils::LoadAssetWithFallback<ULevelSequence>(AssetPath);
	if (!LS)
	{
		OutError = FString::Printf(TEXT("LevelSequence not found: %s"), *AssetPath);
		return false;
	}
	if (!LS->GetMovieScene())
	{
		OutError = TEXT("LevelSequence has no MovieScene data");
		return false;
	}
	OutTarget = LS;
	return true;
#else
	OutError = TEXT("manage_asset_level_sequence only available in Editor builds");
	return false;
#endif
}

void FManageAssetLevelSequenceCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("set_display_rate"),   &HandleLS_SetDisplayRate);
	OutHandlers.Add(TEXT("set_playback_range"), &HandleLS_SetPlaybackRange);
	OutHandlers.Add(TEXT("remove_binding"),     &HandleLS_RemoveBinding);
	OutHandlers.Add(TEXT("add_master_track"),   &HandleLS_AddMasterTrack);
	OutHandlers.Add(TEXT("remove_master_track"),&HandleLS_RemoveMasterTrack);
	OutHandlers.Add(TEXT("add_possessable"),    &HandleLS_AddPossessable);
	OutHandlers.Add(TEXT("add_spawnable"),      &HandleLS_AddSpawnable);
	OutHandlers.Add(TEXT("add_track"),          &HandleLS_AddTrack);
	OutHandlers.Add(TEXT("add_float_key"),      &HandleLS_AddFloatKey);
	OutHandlers.Add(TEXT("set_transform_key"),  &HandleLS_SetTransformKey);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetLevelSequenceCapability)
