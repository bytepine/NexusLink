// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusLegacyToolsets.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusEditorToolsets.h"
#include "Utils/NexusVersionCompat.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneStringChannel.h"

template<typename TChannel>
static TChannel* NexusAnimationChannel(UMovieSceneSection* Section, const FString& Name)
{
    auto Channels = Section->GetChannelProxy().GetChannels<TChannel>();
    auto Metadata = Section->GetChannelProxy().GetMetaData<TChannel>();
    for (int32 Index = 0; Index < Channels.Num(); ++Index)
        if (Metadata.IsValidIndex(Index) && Metadata[Index].Name.ToString() == Name) return Channels[Index];
    return nullptr;
}

static TSharedPtr<FJsonValue> NexusAnimationValue(const FMovieSceneFloatValue& Value) { return MakeShared<FJsonValueNumber>(Value.Value); }
static TSharedPtr<FJsonValue> NexusAnimationValue(int32 Value) { return MakeShared<FJsonValueNumber>(Value); }
static TSharedPtr<FJsonValue> NexusAnimationValue(bool Value) { return MakeShared<FJsonValueBoolean>(Value); }
static TSharedPtr<FJsonValue> NexusAnimationValue(const FString& Value) { return MakeShared<FJsonValueString>(Value); }

template<typename TChannel>
static bool NexusAnimationReadChannel(TChannel* Channel, UMovieScene* Scene, const FString& Tool, TSharedPtr<FJsonObject>& Result)
{
    if (!Channel) return false;
    TArray<TSharedPtr<FJsonValue>> Keys; auto Data = Channel->GetData();
    if (Tool == TEXT("get_keys")) {
        const auto Times = Data.GetTimes(); const auto Values = Data.GetValues();
        for (int32 Index = 0; Index < Times.Num(); ++Index) {
            auto Key = MakeShared<FJsonObject>();
            Key->SetNumberField(TEXT("frame"), FFrameRate::TransformTime(FFrameTime(Times[Index]), Scene->GetTickResolution(), Scene->GetDisplayRate()).AsDecimal());
            Key->SetField(TEXT("value"), NexusAnimationValue(Values[Index])); Keys.Add(MakeShared<FJsonValueObject>(Key));
        }
        Result->SetArrayField(TEXT("Items"), Keys);
    }
    return true;
}

