// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusCapabilityLegacyNames.h"

static const TMap<FString, FString>& GetLegacyCapabilityNameMap()
{
	static const TMap<FString, FString> Map = {
			{ TEXT("create_blueprint"),           TEXT("create_asset_blueprint") },
			{ TEXT("create_material"),           TEXT("create_asset_material") },
			{ TEXT("create_widget"),             TEXT("create_asset_user_widget") },
			{ TEXT("create_struct"),             TEXT("create_asset_struct") },
			{ TEXT("create_data_asset"),         TEXT("create_asset_data_asset") },
			{ TEXT("create_data_table"),         TEXT("create_asset_data_table") },
			{ TEXT("create_behavior_tree"),      TEXT("create_asset_behavior_tree") },
			{ TEXT("create_blackboard"),         TEXT("create_asset_blackboard") },
			{ TEXT("create_anim_blueprint"),     TEXT("create_asset_anim_blueprint") },
			{ TEXT("create_anim_montage"),       TEXT("create_asset_anim_montage") },
			{ TEXT("get_behavior_tree"),         TEXT("get_asset_behavior_tree") },
			{ TEXT("manage_struct_field"),       TEXT("manage_asset_struct_field") },
			{ TEXT("manage_blueprint_variable"),  TEXT("manage_asset_blueprint") },
			{ TEXT("manage_blueprint_graph"),      TEXT("manage_asset_blueprint") },
			{ TEXT("manage_blueprint_wires"),      TEXT("manage_asset_blueprint") },
			{ TEXT("manage_blueprint_component"),  TEXT("manage_asset_blueprint") },
			{ TEXT("manage_material"),           TEXT("manage_asset_material") },
			{ TEXT("manage_widget"),             TEXT("manage_asset_user_widget") },
			{ TEXT("list_actors"),               TEXT("list_runtime_actors") },
			{ TEXT("spawn_actor"),               TEXT("spawn_runtime_actor") },
			{ TEXT("destroy_actor"),             TEXT("destroy_runtime_actor") },
			{ TEXT("diff_actors"),               TEXT("diff_runtime_actors") },
			{ TEXT("get_actor_animation"),       TEXT("get_runtime_actor_animation") },
			{ TEXT("get_actor_behavior_tree"),   TEXT("get_runtime_actor_behavior_tree") },
			{ TEXT("spawn_widget"),              TEXT("spawn_runtime_widget") },
			{ TEXT("get_asset_slate_widget"),    TEXT("get_runtime_slate_widget") },
			{ TEXT("interact_widget"),           TEXT("interact_runtime_widget") },
			{ TEXT("get_property"),              TEXT("get_runtime_actor_property") },
			{ TEXT("set_property"),              TEXT("set_runtime_actor_property") },
	};
	return Map;
}

FString FNexusCapabilityLegacyNames::Resolve(const FString& Name)
{
	const FString Trimmed = Name.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return Trimmed;
	}
	if (const FString* Canon = GetLegacyCapabilityNameMap().Find(Trimmed))
	{
		return *Canon;
	}
	return Trimmed;
}

bool FNexusCapabilityLegacyNames::IsLegacyName(const FString& Name)
{
	return GetLegacyCapabilityNameMap().Contains(Name.TrimStartAndEnd());
}

FString FNexusCapabilityLegacyNames::GetCanonicalNameForLegacy(const FString& LegacyName)
{
	if (const FString* Canon = GetLegacyCapabilityNameMap().Find(LegacyName.TrimStartAndEnd()))
	{
		return *Canon;
	}
	return FString();
}
