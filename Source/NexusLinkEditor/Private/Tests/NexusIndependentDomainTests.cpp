// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusAnimationAdvancedTools.h"
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusEditorActionSchema.h"
#include "Utils/NexusVersionCompat.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Utils/NexusEditorToolSchema.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusSequencerPluginAvailabilityTest, "NexusLink.Independent.Animation.SequencerPluginAvailability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusSequencerPluginAvailabilityTest::RunTest(const FString&)
{
    const auto Check = [this](const TCHAR* PluginName, const TCHAR* Tool)
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(PluginName);
        const bool bEnabled = Plugin.IsValid() && Plugin->IsEnabled();
        const bool bWasLoaded = FModuleManager::Get().IsModuleLoaded(PluginName);
        const bool bAvailable = FNexusAnimationAdvancedTools::FindFunction(TEXT("NexusAnimation.Sequencer"), Tool) != nullptr;
        TestEqual(FString(Tool), bAvailable, bEnabled);
        if (!bEnabled)
            TestEqual(FString(PluginName), FModuleManager::Get().IsModuleLoaded(PluginName), bWasLoaded);
    };
    Check(TEXT("LevelSequenceEditor"), TEXT("get_current_sequence"));
    Check(TEXT("SequencerScripting"), TEXT("get_tracks_on_binding"));
    return true;
}

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
    TestTrue(TEXT("Capacity exhaustion returns a structured error"), FNexusEditorToolSchema::Result(Rejected).HasErrors());
    for (const auto& Operation : Operations) FNexusEditorOperations::Finish(Operation, TEXT("Cancelled"), nullptr);
    const auto Recovered = FNexusEditorOperations::Create(TEXT("CapacityTest"));
    TestTrue(TEXT("Releasing slots allows the next operation"), Recovered.IsValid());
    FNexusEditorOperations::Finish(Recovered, TEXT("Completed"), nullptr);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNamedRigControlAvailabilityTest, "NexusLink.Independent.Animation.NamedRigControlAvailability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusNamedRigControlAvailabilityTest::RunTest(const FString&)
{
    auto SavedValidation = MoveTemp(FNexusAnimationAdvancedTools::ValidateRigControl);
    ON_SCOPE_EXIT { FNexusAnimationAdvancedTools::ValidateRigControl = MoveTemp(SavedValidation); };
    FNexusAnimationAdvancedTools::ValidateRigControl = nullptr;

    const FString Domain = TEXT("NexusAnimation.ControlRigSequencer");
    const FString Tool = TEXT("set_transform");
    TestFalse(TEXT("SetRigTransform requires named control validation"), FNexusAnimationAdvancedTools::Supports(Domain, Tool));

    const auto Actions = FNexusEditorActionSchema::List(TEXT("ControlEditorAnimation"), TEXT("SetRigTransform"));
    if (TestEqual(TEXT("SetRigTransform is still discoverable"), Actions.Num(), 1))
    {
        const auto Row = Actions[0]->AsObject();
        TestFalse(TEXT("Discovery reports the unsupported action"), Row->GetBoolField(TEXT("bSupported")));
        TestTrue(TEXT("Discovery names the required integration"), Row->GetStringField(TEXT("Reason")).Contains(TEXT("ControlRig")));
    }

    auto Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("control_name"), TEXT("EulerControl"));
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    FString Error;
    TestFalse(TEXT("Calling SetRigTransform fails without validation"), FNexusAnimationAdvancedTools::Call(Domain, Tool, Args, Result, Error));
    TestTrue(TEXT("Execution explains the missing integration before resolving a target"), Error.Contains(TEXT("ControlRig")));

    const FString ListRigsTool = TEXT("get_control_rigs");
    TestEqual(TEXT("Listing rigs only requires the engine API"), FNexusAnimationAdvancedTools::Supports(Domain, ListRigsTool),
        FNexusAnimationAdvancedTools::FindFunction(Domain, ListRigsTool) != nullptr);
    return true;
}
#endif
