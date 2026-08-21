// Copyright byteyang. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "NexusCapabilityRegistry.h"
#include "NexusSchemaTestUtils.h"
#include "Utils/NexusAnimGraphUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusAnimGraphUtilsResolveClasses,
	"NexusLink.Utils.AnimGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAnimGraphUtilsResolveClasses::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	TestNotNull(TEXT("SequencePlayer"), FNexusAnimGraphUtils::ResolveAnimGraphNodeClass(TEXT("SequencePlayer")));
	TestNotNull(TEXT("BlendSpacePlayer"), FNexusAnimGraphUtils::ResolveAnimGraphNodeClass(TEXT("BlendSpacePlayer")));
	TestNotNull(TEXT("Slot"), FNexusAnimGraphUtils::ResolveAnimGraphNodeClass(TEXT("Slot")));
	TestNull(TEXT("未知类"), FNexusAnimGraphUtils::ResolveAnimGraphNodeClass(TEXT("K2Node_Event")));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusAnimBlueprintManageHasGraphActions,
	"NexusLink.Capability.AnimBlueprint.AnimGraphActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAnimBlueprintManageHasGraphActions::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("add_node"), NexusSchemaActionContains(TEXT("manage_asset_anim_blueprint"), TEXT("add_node"), *this));
	TestTrue(TEXT("connect"), NexusSchemaActionContains(TEXT("manage_asset_anim_blueprint"), TEXT("connect"), *this));
	return true;
}

#endif
