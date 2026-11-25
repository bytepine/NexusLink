// Copyright byteyang. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusVersionCompat.h"
#if WITH_PCG
#include "PCGGraph.h"
#endif
#if WITH_PCG
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLegacyPCGParameterPropagationTest, "NexusLink.Independent.PCGParameterPropagation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
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
        const bool bSucceeded = FNexusEditorDomainTools::PCG(TEXT("SetGraphParams"), Arguments, Result, Error);
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
    const bool bRemoved = FNexusEditorDomainTools::PCG(TEXT("RemoveGraphParams"), Arguments, Result, Error);
    if (!TestTrue(TEXT("Remove graph parameters: ") + Error, bRemoved)) return false;
    for (UPCGGraphInstance* Instance : {Inherited, Overridden})
        TestEqual(TEXT("Removed parameters disappear from existing instances"), Instance->GetUserParametersStruct()->GetNumPropertiesInBag(), 0);
    return true;
}
#endif
