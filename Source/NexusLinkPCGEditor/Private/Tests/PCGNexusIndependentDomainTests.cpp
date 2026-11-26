// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusEditorActionSchema.h"
#include "Utils/NexusVersionCompat.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Utils/NexusEditorToolSchema.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_PCG
#include "PCGGraph.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusIndependentPCGParameterTest, "NexusLink.Independent.PCG.Parameters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusIndependentPCGParameterTest::RunTest(const FString&)
{
    UPCGGraph* Graph = NewObject<UPCGGraph>(GetTransientPackage());
    auto Args = MakeShared<FJsonObject>(); Args->SetObjectField(TEXT("graph"), FNexusEditorDomainTools::Reference(Graph->GetPathName()));
    auto Parameter = MakeShared<FJsonObject>(); Parameter->SetStringField(TEXT("name"), TEXT("Offsets")); Parameter->SetStringField(TEXT("type"), TEXT("Vector"));
    Parameter->SetStringField(TEXT("containerType"), TEXT("Array")); Parameter->SetStringField(TEXT("defaultValueJson"), TEXT("[{\"X\":1,\"Y\":2,\"Z\":3}]"));
    Args->SetArrayField(TEXT("params"), {MakeShared<FJsonValueObject>(Parameter)});
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>(); FString Error;
    if (!TestTrue(TEXT("Vector array parameter is created without a registered tool provider"), FNexusEditorDomainTools::PCG(TEXT("SetGraphParams"), Args, Result, Error))) { AddError(Error); return false; }
    Result = MakeShared<FJsonObject>();
    if (!TestTrue(TEXT("Independent schema reads back the bag"), FNexusEditorDomainTools::PCG(TEXT("GetGraphSchema"), Args, Result, Error))) { AddError(Error); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    TestTrue(TEXT("Vector array default survives the round trip"), Result->TryGetArrayField(TEXT("offsets"), Values) || Result->TryGetArrayField(TEXT("Offsets"), Values));
    if (Values) TestEqual(TEXT("One vector is stored"), Values->Num(), 1);
    auto Source = MakeShared<FJsonObject>(); Source->SetObjectField(TEXT("graph"), FNexusEditorDomainTools::Reference(Graph->GetPathName()));
    Result = MakeShared<FJsonObject>();
    TestTrue(TEXT("Structure inspection does not need an official provider"), FNexusEditorDomainTools::PCG(TEXT("GetGraphStructure"), Source, Result, Error));
    TestTrue(TEXT("Structure contains node records"), Result->HasField(TEXT("Nodes")));
    return true;
}
#endif
