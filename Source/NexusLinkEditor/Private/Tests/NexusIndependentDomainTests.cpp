// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusNiagaraEditorTools.h"
#include "Utils/NexusEditorActionSchema.h"
#include "Utils/NexusVersionCompat.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Utils/NexusEditorToolSchema.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusIndependentOperationCapacityTest, "NexusLink.Independent.OperationCapacity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusIndependentOperationCapacityTest::RunTest(const FString&)
{
    TArray<TSharedPtr<FNexusEditorOperation>> Operations;
    ON_SCOPE_EXIT { for (const auto& Operation : Operations) FNexusEditorOperations::Finish(Operation, TEXT("Cancelled"), nullptr); };
    for (int32 Index = 0; Index < 128; ++Index)
    {
        auto Operation = FNexusEditorOperations::Create(TEXT("CapacityTest"));
        if (!Operation.IsValid()) break;
        Operations.Add(Operation);
    }
    const auto Rejected = FNexusEditorOperations::Create(TEXT("CapacityTest"));
    if (!TestFalse(TEXT("All running slots reject the next operation"), Rejected.IsValid())) return false;
    auto Args = MakeShared<FJsonObject>();
    // 直接覆盖三个共享入口；满队列不得继续分派或解引用空句柄。
    FNexusEditorDomainTools::Call(TEXT("NexusPCG"), TEXT("GetGraphSchema"), Args, Rejected);
    FNexusEditorDomainTools::Call(TEXT("NexusAnimation.Sequencer"), TEXT("get_display_rate"), Args, Rejected);
    FNexusNiagaraEditorTools::Call(TEXT("GetSystemSchema"), Args, Rejected);
    TestTrue(TEXT("Capacity exhaustion returns a structured error"), FNexusEditorToolSchema::Result(Rejected).HasErrors());
    for (const auto& Operation : Operations) FNexusEditorOperations::Finish(Operation, TEXT("Cancelled"), nullptr);
    const auto Recovered = FNexusEditorOperations::Create(TEXT("CapacityTest"));
    TestTrue(TEXT("Releasing slots allows the next operation"), Recovered.IsValid());
    FNexusEditorOperations::Finish(Recovered, TEXT("Completed"), nullptr);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_NIAGARA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusIndependentNiagaraDiscoveryTest, "NexusLink.Independent.Niagara.ActionDiscovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusIndependentNiagaraDiscoveryTest::RunTest(const FString&)
{
    const auto Check = [this](const TCHAR* Capability, const TCHAR* Action, bool bExpected, const TCHAR* MinimumVersion)
    {
        const auto Rows = FNexusEditorActionSchema::List(Capability, Action);
        if (!TestEqual(FString(Action) + TEXT(" is discoverable"), Rows.Num(), 1)) return;
        const auto Row = Rows[0]->AsObject();
        TestEqual(FString(Action) + TEXT(" support matches this engine"), Row->GetBoolField(TEXT("bSupported")), bExpected);
        TestEqual(FString(Action) + TEXT(" reports its first implementation version"), Row->GetStringField(TEXT("MinimumVersion")), FString(MinimumVersion));
        if (bExpected)
        {
            const auto Schema = Row->GetObjectField(TEXT("InputSchema"));
            TestTrue(FString(Action) + TEXT(" has a usable schema"), Schema->HasField(TEXT("properties")));
        }
    };
    Check(TEXT("GetEditorNiagara"), TEXT("SystemData"), true, TEXT("4.26"));
    Check(TEXT("GetEditorNiagara"), TEXT("RendererData"), true, TEXT("4.26"));
    Check(TEXT("GetEditorNiagara"), TEXT("CompileState"), NX_UE_HAS_NIAGARA_EXTERNAL_EDIT, TEXT("5.8"));
    Check(TEXT("GetEditorNiagara"), TEXT("StackIssues"), NX_UE_HAS_NIAGARA_EXTERNAL_EDIT, TEXT("5.8"));
    Check(TEXT("GetEditorNiagara"), TEXT("Dependencies"), NX_UE_HAS_NIAGARA_EXTERNAL_EDIT, TEXT("5.8"));
    Check(TEXT("ControlEditorNiagara"), TEXT("SetModuleEnabled"), NX_UE_HAS_NIAGARA_EXTERNAL_EDIT, TEXT("5.8"));
    return true;
}
#endif

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

#if WITH_DEV_AUTOMATION_TESTS && WITH_NIAGARA && NX_UE_HAS_NIAGARA_EXTERNAL_EDIT
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraSystemFactoryNew.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusIndependentNiagaraTest, "NexusLink.Independent.Niagara.SchemaAndVariables", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusIndependentNiagaraTest::RunTest(const FString&)
{
    auto Args = MakeShared<FJsonObject>(); TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>(); FString Error;
    if (!TestTrue(TEXT("System schema is independent of registry registration"), FNexusNiagaraEditorTools::Execute(TEXT("GetSystemSchema"), Args, Result, Error))) { AddError(Error); return false; }
    TestFalse(TEXT("Domain schema is nonempty"), Result->GetStringField(TEXT("propertySchema")).IsEmpty());
    UNiagaraSystem* System = NewObject<UNiagaraSystem>(GetTransientPackage());
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    Args->SetObjectField(TEXT("system"), FNexusEditorDomainTools::Reference(System->GetPathName()));
    auto Variable = MakeShared<FJsonObject>(), Type = MakeShared<FJsonObject>();
    Variable->SetStringField(TEXT("name"), TEXT("User.NexusIndependent")); Type->SetObjectField(TEXT("classStructOrEnum"), FNexusEditorDomainTools::Reference(FNiagaraTypeDefinition::GetFloatStruct()->GetPathName())); Variable->SetObjectField(TEXT("type"), Type);
    Args->SetArrayField(TEXT("variablesToAdd"), {MakeShared<FJsonValueObject>(Variable)});
    Result = MakeShared<FJsonObject>();
    if (!TestTrue(TEXT("Independent variable addition"), FNexusNiagaraEditorTools::Execute(TEXT("AddUserVariables"), Args, Result, Error))) { AddError(Error); return false; }
    Result = MakeShared<FJsonObject>();
    if (!TestTrue(TEXT("Independent variable readback"), FNexusNiagaraEditorTools::Execute(TEXT("GetUserVariables"), Args, Result, Error))) { AddError(Error); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
    if (!TestTrue(TEXT("Typed variables are returned"), Result->TryGetArrayField(TEXT("userVariables"), Variables))) return false;
    TestEqual(TEXT("One variable was added"), Variables->Num(), 1);
    if (Variables->Num())
    {
        auto Default = (*Variables)[0]->AsObject()->GetObjectField(TEXT("defaultValue"));
        TestTrue(TEXT("Instanced defaults carry an explicit type reference"), Default->HasField(TEXT("struct")));
        TestTrue(TEXT("Instanced defaults carry typed data"), Default->HasField(TEXT("value")));
    }
    Args->SetArrayField(TEXT("variablesToRemove"), {MakeShared<FJsonValueObject>(Variable)}); Result = MakeShared<FJsonObject>();
    TestTrue(TEXT("Independent variable removal"), FNexusNiagaraEditorTools::Execute(TEXT("RemoveUserVariables"), Args, Result, Error));
    TestTrue(TEXT("Deep writes advertise their own schema"), FNexusNiagaraEditorTools::InputSchema(TEXT("SetStackInputData")).IsValid());
    return true;
}
#endif
