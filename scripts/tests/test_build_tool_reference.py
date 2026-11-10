"""build_tool_reference 中英双语生成契约。"""
from __future__ import annotations

import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tool_reference_i18n import heading_anchor, load_zh_overlay, translate_param_text  # noqa: E402

REPO_ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
DOCS = os.path.join(REPO_ROOT, "docs")
RE_HEAD = re.compile(r"^### `([^`]+)`", re.M)


def _read(rel: str) -> str:
    return open(os.path.join(REPO_ROOT, rel), encoding="utf-8").read()


def test_heading_anchor_matches_github_punctuation_and_spaces():
    assert heading_anchor("Data assets (DataAsset / DataTable)") == "data-assets-dataasset--datatable"
    assert heading_anchor("数据资产（DataAsset / DataTable）") == "数据资产dataasset--datatable"


def test_translate_param_live_patterns():
    assert translate_param_text("FooBar asset path") == "FooBar 资产路径"
    assert translate_param_text("Asset package path") == "资产包路径"
    assert "可选" in translate_param_text("Actor class filter (optional)")
    assert translate_param_text("已是中文说明") == "已是中文说明"


def test_zh_overlay_covers_all_generated_caps():
    en = _read("docs/tool-reference.md")
    overlay = load_zh_overlay()
    names = set(RE_HEAD.findall(en))
    missing = sorted(names - set(overlay.get("descriptions") or {}))
    assert not missing, f"tool_reference_zh.json missing descriptions: {missing[:20]}"


def test_en_and_zh_docs_have_same_cap_headings():
    en = set(RE_HEAD.findall(_read("docs/tool-reference.md")))
    zh = set(RE_HEAD.findall(_read("docs/tool-reference.zh.md")))
    assert en == zh
    assert "SearchCapabilities" in en
    assert len(en) >= 200


def test_zh_doc_uses_chinese_chrome():
    zh = _read("docs/tool-reference.zh.md")
    assert "## 目录" in zh
    assert "## 元工具" in zh
    assert "| 参数 | 类型 | 必填 | 说明 |" in zh
    assert "tool-reference.md" in zh
    en = _read("docs/tool-reference.md")
    assert "## Contents" in en
    assert "tool-reference.zh.md" in en


def test_zh_overlay_json_valid():
    path = os.path.join(REPO_ROOT, "scripts", "tool_reference_zh.json")
    data = json.loads(open(path, encoding="utf-8").read())
    assert isinstance(data["descriptions"], dict)
    assert isinstance(data["when_to_use"], dict)
    assert isinstance(data["param_text"], dict)
    assert len(data["descriptions"]) >= 200


def test_current_source_names_override_stale_catalog(tmp_path, monkeypatch):
    import build_tool_reference as reference

    source = tmp_path / "NexusCreateAssetNamingCapability.cpp"
    source.write_text('''
    Out.Name = TEXT("CreateAssetNaming");
    Out.Description = TEXT("Create a test asset.");
    Out.InputSchema = FNexusSchema::Object()
        .Prop(TEXT("Type"), FNexusSchema::Str(TEXT("Asset kind")))
        .Build();
    ''', encoding="utf-8")
    monkeypatch.setattr(reference, "load_capability_schemas", lambda: {
        "CreateAssetNaming": {"properties": {"type": {"type": "string"}}},
    })
    parsed = reference.parse_capability(source)
    assert [param["name"] for param in parsed["params"]] == ["Type"]


def test_dynamic_source_keeps_catalog_fallback(tmp_path, monkeypatch):
    import build_tool_reference as reference

    source = tmp_path / "NexusGetNamingCapability.cpp"
    source.write_text('''
    Out.Name = TEXT("GetNaming");
    Out.Description = TEXT("Read a test value.");
    Out.InputSchema = BuildDynamicSchema();
    ''', encoding="utf-8")
    monkeypatch.setattr(reference, "load_capability_schemas", lambda: {
        "GetNaming": {"properties": {"Name": {"type": "string"}}},
    })
    parsed = reference.parse_capability(source)
    assert [param["name"] for param in parsed["params"]] == ["Name"]


def test_pascal_manage_names_receive_framework_parameters_once(monkeypatch):
    import build_tool_reference as reference

    monkeypatch.setattr(reference, "load_manage_compile_caps", lambda: {"ManageAssetBlueprint"})
    cap = {"name": "ManageAssetBlueprint", "params": []}
    reference.inject_manage_finalize_params(cap)
    reference.inject_manage_finalize_params(cap)
    assert [param["name"] for param in cap["params"]] == ["bSaveToDisk", "bCompile"]
    create = {"name": "CreateAssetBlueprint", "params": []}
    reference.inject_manage_finalize_params(create)
    assert create["params"] == []


def test_editor_helpers_and_added_prerequisites_are_documented(tmp_path, monkeypatch):
    import build_tool_reference as reference

    monkeypatch.setattr(reference, "load_capability_schemas", lambda: {})
    source = tmp_path / "NexusGetIndependentCapability.cpp"
    cases = [
        ('Configure(Out, false)', []),
        ('ConfigureIndependent(Out, false, TEXT("NexusScene"))', []),
        ('ConfigureIndependent(Out, true, TEXT("NexusPCG"))', ["PluginEnabled:PCG"]),
        ('ConfigureIndependent(Out, true, TEXT("NexusNiagara"))', ["PluginEnabled:Niagara"]),
    ]
    for call, domain in cases:
        source.write_text(
            'Out.Name = TEXT("GetIndependent");\n'
            'Out.Description = TEXT("Read independent data.");\n'
            'Out.Prerequisites = {TEXT("EditorOnly")};\n'
            'FNexusEditorToolSchema::' + call + ';\n'
            'Out.Prerequisites.Add(TEXT("PluginEnabled:SemanticSearch"));\n'
            'Out.Prerequisites.Add(TEXT("EditorOnly"));\n', encoding="utf-8")
        parsed = reference.parse_capability(source)
        assert parsed["prerequisites"] == ["EditorOnly", *domain, "PluginEnabled:SemanticSearch"], (call, parsed)
