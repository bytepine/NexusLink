// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusWidgetAnimationUtils.h"
#include "Utils/NexusVersionCompat.h"

#if WITH_EDITOR

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieScenePossessable.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#if NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "KeyParams.h"
#include "Curves/KeyHandle.h"
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
	if (!WBP) { OutError = TEXT("Invalid WidgetBlueprint"); return nullptr; }
	if (Name.IsEmpty()) { OutError = TEXT("animationName is required"); return nullptr; }
	if (FindAnimation(WBP, Name)) { OutError = FString::Printf(TEXT("Animation already exists: %s"), *Name); return nullptr; }

	UWidgetAnimation* Anim = NewObject<UWidgetAnimation>(WBP, FName(*Name), RF_Transactional);
	if (!Anim) { OutError = TEXT("Create UWidgetAnimation failed"); return nullptr; }
	UMovieScene* Scene = NewObject<UMovieScene>(Anim, NAME_None, RF_Transactional);
	if (!Scene) { OutError = TEXT("Create MovieScene failed"); return nullptr; }
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
	if (!WBP) { OutError = TEXT("Invalid WidgetBlueprint"); return false; }
	UWidgetAnimation* Anim = FindAnimation(WBP, Name);
	if (!Anim) { OutError = FString::Printf(TEXT("Animation not found: %s"), *Name); return false; }
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
	if (!Scene) { OutError = TEXT("Animation has no MovieScene"); return false; }
	UMovieSceneFloatTrack* Track = nullptr;
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
	Track = Scene->AddMasterTrack<UMovieSceneFloatTrack>();
#else
	Track = Scene->AddTrack<UMovieSceneFloatTrack>();
#endif
	if (!Track) { OutError = TEXT("Failed to add FloatTrack"); return false; }
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

static TArray<UMovieSceneTrack*> CollectAllAnimTracks(UMovieScene* Scene)
{
	TArray<UMovieSceneTrack*> Result;
	if (!Scene) return Result;
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
	Result.Append(Scene->GetMasterTracks());
#else
	Result.Append(Scene->GetTracks());
#endif
	const int32 NumPos = Scene->GetPossessableCount();
	for (int32 i = 0; i < NumPos; ++i)
	{
		const FGuid Id = Scene->GetPossessable(i).GetGuid();
		if (UMovieSceneTrack* T = Scene->FindTrack(UMovieSceneFloatTrack::StaticClass(), Id))
		{
			Result.AddUnique(T);
		}
	}
	return Result;
}

static UMovieSceneFloatTrack* FindFloatTrackByName(UMovieScene* Scene, const FString& TrackName)
{
	const TArray<UMovieSceneTrack*> Tracks = CollectAllAnimTracks(Scene);
	for (UMovieSceneTrack* T : Tracks)
	{
		UMovieSceneFloatTrack* FT = Cast<UMovieSceneFloatTrack>(T);
		if (!FT) continue;
		if (TrackName.IsEmpty() || FT->GetDisplayName().ToString().Equals(TrackName, ESearchCase::IgnoreCase))
		{
			return FT;
		}
	}
	return nullptr;
}

static TArray<FWidgetAnimationBinding>* GetMutableBindings(UWidgetAnimation* Anim)
{
	if (!Anim) return nullptr;
	FArrayProperty* Prop = FindFProperty<FArrayProperty>(UWidgetAnimation::StaticClass(), TEXT("AnimationBindings"));
	if (!Prop) return nullptr;
	return Prop->ContainerPtrToValuePtr<TArray<FWidgetAnimationBinding>>(Anim);
}

