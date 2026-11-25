// Copyright byteyang. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusVersionCompat.h"
#if WITH_NIAGARA
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#endif
#if WITH_NIAGARA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLegacyNiagaraValueTypesTest, "NexusLink.Independent.NiagaraValueTypes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
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
        const bool bSucceeded = FNexusEditorDomainTools::Niagara(Tool, Arguments, Result, Error);
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
