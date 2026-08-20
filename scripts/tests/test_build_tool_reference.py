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


def test_heading_anchor_keeps_ascii_parens():
    assert heading_anchor("Data assets (DataAsset / DataTable)") == "data-assets-(dataasset-datatable)"
    assert heading_anchor("数据资产（DataAsset / DataTable）") == "数据资产-dataasset-datatable"


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
    assert "search_capabilities" in en
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
