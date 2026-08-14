// Copyright byteyang. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "NexusCapabilityRegistry.h"
#include "Utils/NexusWidgetAnimationUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusWidgetAnimationUtilsRoundtrip,
	"NexusLink.Utils.WidgetAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusWidgetAnimationUtilsRoundtrip::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>((UObject*)GetTransientPackage());
	TestNotNull(TEXT("transient WBP"), WBP);
	if (!WBP) return false;

	FString Err;
	UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::AddAnimation(WBP, TEXT("FadeIn"), Err);
	TestNotNull(TEXT("AddAnimation"), Anim);
	TestTrue(TEXT("AddAnimation 无错误"), Err.IsEmpty());
	TestEqual(TEXT("Animations 数量"), WBP->Animations.Num(), 1);
	TestEqual(TEXT("FindAnimation"), FNexusWidgetAnimationUtils::FindAnimation(WBP, TEXT("FadeIn")), Anim);

	FString TrackName;
	TestTrue(TEXT("AddFloatTrack"), FNexusWidgetAnimationUtils::AddFloatTrack(Anim, TEXT("Alpha"), TrackName, Err));
	TestEqual(TEXT("trackName"), TrackName, FString(TEXT("Alpha")));

	Err.Reset();
	TestTrue(TEXT("AddFloatKey"), FNexusWidgetAnimationUtils::AddFloatKey(Anim, 0.5f, 1.0f, Err));

	TArray<TSharedPtr<FJsonValue>> Tracks;
	FNexusWidgetAnimationUtils::AppendTrackSummaries(Anim, Tracks);
	TestTrue(TEXT("至少一条轨"), Tracks.Num() >= 1);

	Err.Reset();
	TestTrue(TEXT("RemoveAnimation"), FNexusWidgetAnimationUtils::RemoveAnimation(WBP, TEXT("FadeIn"), Err));
	TestEqual(TEXT("移除后为空"), WBP->Animations.Num(), 0);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusWidgetManageAnimationActionsInSchema,
	"NexusLink.Capability.UserWidget.AnimationActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusWidgetManageAnimationActionsInSchema::RunTest(const FString& Parameters)
{
	const FCapRecord* Rec = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("manage_asset_user_widget"));
	TestNotNull(TEXT("manage_asset_user_widget 已注册"), Rec);
	if (!Rec) return false;

	TestTrue(TEXT("Related 含 manage_asset_blueprint"), Rec->Def.RelatedCapabilities.Contains(TEXT("manage_asset_blueprint")));
	const TSharedPtr<FJsonObject>& Schema = Rec->Def.InputSchema;
	TestTrue(TEXT("有 InputSchema"), Schema.IsValid());
	if (!Schema.IsValid()) return false;

	const TSharedPtr<FJsonObject>* Props = nullptr;
	if (!Schema->TryGetObjectField(TEXT("properties"), Props) || !Props) return false;
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
	for (const TSharedPtr<FJsonValue>& V : *Enums)
	{
		Actions.Add(V->AsString());
	}
	TestTrue(TEXT("含 add_animation"), Actions.Contains(TEXT("add_animation")));
	TestTrue(TEXT("含 add_track"), Actions.Contains(TEXT("add_track")));
	TestTrue(TEXT("含 add_key"), Actions.Contains(TEXT("add_key")));
	return true;
}

#endif
