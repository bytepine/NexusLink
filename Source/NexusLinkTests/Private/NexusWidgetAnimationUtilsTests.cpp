// Copyright byteyang. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "NexusCapabilityRegistry.h"
#include "NexusSchemaTestUtils.h"
#include "Utils/NexusWidgetAnimationUtils.h"
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
	TestTrue(TEXT("含 add_animation"), NexusSchemaActionContains(TEXT("manage_asset_user_widget"), TEXT("add_animation"), *this));
	TestTrue(TEXT("含 add_track"), NexusSchemaActionContains(TEXT("manage_asset_user_widget"), TEXT("add_track"), *this));
	TestTrue(TEXT("含 add_key"), NexusSchemaActionContains(TEXT("manage_asset_user_widget"), TEXT("add_key"), *this));
	return true;
}

#endif