bool FNexusWidgetAnimationUtils::AddBoundFloatTrack(UWidgetAnimation* Anim, UWidgetBlueprint* WBP,
	const FString& WidgetName, const FString& PropertyPath,
	const FString& TrackName, FString& OutTrackName, FString& OutError)
{
	UMovieScene* Scene = GetAnimScene(Anim);
	if (!Scene) { OutError = TEXT("Animation has no MovieScene"); return false; }
	if (!WBP || !WBP->WidgetTree) { OutError = TEXT("WidgetTree unavailable"); return false; }
	UWidget* Widget = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget) { OutError = FString::Printf(TEXT("Widget not found: %s"), *WidgetName); return false; }

	FGuid Guid;
	TArray<FWidgetAnimationBinding>* Bindings = GetMutableBindings(Anim);
	if (Bindings)
	{
		for (const FWidgetAnimationBinding& B : *Bindings)
		{
			if (B.WidgetName == Widget->GetFName())
			{
				Guid = B.AnimationGuid;
				break;
			}
		}
	}
	if (!Guid.IsValid())
	{
		Guid = Scene->AddPossessable(WidgetName, Widget->GetClass());
		if (Bindings)
		{
			FWidgetAnimationBinding NewB;
			NewB.WidgetName = Widget->GetFName();
			NewB.AnimationGuid = Guid;
			NewB.bIsRootWidget = (WBP->WidgetTree->RootWidget == Widget);
			Bindings->Add(NewB);
		}
	}

	UMovieSceneFloatTrack* Track = Cast<UMovieSceneFloatTrack>(
		Scene->AddTrack(UMovieSceneFloatTrack::StaticClass(), Guid));
	if (!Track) { OutError = TEXT("Failed to add bound FloatTrack"); return false; }
	const FString Display = TrackName.IsEmpty()
		? (PropertyPath.IsEmpty() ? TEXT("Float") : PropertyPath)
		: TrackName;
	Track->SetDisplayName(FText::FromString(Display));
	if (!PropertyPath.IsEmpty())
	{
		Track->SetPropertyNameAndPath(FName(*PropertyPath), PropertyPath);
	}
	UMovieSceneSection* Section = Track->CreateNewSection();
	if (Section)
	{
		Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(30)));
		Track->AddSection(*Section);
	}
	OutTrackName = Display;
	return true;
}

static bool WriteKeyOnFloatTrack(UMovieScene* Scene, UMovieSceneFloatTrack* FloatTrack, float TimeSeconds, float Value, FString& OutError)
{
	if (!Scene || !FloatTrack) { OutError = TEXT("Invalid FloatTrack"); return false; }
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
	if (!Section) { OutError = TEXT("Unable to create FloatSection"); return false; }

	const FFrameRate Tick = Scene->GetTickResolution();
	const FFrameNumber Frame = Tick.AsFrameNumber(TimeSeconds);
	Section->SetRange(TRange<FFrameNumber>::Hull(Section->GetRange(), TRange<FFrameNumber>(Frame, Frame + 1)));

#if NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	TArrayView<FMovieSceneFloatChannel*> Channels = Proxy.GetChannels<FMovieSceneFloatChannel>();
	if (Channels.Num() == 0) { OutError = TEXT("FloatSection has no FloatChannel"); return false; }
	AddKeyToChannel(Channels[0], Frame, Value, EMovieSceneKeyInterpolation::Auto);
#else
	Section->FloatCurve.AddKey(TimeSeconds, Value);
#endif
	return true;
}

bool FNexusWidgetAnimationUtils::AddFloatKey(UWidgetAnimation* Anim, float TimeSeconds, float Value, FString& OutError)
{
	return AddFloatKey(Anim, FString(), TimeSeconds, Value, OutError);
}

bool FNexusWidgetAnimationUtils::AddFloatKey(UWidgetAnimation* Anim, const FString& TrackName, float TimeSeconds, float Value, FString& OutError)
{
	UMovieScene* Scene = GetAnimScene(Anim);
	if (!Scene) { OutError = TEXT("Animation has no MovieScene"); return false; }
	UMovieSceneFloatTrack* FloatTrack = FindFloatTrackByName(Scene, TrackName);
	if (!FloatTrack) { OutError = TEXT("No Float track; add_track first"); return false; }
	return WriteKeyOnFloatTrack(Scene, FloatTrack, TimeSeconds, Value, OutError);
}

