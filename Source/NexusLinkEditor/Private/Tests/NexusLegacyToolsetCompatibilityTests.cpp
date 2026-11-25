// Copyright byteyang. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "Utils/NexusVersionCompat.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Utils/NexusEditorDomainTools.h"
#include "UObject/UnrealType.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLegacyTransformKeysTest, "NexusLink.Independent.TransformKeys", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusLegacyTransformKeysTest::RunTest(const FString&)
{
    ULevelSequence* Sequence = NewObject<ULevelSequence>();
    Sequence->Initialize();
    UMovieScene* Scene = Sequence->GetMovieScene();
    Scene->SetDisplayRate(FFrameRate(30, 1));
    Scene->SetTickResolutionDirectly(FFrameRate(24000, 1));
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
    UMovieScene3DTransformTrack* Track = Scene->AddMasterTrack<UMovieScene3DTransformTrack>();
#else
    UMovieScene3DTransformTrack* Track = Scene->AddTrack<UMovieScene3DTransformTrack>();
#endif
    if (!TestNotNull(TEXT("Transform track created"), Track)) return false;
    UMovieSceneSection* Section = Track->CreateNewSection();
    if (!TestNotNull(TEXT("Transform section created"), Section)) return false;
    Track->AddSection(*Section);

    auto Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("section"), Section->GetPathName());
    Arguments->SetStringField(TEXT("channel_name"), TEXT("Location.X"));
    const FString Domain = TEXT("NexusAnimation.Keyframing");
    TSharedPtr<FJsonObject> Result;
    FString Error;
    const auto Call = [&](const TCHAR* Tool)
    {
        Result = MakeShared<FJsonObject>(); Error.Empty();
        const bool bSucceeded = FNexusEditorDomainTools::Animation(Domain, Tool, Arguments, Result, Error);
        TestTrue(FString(Tool) + TEXT(": ") + Error, bSucceeded);
        return bSucceeded;
    };
    if (!Call(TEXT("get_channel_names"))) return false;
    bool bHasLocation = false;
    for (const auto& Name : Result->GetArrayField(TEXT("Items"))) bHasLocation |= Name->AsString() == TEXT("Location.X");
    TestTrue(TEXT("Real transform channel discovered"), bHasLocation);

    Arguments->SetNumberField(TEXT("frame"), 12);
#if NX_UE_HAS_MOVIE_SCENE_DOUBLE_CHANNEL
    const double ExpectedValue = 16777217.25;
#else
    const double ExpectedValue = 17.25;
#endif
    Arguments->SetNumberField(TEXT("value"), ExpectedValue);
    Arguments->SetStringField(TEXT("interpolation"), TEXT("Linear"));
    if (!Call(TEXT("add_key_float"))) return false;
    // 同帧更新必须复用原键；Double 值不得经过 float 精度截断。
    if (!Call(TEXT("add_key_float"))) return false;
    Arguments->RemoveField(TEXT("frame"));
    if (!Call(TEXT("get_keys"))) return false;
    const auto& Keys = Result->GetArrayField(TEXT("Items"));
    if (!TestEqual(TEXT("Same display frame updates one key"), Keys.Num(), 1)) return false;
    TestEqual(TEXT("Display frame survives tick conversion"), Keys[0]->AsObject()->GetNumberField(TEXT("frame")), 12.0);
    TestEqual(TEXT("Numeric value is preserved"), Keys[0]->AsObject()->GetNumberField(TEXT("value")), ExpectedValue);
    Arguments->SetNumberField(TEXT("frame"), 12);
    if (!Call(TEXT("remove_key_at_frame"))) return false;
    TestEqual(TEXT("One key removed"), Result->GetNumberField(TEXT("RemovedCount")), 1.0);
    Arguments->RemoveField(TEXT("frame"));
    if (!Call(TEXT("get_keys"))) return false;
    TestEqual(TEXT("Channel is empty after removal"), Result->GetArrayField(TEXT("Items")).Num(), 0);
    return true;
}


#endif
