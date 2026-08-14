// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusWidgetAnimationUtils.h"
#include "Utils/NexusVersionCompat.h"

#if WITH_EDITOR

#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#if NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "KeyParams.h"
#endif

UWidgetAnimation* FNexusWidgetAnimationUtils::FindAnimation(UWidgetBlueprint* WBP, const FString& Name)
{
	if (!WBP || Name.IsEmpty()) return nullptr;
	for (UWidgetAnimation* Anim : WBP->Animations)
	{
		if (!Anim) continue;
		if (Anim->GetName() == Name) return Anim;
		if (Anim->GetDisplayLabel() == Name) return Anim;
	}
	return nullptr;
}

UWidgetAnimation* FNexusWidgetAnimationUtils::AddAnimation(UWidgetBlueprint* WBP, const FString& Name, FString& OutError)
{
	if (!WBP) { OutError = TEXT("WidgetBlueprint 无效"); return nullptr; }
	if (Name.IsEmpty()) { OutError = TEXT("animationName 必填"); return nullptr; }
	if (FindAnimation(WBP, Name)) { OutError = FString::Printf(TEXT("动画已存在: %s"), *Name); return nullptr; }

	UWidgetAnimation* Anim = NewObject<UWidgetAnimation>(WBP, FName(*Name), RF_Transactional);
	if (!Anim) { OutError = TEXT("创建 UWidgetAnimation 失败"); return nullptr; }
	UMovieScene* Scene = NewObject<UMovieScene>(Anim, NAME_None, RF_Transactional);
	if (!Scene) { OutError = TEXT("创建 MovieScene 失败"); return nullptr; }
	Scene->SetDisplayRate(FFrameRate(30, 1));
#if NX_UE_HAS_WIDGET_ANIM_SET_MOVIE_SCENE
	Anim->SetMovieScene(Scene);
#else
	Anim->MovieScene = Scene;
#endif
	WBP->Modify();
	WBP->Animations.Add(Anim);
	return Anim;
}

bool FNexusWidgetAnimationUtils::RemoveAnimation(UWidgetBlueprint* WBP, const FString& Name, FString& OutError)
{
	if (!WBP) { OutError = TEXT("WidgetBlueprint 无效"); return false; }
	UWidgetAnimation* Anim = FindAnimation(WBP, Name);
	if (!Anim) { OutError = FString::Printf(TEXT("动画未找到: %s"), *Name); return false; }
	WBP->Modify();
	WBP->Animations.Remove(Anim);
	return true;
}

static UMovieScene* GetAnimScene(UWidgetAnimation* Anim)
{
	if (!Anim) return nullptr;
#if NX_UE_HAS_WIDGET_ANIM_SET_MOVIE_SCENE
	return Anim->GetMovieScene();
#else
	return Anim->MovieScene;
#endif
}

bool FNexusWidgetAnimationUtils::AddFloatTrack(UWidgetAnimation* Anim, const FString& TrackName, FString& OutTrackName, FString& OutError)
{
	UMovieScene* Scene = GetAnimScene(Anim);
	if (!Scene) { OutError = TEXT("动画无 MovieScene"); return false; }
	UMovieSceneFloatTrack* Track = nullptr;
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
	Track = Scene->AddMasterTrack<UMovieSceneFloatTrack>();
#else
	Track = Scene->AddTrack<UMovieSceneFloatTrack>();
#endif
	if (!Track) { OutError = TEXT("添加 FloatTrack 失败"); return false; }
	const FString Display = TrackName.IsEmpty() ? TEXT("Float") : TrackName;
	Track->SetDisplayName(FText::FromString(Display));
	UMovieSceneSection* Section = Track->CreateNewSection();
	if (Section)
	{
		Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(30)));
		Track->AddSection(*Section);
	}
	OutTrackName = Display;
	return true;
}

bool FNexusWidgetAnimationUtils::AddFloatKey(UWidgetAnimation* Anim, float TimeSeconds, float Value, FString& OutError)
{
	UMovieScene* Scene = GetAnimScene(Anim);
	if (!Scene) { OutError = TEXT("动画无 MovieScene"); return false; }

	UMovieSceneFloatTrack* FloatTrack = nullptr;
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
	const TArray<UMovieSceneTrack*>& Tracks = Scene->GetMasterTracks();
#else
	const TArray<UMovieSceneTrack*>& Tracks = Scene->GetTracks();
#endif
	for (UMovieSceneTrack* T : Tracks)
	{
		FloatTrack = Cast<UMovieSceneFloatTrack>(T);
		if (FloatTrack) break;
	}
	if (!FloatTrack) { OutError = TEXT("没有 Float 轨，先 add_track"); return false; }

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
	if (!Section) { OutError = TEXT("无法创建 FloatSection"); return false; }

	const FFrameRate Tick = Scene->GetTickResolution();
	const FFrameNumber Frame = Tick.AsFrameNumber(TimeSeconds);
	Section->SetRange(TRange<FFrameNumber>::Hull(Section->GetRange(), TRange<FFrameNumber>(Frame, Frame + 1)));

#if NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	TArrayView<FMovieSceneFloatChannel*> Channels = Proxy.GetChannels<FMovieSceneFloatChannel>();
	if (Channels.Num() == 0) { OutError = TEXT("FloatSection 无 FloatChannel"); return false; }
	AddKeyToChannel(Channels[0], Frame, Value, EMovieSceneKeyInterpolation::Auto);
#else
	Section->FloatCurve.AddKey(TimeSeconds, Value);
#endif
	return true;
}

void FNexusWidgetAnimationUtils::AppendTrackSummaries(UWidgetAnimation* Anim, TArray<TSharedPtr<FJsonValue>>& OutTracks)
{
	UMovieScene* Scene = GetAnimScene(Anim);
	if (!Scene) return;
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
	const TArray<UMovieSceneTrack*>& Tracks = Scene->GetMasterTracks();
#else
	const TArray<UMovieSceneTrack*>& Tracks = Scene->GetTracks();
#endif
	for (UMovieSceneTrack* T : Tracks)
	{
		if (!T) continue;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("trackClass"), T->GetClass()->GetName());
		Obj->SetStringField(TEXT("displayName"), T->GetDisplayName().ToString());
		Obj->SetNumberField(TEXT("sectionsCount"), T->GetAllSections().Num());
		OutTracks.Add(MakeShared<FJsonValueObject>(Obj));
	}
}

#endif // WITH_EDITOR