bool FNexusWidgetAnimationUtils::RemoveFloatTrack(UWidgetAnimation* Anim, const FString& TrackName, FString& OutError)
{
	UMovieScene* Scene = GetAnimScene(Anim);
	if (!Scene) { OutError = TEXT("Animation has no MovieScene"); return false; }
	if (TrackName.IsEmpty()) { OutError = TEXT("remove_track requires trackName"); return false; }
	UMovieSceneFloatTrack* Track = FindFloatTrackByName(Scene, TrackName);
	if (!Track) { OutError = FString::Printf(TEXT("Track not found: %s"), *TrackName); return false; }
	const bool bRemoved = Scene->RemoveTrack(*Track);
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
	const bool bRemovedMaster = !bRemoved && Scene->RemoveMasterTrack(*Track);
#else
	const bool bRemovedMaster = false;
#endif
	if (!bRemoved && !bRemovedMaster)
	{
		OutError = FString::Printf(TEXT("Failed to remove track: %s"), *TrackName);
		return false;
	}
	return true;
}

bool FNexusWidgetAnimationUtils::RemoveFloatKey(UWidgetAnimation* Anim, const FString& TrackName, float TimeSeconds, FString& OutError)
{
	UMovieScene* Scene = GetAnimScene(Anim);
	if (!Scene) { OutError = TEXT("Animation has no MovieScene"); return false; }
	UMovieSceneFloatTrack* FloatTrack = FindFloatTrackByName(Scene, TrackName);
	if (!FloatTrack) { OutError = TEXT("No Float track"); return false; }
	UMovieSceneFloatSection* Section = nullptr;
	for (UMovieSceneSection* S : FloatTrack->GetAllSections())
	{
		Section = Cast<UMovieSceneFloatSection>(S);
		if (Section) break;
	}
	if (!Section) { OutError = TEXT("Float track has no Section"); return false; }

	const FFrameRate Tick = Scene->GetTickResolution();
	const FFrameNumber Frame = Tick.AsFrameNumber(TimeSeconds);
	bool bRemoved = false;
#if NX_UE_HAS_MOVIE_SCENE_FLOAT_CHANNEL
	TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	if (Channels.Num() == 0) { OutError = TEXT("FloatSection has no FloatChannel"); return false; }
	FMovieSceneFloatChannel* Channel = Channels[0];
	TArray<FFrameNumber> Times;
	TArray<FKeyHandle> Handles;
	Channel->GetKeys(TRange<FFrameNumber>::All(), &Times, &Handles);
	for (int32 i = 0; i < Times.Num(); ++i)
	{
		if (Times[i] == Frame && Handles.IsValidIndex(i))
		{
			const FKeyHandle Handle = Handles[i];
			Channel->DeleteKeys(TArrayView<const FKeyHandle>(&Handle, 1));
			bRemoved = true;
			break;
		}
	}
#else
	TArray<FRichCurveKey>& Keys = Section->FloatCurve.Keys;
	for (int32 i = Keys.Num() - 1; i >= 0; --i)
	{
		if (FMath::IsNearlyEqual(Keys[i].Time, TimeSeconds, 0.001f))
		{
			Section->FloatCurve.DeleteKey(Section->FloatCurve.GetKeyHandle(i));
			bRemoved = true;
			break;
		}
	}
#endif
	if (!bRemoved)
	{
		OutError = FString::Printf(TEXT("No keyframe at time %.3f"), TimeSeconds);
		return false;
	}
	return true;
}

void FNexusWidgetAnimationUtils::AppendTrackSummaries(UWidgetAnimation* Anim, TArray<TSharedPtr<FJsonValue>>& OutTracks)
{
	UMovieScene* Scene = GetAnimScene(Anim);
	if (!Scene) return;
	for (UMovieSceneTrack* T : CollectAllAnimTracks(Scene))
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
