// Copyright byteyang. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "NexusCapabilityRegistry.h"
#include "NexusSchemaTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusCapabilityGapNewCapsRegistered,
	"NexusLink.Smoke.PluginAndRegistry.GapCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCapabilityGapNewCapsRegistered::RunTest(const FString& Parameters)
{
	const TArray<FCapRecord>& Records = FNexusCapabilityRegistry::Get().GetAllRecords();
	TestTrue(TEXT("registry not empty"), Records.Num() > 0);

	auto ExpectPresent = [this](const TCHAR* Name, bool bExpect)
	{
		const FCapRecord* Rec = FNexusCapabilityRegistry::Get().FindRecordByName(Name);
		if (bExpect)
		{
			TestNotNull(Name, Rec);
		}
		else
		{
			TestNull(FString::Printf(TEXT("%s absent"), Name), Rec);
		}
	};

#if WITH_NIAGARA
	ExpectPresent(TEXT("create_asset_niagara_system"), true);
	ExpectPresent(TEXT("interact_runtime_actor_niagara"), true);
	TestTrue(TEXT("Niagara set_emitter_enabled"), NexusSchemaActionContains(TEXT("manage_asset_niagara_system"), TEXT("set_emitter_enabled"), *this));
#else
	ExpectPresent(TEXT("create_asset_niagara_system"), false);
#endif
#if WITH_STATETREE
	ExpectPresent(TEXT("create_asset_state_tree"), true);
	TestTrue(TEXT("ST add_task"), NexusSchemaActionContains(TEXT("manage_asset_state_tree"), TEXT("add_task"), *this));
#endif
#if WITH_UNLUA
	ExpectPresent(TEXT("manage_asset_lua_binding"), true);
#else
	ExpectPresent(TEXT("manage_asset_lua_binding"), false);
#endif
#if WITH_GAS
	ExpectPresent(TEXT("create_asset_gameplay_cue_notify"), true);
	TestTrue(TEXT("GCNotify set_cue_name"), NexusSchemaActionContains(TEXT("manage_asset_gameplay_cue_notify"), TEXT("set_cue_name"), *this));
#else
	ExpectPresent(TEXT("create_asset_gameplay_cue_notify"), false);
#endif
#if WITH_PAPER2D
	ExpectPresent(TEXT("create_asset_paper_sprite"), true);
#else
	ExpectPresent(TEXT("create_asset_paper_sprite"), false);
#endif
#if WITH_COMMON_UI
	ExpectPresent(TEXT("create_asset_common_button_style"), true);
#else
	ExpectPresent(TEXT("create_asset_common_button_style"), false);
#endif
#if WITH_MOVIE_RENDER_PIPELINE
	ExpectPresent(TEXT("create_asset_movie_pipeline_config"), true);
#else
	ExpectPresent(TEXT("create_asset_movie_pipeline_config"), false);
#endif

	TestTrue(TEXT("BP add_macro"), NexusSchemaActionContains(TEXT("manage_asset_blueprint"), TEXT("add_macro"), *this));
	TestTrue(TEXT("WBP add_animation"), NexusSchemaActionContains(TEXT("manage_asset_user_widget"), TEXT("add_animation"), *this));
	return true;
}

#endif
