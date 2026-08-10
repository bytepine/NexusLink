# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""audit_capability_params.py — 合成文本规则测试（不依赖真实源码变绿）。"""
from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from audit_capability_params import (  # noqa: E402
    audit_cap,
    audit_file,
    audit_tree,
    scan_capability_text,
)

FIXTURE_GOOD = r'''
void FGoodCap::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_demo");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"), { TEXT("set") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("写路径")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
}
FCapabilityResult FGoodCap::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString AssetPath;
	RequireString(Arguments, TEXT("assetPath"), AssetPath, R.Entries);
	return {};
}
'''

FIXTURE_BAD_MULTI = r'''
void FBad::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_demo");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("批量路径")))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("单数路径")))
		.Build();
}
'''

FIXTURE_BAD_MANAGE_OPS = r'''
void FBadManage::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_demo");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("路径")))
		.Prop(TEXT("keys"), FNexusSchema::ArrOfObj(TEXT("旧容器")))
		.Prop(TEXT("action"), FNexusSchema::Str(TEXT("顶层 action")))
		.Build();
}
'''

FIXTURE_BAD_RENAME_LUA = r'''
void FBadRename::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("rename_asset");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("源")))
		.Prop(TEXT("newPath"), FNexusSchema::Str(TEXT("旧目标")))
		.Build();
}
void FBadLua::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("dofile_runtime_lua");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("filePath"), FNexusSchema::Str(TEXT("旧脚本路径")))
		.Build();
}
'''

FIXTURE_BAD_SET = r'''
void FBadSet::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("set_runtime_actor_property");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("actorName"), FNexusSchema::Str(TEXT("Actor")))
		.Build();
}
'''

FIXTURE_BAD_PKG = r'''
void FBadCreate::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_demo");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("packagePath"), FNexusSchema::Str(TEXT("包")))
		.Prop(TEXT("assetName"), FNexusSchema::Str(TEXT("名")))
		.Build();
}
'''

FIXTURE_SPAWN_OLD = r'''
void FBadSpawn::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("spawn_runtime_actor");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("blueprintPath"), FNexusSchema::Str(TEXT("旧蓝图")))
		.Prop(TEXT("classPath"), FNexusSchema::Str(TEXT("旧类路径")))
		.Build();
}
'''


def _rules(text: str, *, lua_path: Path | None = None) -> set[str]:
    cap = scan_capability_text(text, lua_path)
    assert cap is not None
    return {v.rule for v in audit_cap(cap)}


def test_good_manage_passes():
    assert _rules(FIXTURE_GOOD) == set()


def test_nested_propertyPath_in_ops_ok():
    """manage operations item 内的 propertyPath 不应触发 get_singular 规则。"""
    assert "get_singular_propertyPath" not in _rules(FIXTURE_GOOD)
    assert "forbidden_param" not in _rules(FIXTURE_GOOD)


def test_forbid_assetPaths_and_get_propertyPath():
    rules = _rules(FIXTURE_BAD_MULTI)
    assert "forbidden_param" in rules
    assert "get_singular_propertyPath" in rules


def test_manage_requires_operations_and_bans_top_containers():
    rules = _rules(FIXTURE_BAD_MANAGE_OPS)
    assert "manage_operations" in rules
    assert "manage_top_container" in rules
    assert "manage_top_action" in rules


def test_forbid_newPath_and_filePath():
    # 文件含两个 Out.Name —— scan 只取第一个
    rules = _rules(FIXTURE_BAD_RENAME_LUA)
    assert "forbidden_param" in rules


def test_lua_path_with_lua_dir(tmp_path: Path):
    cpp = tmp_path / "Lua" / "Runtime" / "NexusGetRuntimeLuaEnvCapability.cpp"
    cpp.parent.mkdir(parents=True)
    cpp.write_text(
        '''
void FCap::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_lua_env");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("path"), FNexusSchema::Str(TEXT("旧")))
		.Build();
}
''',
        encoding="utf-8",
    )
    rules = {v.rule for v in audit_file(cpp)}
    assert "lua_path" in rules


def test_set_property_requires_updates():
    assert "set_updates" in _rules(FIXTURE_BAD_SET)


def test_packagePath_assetName():
    assert "packagePath_assetName" in _rules(FIXTURE_BAD_PKG)


def test_spawn_old_keys():
    rules = _rules(FIXTURE_SPAWN_OLD)
    assert "forbidden_param" in rules


def test_audit_tree_fixture_dir(tmp_path: Path):
    good = tmp_path / "Good.cpp"
    bad = tmp_path / "Bad.cpp"
    good.write_text(FIXTURE_GOOD, encoding="utf-8")
    bad.write_text(FIXTURE_BAD_MULTI, encoding="utf-8")
    errs = audit_tree(tmp_path)
    assert any(e.cap == "get_asset_demo" for e in errs)
    assert not any(e.cap == "manage_asset_demo" for e in errs)


def test_ownerWidget_execute_read(tmp_path: Path):
    cpp = tmp_path / "NexusInteractRuntimeWidgetCapability.cpp"
    cpp.write_text(
        '''
void FCap::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("interact_runtime_widget");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("widgetName"), FNexusSchema::Str(TEXT("控件")))
		.Build();
}
FCapabilityResult FCap::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString Owner;
	Arguments->TryGetStringField(TEXT("ownerWidget"), Owner);
	return {};
}
''',
        encoding="utf-8",
    )
    rules = {v.rule for v in audit_file(cpp)}
    assert "forbidden_param" in rules


def test_lambda_inputschema_uses_return_object(tmp_path: Path):
    """InputSchema = [](){ ItemSchema=Object()…; return Object().Prop(operations)…}() 应认顶层 operations。"""
    cpp = tmp_path / "NexusManageAssetBlackboardCapability.cpp"
    cpp.write_text(
        r'''
void FCap::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_blackboard");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
			.Prop(TEXT("action"), FNexusSchema::Str(TEXT("操作")))
			.Prop(TEXT("keyName"), FNexusSchema::Str(TEXT("键")))
			.Required({ TEXT("action") })
			.Build();
		return FNexusSchema::Object()
			.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("路径")))
			.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量"), ItemSchema.ToSharedRef()))
			.Required({ TEXT("assetPath"), TEXT("operations") })
			.Build();
	}();
}
''',
        encoding="utf-8",
    )
    rules = {v.rule for v in audit_file(cpp)}
    assert rules == set()
