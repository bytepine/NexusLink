// Copyright byteyang. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "NexusCapabilityRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#if WITH_DEV_AUTOMATION_TESTS

static bool SchemaActionContains(const FString& CapName, const FString& Action, FAutomationTestBase& Test)
{
	const FCapRecord* Rec = FNexusCapabilityRegistry::Get().FindRecordByName(CapName);
	if (!Rec)
	{
		Test.AddError(FString::Printf(TEXT("未注册: %s"), *CapName));
		return false;
	}
	if (!Rec->Def.InputSchema.IsValid()) return false;
	const TSharedPtr<FJsonObject>* Props = nullptr;
	if (!Rec->Def.InputSchema->TryGetObjectField(TEXT("properties"), Props) || !Props) return false;
	const TSharedPtr<FJsonObject>* Ops = nullptr;
	if (!(*Props)->TryGetObjectField(TEXT("operations"), Ops) || !Ops) return false;
	const TSharedPtr<FJsonObject>* Items = nullptr;
	if (!(*Ops)->TryGetObjectField(TEXT("items"), Items) || !Items) return false;
	const TSharedPtr<FJsonObject>* ItemProps = nullptr;
	if (!(*Items)->TryGetObjectField(TEXT("properties"), ItemProps) || !ItemProps) return false;
	const TSharedPtr<FJsonObject>* ActionObj = nullptr;
	if (!(*ItemProps)->TryGetObjectField(TEXT("action"), ActionObj) || !ActionObj) return false;
	const TArray<TSharedPtr<FJsonValue>>* Enums = nullptr;
	if (!(*ActionObj)->TryGetArrayField(TEXT("enum"), Enums) || !Enums) return false;
	for (const TSharedPtr<FJsonValue>& V : *Enums)
	{
		if (V->AsString() == Action) return true;
	}
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusCapabilityGapNewCapsRegistered,
	"NexusLink.Smoke.PluginAndRegistry.GapCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCapabilityGapNewCapsRegistered::RunTest(const FString& Parameters)
{
	TestNotNull(TEXT("create_asset_level_sequence"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_level_sequence")));
	TestNotNull(TEXT("create_asset_physical_material"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_physical_material")));
	TestNotNull(TEXT("create_asset_sound_cue"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_sound_cue")));
	TestNotNull(TEXT("create_asset_level"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_level")));
	TestTrue(TEXT("LS add_possessable"), SchemaActionContains(TEXT("manage_asset_level_sequence"), TEXT("add_possessable"), *this));
	TestTrue(TEXT("BP add_macro"), SchemaActionContains(TEXT("manage_asset_blueprint"), TEXT("add_macro"), *this));
	TestTrue(TEXT("WBP add_animation"), SchemaActionContains(TEXT("manage_asset_user_widget"), TEXT("add_animation"), *this));
	TestTrue(TEXT("ABP add_node"), SchemaActionContains(TEXT("manage_asset_anim_blueprint"), TEXT("add_node"), *this));
	TestTrue(TEXT("Mat SAT MaterialFunction"), [&]()
	{
		const FCapRecord* Rec = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("get_asset_material"));
		if (!Rec) return false;
		return Rec->Def.SearchAssetTypes.Contains(TEXT("MaterialFunction"));
	}());
	TestNotNull(TEXT("interact_runtime_actor_audio"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("interact_runtime_actor_audio")));
	TestNotNull(TEXT("interact_runtime_actor_ai"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("interact_runtime_actor_ai")));
#if WITH_NIAGARA
	TestTrue(TEXT("Niagara set_emitter_enabled"), SchemaActionContains(TEXT("manage_asset_niagara_system"), TEXT("set_emitter_enabled"), *this));
	TestNotNull(TEXT("create_asset_niagara_system"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_niagara_system")));
	TestNotNull(TEXT("interact_runtime_actor_niagara"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("interact_runtime_actor_niagara")));
#endif
#if WITH_STATETREE
	TestTrue(TEXT("ST add_task"), SchemaActionContains(TEXT("manage_asset_state_tree"), TEXT("add_task"), *this));
	TestNotNull(TEXT("create_asset_state_tree"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_state_tree")));
#endif
#if WITH_MVVM
	TestTrue(TEXT("MVVM add_view_model"), SchemaActionContains(TEXT("manage_asset_view_model"), TEXT("add_view_model"), *this));
#endif
#if WITH_UNLUA
	TestNotNull(TEXT("manage_asset_lua_binding"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("manage_asset_lua_binding")));
#endif
#if WITH_IK_RIG
	TestTrue(TEXT("IK add_chain"), SchemaActionContains(TEXT("manage_asset_ik_rig"), TEXT("add_chain"), *this));
	TestNotNull(TEXT("create_asset_ik_retargeter"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_ik_retargeter")));
#endif
#if WITH_PCG
	TestTrue(TEXT("PCG remove_edge"), SchemaActionContains(TEXT("manage_asset_pcg_graph"), TEXT("remove_edge"), *this));
#endif
#if WITH_CONTROL_RIG
	TestTrue(TEXT("CR add_control"), SchemaActionContains(TEXT("manage_asset_control_rig"), TEXT("add_control"), *this));
#endif
#if WITH_GAS
	TestNotNull(TEXT("create_asset_gameplay_cue_notify"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_gameplay_cue_notify")));
	TestTrue(TEXT("GCNotify set_cue_name"), SchemaActionContains(TEXT("manage_asset_gameplay_cue_notify"), TEXT("set_cue_name"), *this));
#else
	TestNull(TEXT("create_asset_gameplay_cue_notify absent"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_gameplay_cue_notify")));
#endif
#if WITH_PAPER2D
	TestNotNull(TEXT("create_asset_paper_sprite"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_paper_sprite")));
	TestTrue(TEXT("Sprite set_source"), SchemaActionContains(TEXT("manage_asset_paper_sprite"), TEXT("set_source"), *this));
	TestNotNull(TEXT("get_asset_paper_tile_map"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("get_asset_paper_tile_map")));
#else
	TestNull(TEXT("create_asset_paper_sprite absent"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_paper_sprite")));
#endif
#if WITH_GEOMETRY_COLLECTION
	TestNotNull(TEXT("create_asset_geometry_collection"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_geometry_collection")));
	TestTrue(TEXT("GC set_damage_threshold"), SchemaActionContains(TEXT("manage_asset_geometry_collection"), TEXT("set_damage_threshold"), *this));
#else
	TestNull(TEXT("create_asset_geometry_collection absent"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_geometry_collection")));
#endif
#if WITH_COMMON_UI
	TestNotNull(TEXT("create_asset_common_button_style"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_common_button_style")));
	TestNotNull(TEXT("create_asset_common_text_style"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_common_text_style")));
#else
	TestNull(TEXT("create_asset_common_button_style absent"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_common_button_style")));
#endif
#if WITH_MOVIE_RENDER_PIPELINE
	TestNotNull(TEXT("create_asset_movie_pipeline_config"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_movie_pipeline_config")));
	TestTrue(TEXT("MRQ set_output"), SchemaActionContains(TEXT("manage_asset_movie_pipeline_config"), TEXT("set_output"), *this));
#else
	TestNull(TEXT("create_asset_movie_pipeline_config absent"), FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("create_asset_movie_pipeline_config")));
#endif
	TestTrue(TEXT("ST add_key"), SchemaActionContains(TEXT("manage_asset_string_table"), TEXT("add_key"), *this));
	TestTrue(TEXT("Foliage set_mesh"), SchemaActionContains(TEXT("manage_asset_foliage_type"), TEXT("set_mesh"), *this));
	TestTrue(TEXT("Media set_file_path"), SchemaActionContains(TEXT("manage_asset_media_source"), TEXT("set_file_path"), *this));
	TestTrue(TEXT("Font SAT Font"), [&]()
	{
		const FCapRecord* Rec = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("get_asset_font"));
		return Rec && Rec->Def.SearchAssetTypes.Contains(TEXT("Font"));
	}());
	return true;
}

#endif
