// Copyright byteyang. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "Utils/NexusVersionCompat.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Utils/NexusLegacyToolsets.h"
#include "UObject/UnrealType.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#if WITH_NIAGARA
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#endif
#if WITH_PCG
#include "PCGGraph.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLegacyTransformKeysTest, "NexusLink.Toolsets.Compatibility.TransformKeys", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
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
    const FString Domain = TEXT("animation_toolset.toolsets.keyframing.SequencerKeyframingTools");
    TSharedPtr<FJsonObject> Result;
    FString Error;
    const auto Call = [&](const TCHAR* Tool)
    {
        Result = MakeShared<FJsonObject>(); Error.Empty();
        const bool bSucceeded = FNexusLegacyToolsets::Animation(Domain, Tool, Arguments, Result, Error);
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

#if WITH_NIAGARA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLegacyNiagaraValueTypesTest, "NexusLink.Toolsets.Compatibility.NiagaraValueTypes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusLegacyNiagaraValueTypesTest::RunTest(const FString&)
{
    UNiagaraSystem* System = NewObject<UNiagaraSystem>();
    const FNiagaraTypeDefinition Types[] = {FNiagaraTypeDefinition::GetVec2Def(), FNiagaraTypeDefinition::GetVec3Def(), FNiagaraTypeDefinition::GetVec4Def(), FNiagaraTypeDefinition::GetQuatDef()};
    int32 TypeIndex = 0;
    for (const auto& Type : Types)
    {
        FNiagaraVariable Variable(Type, FName(*FString::Printf(TEXT("User.Compatibility%d"), TypeIndex++)));
        Variable.AllocateData();
        System->GetExposedParameters().AddParameter(Variable);
        System->GetExposedParameters().SetParameterData(Variable.GetData(), Variable);
    }
    auto Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("system"), System->GetPathName());
    TSharedPtr<FJsonObject> Result;
    FString Error;
    const auto Call = [&](const TCHAR* Tool)
    {
        Result = MakeShared<FJsonObject>(); Error.Empty();
        const bool bSucceeded = FNexusLegacyToolsets::Niagara(Tool, Arguments, Result, Error);
        TestTrue(FString(Tool) + TEXT(": ") + Error, bSucceeded);
        return bSucceeded;
    };
    if (!Call(TEXT("GetUserVariables"))) return false;
    const auto Variables = Result->GetArrayField(TEXT("userVariables"));
    if (!TestEqual(TEXT("All host vector and quaternion types discovered"), Variables.Num(), 4)) return false;
    // 使用查询原样返回的类型和默认值，验证真实值结构能完成读、删、加闭环。
    Arguments->SetArrayField(TEXT("variablesToRemove"), Variables);
    if (!Call(TEXT("RemoveUserVariables"))) return false;
    if (!Call(TEXT("GetUserVariables"))) return false;
    TestEqual(TEXT("All discovered types removed"), Result->GetArrayField(TEXT("userVariables")).Num(), 0);
    Arguments->SetArrayField(TEXT("variablesToAdd"), Variables);
    if (!Call(TEXT("AddUserVariables"))) return false;
    TArray<FNiagaraVariable> Restored;
    System->GetExposedParameters().GetParameters(Restored);
    TestEqual(TEXT("All value variables restored"), Restored.Num(), 4);
    for (int32 VariableIndex = 0; VariableIndex < UE_ARRAY_COUNT(Types); ++VariableIndex)
    {
        const FNiagaraVariable Expected(Types[VariableIndex], FName(*FString::Printf(TEXT("User.Compatibility%d"), VariableIndex)));
        TestTrue(TEXT("Restored variable retains the host type"), System->GetExposedParameters().IndexOf(Expected) != INDEX_NONE);
    }
    return true;
}
#endif

#if WITH_PCG
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLegacyPCGParameterPropagationTest, "NexusLink.Toolsets.Compatibility.PCGParameterPropagation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusLegacyPCGParameterPropagationTest::RunTest(const FString&)
{
    UPCGGraph* Graph = NewObject<UPCGGraph>();
    UPCGGraphInstance* Inherited = NewObject<UPCGGraphInstance>();
    UPCGGraphInstance* Overridden = NewObject<UPCGGraphInstance>();
    Inherited->SetGraph(Graph);
    Overridden->SetGraph(Graph);
    auto Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("graph"), Graph->GetPathName());
    const auto SetParameter = [&](const TCHAR* Name, const TCHAR* Type, const TCHAR* DefaultValue)
    {
        auto Definition = MakeShared<FJsonObject>();
        Definition->SetStringField(TEXT("name"), Name); Definition->SetStringField(TEXT("type"), Type);
        Definition->SetStringField(TEXT("defaultValueJson"), DefaultValue);
        Arguments->SetArrayField(TEXT("params"), {MakeShared<FJsonValueObject>(Definition)});
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>(); FString Error;
        const bool bSucceeded = FNexusLegacyToolsets::PCG(TEXT("SetGraphParams"), Arguments, Result, Error);
        TestTrue(TEXT("Set graph parameter: ") + Error, bSucceeded);
        return bSucceeded;
    };
    const auto CheckDensity = [&](UPCGGraphInstance* Instance, double Expected)
    {
        const FInstancedPropertyBag* Bag = Instance->GetUserParametersStruct();
        const auto* Definition = Bag ? Bag->FindPropertyDescByName(TEXT("Density")) : nullptr;
        if (!TestNotNull(TEXT("Existing instance contains Density"), Definition)) return false;
        const auto* Property = CastField<FNumericProperty>(Definition->CachedProperty);
        if (!TestNotNull(TEXT("Density is numeric"), Property)) return false;
        return TestEqual(TEXT("Instance has the expected effective value"), Property->GetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Bag->GetValue().GetMemory())), Expected);
    };
    if (!SetParameter(TEXT("Density"), TEXT("Float"), TEXT("1"))) return false;
    if (!CheckDensity(Inherited, 1.0) || !CheckDensity(Overridden, 1.0)) return false;
    TestTrue(TEXT("Instance override succeeds"), Overridden->SetGraphParameter<float>(TEXT("Density"), 7.0f) == EPropertyBagResult::Success);
    if (!SetParameter(TEXT("Density"), TEXT("Float"), TEXT("3"))) return false;
    if (!CheckDensity(Inherited, 3.0) || !CheckDensity(Overridden, 7.0)) return false;
    if (!SetParameter(TEXT("Count"), TEXT("Integer32"), TEXT("5"))) return false;
    for (UPCGGraphInstance* Instance : {Inherited, Overridden})
        TestNotNull(TEXT("Added parameter propagates to an existing instance"), Instance->GetUserParametersStruct()->FindPropertyDescByName(TEXT("Count")));
    if (!CheckDensity(Overridden, 7.0)) return false;
    Arguments->SetArrayField(TEXT("paramNames"), {MakeShared<FJsonValueString>(TEXT("Density")), MakeShared<FJsonValueString>(TEXT("Count"))});
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>(); FString Error;
    const bool bRemoved = FNexusLegacyToolsets::PCG(TEXT("RemoveGraphParams"), Arguments, Result, Error);
    if (!TestTrue(TEXT("Remove graph parameters: ") + Error, bRemoved)) return false;
    for (UPCGGraphInstance* Instance : {Inherited, Overridden})
        TestEqual(TEXT("Removed parameters disappear from existing instances"), Instance->GetUserParametersStruct()->GetNumPropertiesInBag(), 0);
    return true;
}
#endif
#endif
