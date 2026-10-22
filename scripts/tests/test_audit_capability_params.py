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
	Out.Name = TEXT("ManageAssetDemo");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("操作"), { TEXT("Set") }))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("写路径")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("资产路径")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("批量"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
}
FCapabilityResult FGoodCap::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString AssetPath;
	RequireString(Arguments, TEXT("AssetPath"), AssetPath, R.Entries);
	return {};
}
'''

FIXTURE_BAD_MULTI = r'''
void FBad::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetDemo");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("批量路径")))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("单数路径")))
		.Build();
}
'''

FIXTURE_BAD_MANAGE_OPS = r'''
void FBadManage::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetDemo");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("路径")))
		.Prop(TEXT("Keys"), FNexusSchema::ArrOfObj(TEXT("旧容器")))
		.Prop(TEXT("Action"), FNexusSchema::Str(TEXT("顶层 action")))
		.Build();
}
'''

FIXTURE_BAD_RENAME = r'''
void FBadRename::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("RenameAsset");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("源")))
		.Prop(TEXT("newPath"), FNexusSchema::Str(TEXT("旧目标")))
		.Build();
}
'''

FIXTURE_BAD_SET = r'''
void FBadSet::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("SetRuntimeActorProperty");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Actor")))
		.Build();
}
'''

FIXTURE_BAD_PKG = r'''
void FBadCreate::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetDemo");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("PackagePath"), FNexusSchema::Str(TEXT("包")))
		.Prop(TEXT("assetName"), FNexusSchema::Str(TEXT("名")))
		.Build();
}
'''

FIXTURE_SPAWN_OLD = r'''
void FBadSpawn::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("SpawnRuntimeActor");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("blueprintPath"), FNexusSchema::Str(TEXT("旧蓝图")))
		.Prop(TEXT("classPath"), FNexusSchema::Str(TEXT("旧类路径")))
		.Build();
}
'''


def _rules(text: str, *, path: Path | None = None) -> set[str]:
    cap = scan_capability_text(text, path)
    assert cap is not None
    return {v.rule for v in audit_cap(cap)}


def test_good_manage_passes():
    assert _rules(FIXTURE_GOOD) == set()


def test_nested_propertyPath_in_ops_ok():
    """manage operations item 内的 PropertyPath 不应触发 get_singular 规则。"""
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
    rules = _rules(FIXTURE_BAD_RENAME)
    assert "forbidden_param" in rules


def test_set_property_requires_updates():
    assert "set_updates" in _rules(FIXTURE_BAD_SET)


def test_catalog_does_not_override_current_source(monkeypatch):
    monkeypatch.setattr("audit_capability_params.load_capability_schemas", lambda: {
        "SetRuntimeActorProperty": {"properties": {"ActorName": {}, "Updates": {}}},
    })
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
    assert any(e.cap == "GetAssetDemo" for e in errs)
    assert not any(e.cap == "ManageAssetDemo" for e in errs)


def test_ownerWidget_execute_read(tmp_path: Path):
    cpp = tmp_path / "NexusInteractRuntimeWidgetCapability.cpp"
    cpp.write_text(
        '''
void FCap::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractRuntimeWidget");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("WidgetName"), FNexusSchema::Str(TEXT("控件")))
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
	Out.Name = TEXT("ManageAssetBlackboard");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
			.Prop(TEXT("Action"), FNexusSchema::Str(TEXT("操作")))
			.Prop(TEXT("KeyName"), FNexusSchema::Str(TEXT("键")))
			.Required({ TEXT("Action") })
			.Build();
		return FNexusSchema::Object()
			.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("路径")))
			.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("批量"), ItemSchema.ToSharedRef()))
			.Required({ TEXT("AssetPath"), TEXT("Operations") })
			.Build();
	}();
}
''',
        encoding="utf-8",
    )
    rules = {v.rule for v in audit_file(cpp)}
    assert rules == set()