bool FNexusLegacyToolsets::Animation(const FString& Domain, const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error)
{
    const FNexusArgs A(Args);
    ULevelSequence* Sequence = Cast<ULevelSequence>(Resolve(Args, TEXT("sequence")));
    UMovieSceneTrack* Track = Cast<UMovieSceneTrack>(Resolve(Args, TEXT("track")));
    UMovieSceneSection* Section = Cast<UMovieSceneSection>(Resolve(Args, TEXT("section")));
    UMovieScene* Scene = Sequence ? Sequence->GetMovieScene() : Section ? Section->GetTypedOuter<UMovieScene>() : Track ? Track->GetTypedOuter<UMovieScene>() : nullptr;
    if (Tool == TEXT("add_track_to_sequence") && Scene) {
        UClass* Class=Cast<UClass>(Resolve(Args,TEXT("track_type")));
        if (!Class || !Class->IsChildOf(UMovieSceneTrack::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) { Error=TEXT("Concrete MovieScene track_type is required"); return false; }
        Scene->Modify();
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
        UMovieSceneTrack* Added=Scene->AddMasterTrack(Class);
#else
        UMovieSceneTrack* Added=Scene->AddTrack(Class);
#endif
        if (!Added) { Error=TEXT("Sequence rejected track class"); return false; }
        Scene->MarkPackageDirty(); Result=FNexusNativeEditorTools::Reference(Added->GetPathName()); return true;
    }
    if (Tool == TEXT("get_bindings") && Scene) {
        TArray<TSharedPtr<FJsonValue>> Bindings;
        for (const auto& Binding : static_cast<const UMovieScene*>(Scene)->GetBindings()) {
            auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Id"), Binding.GetObjectGuid().ToString(EGuidFormats::DigitsWithHyphens)); const auto* Possessable=Scene->FindPossessable(Binding.GetObjectGuid()); const auto* Spawnable=Scene->FindSpawnable(Binding.GetObjectGuid()); Item->SetStringField(TEXT("Name"),Possessable?Possessable->GetName():Spawnable?Spawnable->GetName():Binding.GetObjectGuid().ToString());
            TArray<TSharedPtr<FJsonValue>> Tracks; for (UMovieSceneTrack* BindingTrack : Binding.GetTracks()) if (BindingTrack) Tracks.Add(MakeShared<FJsonValueObject>(FNexusNativeEditorTools::Reference(BindingTrack->GetPathName())));
            Item->SetArrayField(TEXT("Tracks"), Tracks); Bindings.Add(MakeShared<FJsonValueObject>(Item));
        }
        Result->SetArrayField(TEXT("Items"), Bindings); return true;
    }
    if (Tool == TEXT("get_tracks_on_sequence") && Scene) {
        TArray<TSharedPtr<FJsonValue>> Tracks;
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
        for (auto* Item : Scene->GetMasterTracks())
#else
        for (auto* Item : Scene->GetTracks())
#endif
            if (Item) Tracks.Add(MakeShared<FJsonValueObject>(FNexusNativeEditorTools::Reference(Item->GetPathName())));
        Result->SetArrayField(TEXT("Items"), Tracks); return true;
    }
    if (Tool == TEXT("get_sections") && Track) {
        TArray<TSharedPtr<FJsonValue>> Sections; for (auto* Item : Track->GetAllSections()) if (Item) Sections.Add(MakeShared<FJsonValueObject>(FNexusNativeEditorTools::Reference(Item->GetPathName())));
        Result->SetArrayField(TEXT("Items"), Sections); return true;
    }
    if (Tool == TEXT("add_section") && Track) {
        UMovieSceneSection* Added = Track->CreateNewSection(); if (!Added) { Error = TEXT("Track cannot create sections"); return false; }
        Track->Modify(); Added->SetFlags(RF_Transactional); Track->AddSection(*Added); Track->MarkPackageDirty(); Result = FNexusNativeEditorTools::Reference(Added->GetPathName()); return true;
    }
    if (Tool == TEXT("remove_section") && Track && Section) {
        if (!Track->GetAllSections().Contains(Section)) { Error = TEXT("Section does not belong to track"); return false; }
        Track->Modify(); Track->RemoveSection(*Section); Track->MarkPackageDirty(); return true;
    }
    if (!Section || !Scene) { Error = TEXT("This compatibility action requires a valid section/track/sequence reference"); return false; }
    const auto ToTicks = [&](int32 Frame) { return FFrameRate::TransformTime(FFrameTime(Frame), Scene->GetDisplayRate(), Scene->GetTickResolution()).RoundToFrame(); };
    if (Tool == TEXT("get_section_range") || Tool == TEXT("get_section_properties")) {
        if (Section->HasStartFrame()) Result->SetNumberField(TEXT("start"), FFrameRate::TransformTime(FFrameTime(Section->GetInclusiveStartFrame()), Scene->GetTickResolution(), Scene->GetDisplayRate()).AsDecimal());
        if (Section->HasEndFrame()) Result->SetNumberField(TEXT("end"), FFrameRate::TransformTime(FFrameTime(Section->GetExclusiveEndFrame()), Scene->GetTickResolution(), Scene->GetDisplayRate()).AsDecimal());
        Result->SetBoolField(TEXT("is_active"), Section->IsActive()); return true;
    }
    if (Tool == TEXT("set_section_range")) {
        const double Start = A.Num(TEXT("start_frame")), End = A.Num(TEXT("end_frame"));
        if (!Args->HasTypedField<EJson::Number>(TEXT("start_frame")) || !Args->HasTypedField<EJson::Number>(TEXT("end_frame")) || !FMath::IsFinite(Start) || !FMath::IsFinite(End) || Start >= End || Start < MIN_int32 || End > MAX_int32 || FMath::FloorToDouble(Start)!=Start || FMath::FloorToDouble(End)!=End) { Error = TEXT("Expected ordered int32 display frame range"); return false; }
        Section->Modify(); Section->SetRange(TRange<FFrameNumber>(ToTicks(static_cast<int32>(Start)), ToTicks(static_cast<int32>(End)))); Section->MarkPackageDirty(); return true;
    }
    if (Tool == TEXT("set_section_ease_in") || Tool == TEXT("set_section_ease_out")) {
        const double Duration = A.Num(TEXT("duration"), -1); if (!FMath::IsFinite(Duration) || Duration < 0 || Duration > MAX_int32 || FMath::FloorToDouble(Duration)!=Duration) { Error = TEXT("Duration must be a nonnegative int32 frame count"); return false; }
        Section->Modify();
        if (Tool == TEXT("set_section_ease_in")) { Section->Easing.bManualEaseIn = true; Section->Easing.ManualEaseInDuration = ToTicks(static_cast<int32>(Duration)).Value; }
        else { Section->Easing.bManualEaseOut = true; Section->Easing.ManualEaseOutDuration = ToTicks(static_cast<int32>(Duration)).Value; }
        Section->MarkPackageDirty(); return true;
    }
    if (Tool == TEXT("get_channel_names")) {
        TArray<TSharedPtr<FJsonValue>> Names;
        for (const auto& Entry : Section->GetChannelProxy().GetAllEntries()) for (const auto& Metadata : Entry.GetMetaData()) Names.Add(MakeShared<FJsonValueString>(Metadata.Name.ToString()));
        Result->SetArrayField(TEXT("Items"), Names); return true;
    }
    const FString Name = A.Str(TEXT("channel_name"));
    auto* Float = NexusAnimationChannel<FMovieSceneFloatChannel>(Section, Name); auto* Bool = NexusAnimationChannel<FMovieSceneBoolChannel>(Section, Name);
    auto* Integer = NexusAnimationChannel<FMovieSceneIntegerChannel>(Section, Name); auto* String = NexusAnimationChannel<FMovieSceneStringChannel>(Section, Name);
    if (Tool == TEXT("get_keys")) {
        if (NexusAnimationReadChannel(Float, Scene, Tool, Result) || NexusAnimationReadChannel(Bool, Scene, Tool, Result) || NexusAnimationReadChannel(Integer, Scene, Tool, Result) || NexusAnimationReadChannel(String, Scene, Tool, Result)) return true;
    }
    const double Frame = A.Num(TEXT("frame"));
    if (!Args->HasTypedField<EJson::Number>(TEXT("frame")) || !FMath::IsFinite(Frame) || Frame < MIN_int32 || Frame > MAX_int32 || FMath::FloorToDouble(Frame)!=Frame) { Error = TEXT("Key requires an int32 display frame"); return false; }
    const FFrameNumber Tick = ToTicks(static_cast<int32>(Frame));
    if (Tool == TEXT("remove_key_at_frame")) {
        FMovieSceneChannel* Channel=Float?static_cast<FMovieSceneChannel*>(Float):Bool?static_cast<FMovieSceneChannel*>(Bool):Integer?static_cast<FMovieSceneChannel*>(Integer):static_cast<FMovieSceneChannel*>(String);
        if (!Channel) { Error=TEXT("Named typed channel was not found"); return false; }
        TArray<FKeyHandle> Handles; Channel->GetKeys(TRange<FFrameNumber>::Inclusive(Tick,Tick),nullptr,&Handles);
        Section->Modify(); Channel->DeleteKeys(Handles); Section->MarkPackageDirty(); Result->SetNumberField(TEXT("RemovedCount"),Handles.Num()); return true;
    }
    if (Tool == TEXT("add_key_float") && Float && Args->HasTypedField<EJson::Number>(TEXT("value"))) {
        const double Value = A.Num(TEXT("value")); if (!FMath::IsFinite(Value)) { Error = TEXT("Key value must be finite"); return false; }
        FMovieSceneFloatValue Key(static_cast<float>(Value)); const FString Interpolation = A.Str(TEXT("interpolation")).ToLower();
        if (Interpolation.IsEmpty() || Interpolation == TEXT("auto") || Interpolation == TEXT("cubic")) Key.InterpMode = RCIM_Cubic;
        else if (Interpolation == TEXT("linear")) Key.InterpMode = RCIM_Linear;
        else if (Interpolation == TEXT("constant")) Key.InterpMode = RCIM_Constant;
        else { Error = TEXT("Compatibility interpolation must be Auto, Linear or Constant"); return false; }
        Section->Modify(); Float->GetData().UpdateOrAddKey(Tick, Key); Float->AutoSetTangents();
    } else if (Tool == TEXT("add_key_bool") && Bool && Args->HasTypedField<EJson::Boolean>(TEXT("value"))) { Section->Modify(); Bool->GetData().UpdateOrAddKey(Tick, A.Bool(TEXT("value"))); }
    else if (Tool == TEXT("add_key_integer") && Integer && Args->HasTypedField<EJson::Number>(TEXT("value")) && A.Num(TEXT("value")) >= MIN_int32 && A.Num(TEXT("value")) <= MAX_int32 && A.Num(TEXT("value")) == static_cast<int32>(A.Num(TEXT("value")))) { Section->Modify(); Integer->GetData().UpdateOrAddKey(Tick, static_cast<int32>(A.Num(TEXT("value")))); }
    else if (Tool == TEXT("add_key_string") && String && Args->HasTypedField<EJson::String>(TEXT("value"))) { Section->Modify(); String->GetData().UpdateOrAddKey(Tick, A.Str(TEXT("value"))); }
    else { Error = TEXT("Unsupported compatibility action/channel, or invalid key value"); return false; }
    Section->MarkPackageDirty(); return true;
}
