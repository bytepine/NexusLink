// Copyright byteyang. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusAnimationToolActions.h"
#include "Utils/NexusAnimationAdvancedTools.h"
#include "Utils/NexusVersionCompat.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Channels/MovieSceneChannelProxy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusIndependentAnimationTest, "NexusLink.Animation.IndependentContractsAndChannels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusIndependentAnimationTest::RunTest(const FString&)
{
    for (bool bWrite : {false, true}) for (const auto& Action : FNexusAnimationToolActions::Get(bWrite)) {
        int32 Separator = INDEX_NONE; Action.Value.FindLastChar(TEXT('.'), Separator);
        TestTrue(*Action.Key, FNexusAnimationAdvancedTools::InputSchema(Action.Value.Left(Separator), Action.Value.Mid(Separator + 1)).IsValid());
    }
    ULevelSequence* Sequence = NewObject<ULevelSequence>(); Sequence->Initialize();
    UMovieScene* Scene = Sequence->GetMovieScene();
    if (!TestNotNull(TEXT("MovieScene"), Scene)) return false;
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
    auto* Track = Scene->AddMasterTrack<UMovieSceneFloatTrack>();
#else
    auto* Track = Scene->AddTrack<UMovieSceneFloatTrack>();
#endif
    if (!TestNotNull(TEXT("Float track"), Track)) return false;
    auto* Section = Cast<UMovieSceneFloatSection>(Track->CreateNewSection());
    if (!TestNotNull(TEXT("Float section"), Section)) return false;
    Track->AddSection(*Section);
    const auto Metadata = Section->GetChannelProxy().GetMetaData<FMovieSceneFloatChannel>();
    if (!TestTrue(TEXT("Named float channel"), Metadata.Num() > 0)) return false;
    TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("section"), FNexusEditorDomainTools::Reference(Section->GetPathName()));
    Arguments->SetStringField(TEXT("channel_name"), Metadata[0].Name.ToString());
    Arguments->SetNumberField(TEXT("value"), 3.5);
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>(); FString Error;
    TSharedPtr<FJsonObject> Prepared;
    TestTrue(TEXT("Independent argument contract accepts typed channel parameters"), FNexusAnimationAdvancedTools::PrepareArguments(TEXT("NexusAnimation.Keyframing"), TEXT("set_default_value"), Arguments, Prepared, Error));
    Arguments->SetBoolField(TEXT("UnknownParameter"), true);
    TestFalse(TEXT("Unknown animation argument is rejected"), FNexusAnimationAdvancedTools::PrepareArguments(TEXT("NexusAnimation.Keyframing"), TEXT("set_default_value"), Arguments, Prepared, Error));
    Arguments->RemoveField(TEXT("UnknownParameter"));
    TestTrue(TEXT("Set default through independent channel API"), FNexusEditorDomainTools::Animation(TEXT("NexusAnimation.Keyframing"), TEXT("set_default_value"), Arguments, Result, Error));
    TestTrue(TEXT("Read default through independent channel API"), FNexusEditorDomainTools::Animation(TEXT("NexusAnimation.Keyframing"), TEXT("get_default_value"), Arguments, Result, Error));
    TestEqual(TEXT("Default survives round trip"), Result->GetNumberField(TEXT("Value")), 3.5);
    Arguments->SetNumberField(TEXT("start_frame"), 0); Arguments->SetNumberField(TEXT("end_frame"), 3);
    TestTrue(TEXT("Sample default without an open Sequencer"), FNexusEditorDomainTools::Animation(TEXT("NexusAnimation.Keyframing"), TEXT("bake_channel_keys"), Arguments, Result, Error));
    const TArray<TSharedPtr<FJsonValue>>* Samples = nullptr;
    if (TestTrue(TEXT("Samples returned"), Result->TryGetArrayField(TEXT("Items"), Samples))) {
        TestEqual(TEXT("One sample per display frame"), Samples->Num(), 3);
        for (const auto& Sample : *Samples) TestEqual(TEXT("Sample value"), Sample->AsNumber(), 3.5);
    }
    Arguments->SetNumberField(TEXT("end_frame"), 5000);
    TestFalse(TEXT("Unbounded channel sampling rejected"), FNexusEditorDomainTools::Animation(TEXT("NexusAnimation.Keyframing"), TEXT("bake_channel_keys"), Arguments, Result, Error));
    return true;
}
#endif
