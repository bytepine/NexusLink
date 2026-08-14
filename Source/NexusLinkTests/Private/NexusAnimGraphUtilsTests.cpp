// Copyright byteyang. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "NexusCapabilityRegistry.h"
#include "Utils/NexusAnimGraphUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

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
	const FCapRecord* Rec = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("manage_asset_anim_blueprint"));
	TestNotNull(TEXT("已注册"), Rec);
	if (!Rec || !Rec->Def.InputSchema.IsValid()) return false;

	const TSharedPtr<FJsonObject>* Props = nullptr;
	if (!Rec->Def.InputSchema->TryGetObjectField(TEXT("properties"), Props) || !Props) return false;
	const TSharedPtr<FJsonObject>* Ops = nullptr;
	if (!(*Props)->TryGetObjectField(TEXT("operations"), Ops) || !Ops) return false;
	const TSharedPtr<FJsonObject>* Items = nullptr;
	if (!(*Ops)->TryGetObjectField(TEXT("items"), Items) || !Items) return false;
	const TSharedPtr<FJsonObject>* ItemProps = nullptr;
	if (!(*Items)->TryGetObjectField(TEXT("properties"), ItemProps) || !ItemProps) return false;
	const TSharedPtr<FJsonObject>* Action = nullptr;
	if (!(*ItemProps)->TryGetObjectField(TEXT("action"), Action) || !Action) return false;
	const TArray<TSharedPtr<FJsonValue>>* Enums = nullptr;
	if (!(*Action)->TryGetArrayField(TEXT("enum"), Enums) || !Enums) return false;
	TArray<FString> Actions;
	for (const TSharedPtr<FJsonValue>& V : *Enums) Actions.Add(V->AsString());
	TestTrue(TEXT("add_node"), Actions.Contains(TEXT("add_node")));
	TestTrue(TEXT("connect"), Actions.Contains(TEXT("connect")));
	return true;
}

#endif
