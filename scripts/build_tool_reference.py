#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""
build_tool_reference.py — 从 Capabilities / 元工具源码生成 docs/tool-reference.md（英）与 docs/tool-reference.zh.md（中）。

用法：
  # NexusLink 独立仓（推荐）
  py scripts/build_tool_reference.py [--live URL]

  # NexusWork 工作区
  py nexus-unreal/Script/build_tool_reference.py [--live URL]

  --repo-root  可选；含 NexusLink.uplugin 的目录（默认从脚本位置自动探测）
  --live URL   （可选）对运行中的 UE MCP 端点调 search_capabilities 补全 schema
               示例：--live http://127.0.0.1:45000/stream

输出（同一趟同时写出，禁止只改其中一份）：
  docs/tool-reference.md    = tool-reference.header.md    + AUTO-GENERATED 段（英文）
  docs/tool-reference.zh.md = tool-reference.header.zh.md + AUTO-GENERATED 段（中文）

  AUTO-GENERATED 段由脚本生成，禁止手工编辑；
  手工维护内容在对应 header（通用约定、引言等）。
  中文 Description / WhenToUse / 参数精确表在 scripts/tool_reference_zh.json；
  未收录的新参数说明会按短语规则即时译成中文。

规则：
  - 改 Capability schema 后须重跑本脚本，不要手工改生成段
  - 从 `FNexusSchema::Object()…Build()` 链解析参数；多 section cap 勿用 `.Required({})` 处截断
  - 首次 --live 补全后 schema 才完整（CI 无 UE 时跳过，已尽力从源码提取）
"""

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

_SCRIPTS_DIR = Path(__file__).resolve().parent
if str(_SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS_DIR))
from tool_reference_i18n import (  # noqa: E402
    DocLocale,
    build_en_locale,
    build_zh_locale,
    heading_anchor,
)

# ── 目录分类映射（Capabilities 子目录 → 文档分类名） ────────────────────────────

DIR_CATEGORY: dict[str, str] = {
    "AI":        "AI tools",
    "Animation": "Animation assets",
    "Blueprint": "Blueprint tools",
    "DataAsset": "Data assets (DataAsset / DataTable)",
    "Editor":    "Editor tools",
    "Lua":       "Lua runtime tools",
    "Material":  "Material tools",
    "Runtime":   "Runtime tools",
    "Struct":    "Struct tools",
    "UMG":       "Widget blueprint tools",
    "Asset":     "General asset tools",
}

# 文档章节展示顺序
CATEGORY_ORDER = [
    "Meta tools",
    "Editor tools",
    "General asset tools",
    "Blueprint tools",
    "Animation assets",
    "Material tools",
    "Struct tools",
    "Data assets (DataAsset / DataTable)",
    "Widget blueprint tools",
    "Lua runtime tools",
    "Runtime tools",
    "AI tools",
]

# ── Doc description overrides（key = capability/tool 名称） ──────────────────────────────
# C++ Out.Description / Out.WhenToUse / schema text feed MCP and docs;
# DOC_DESCRIPTIONS / DOC_WHEN_TO_USE optionally override C++ English copy for docs.

DOC_DESCRIPTIONS: dict[str, str] = {
    # Meta
    "call_capability":    "Execute a capability (after search_asset / get_asset_*). On failure check errorKind: unknown/disabled/arg_invalid; do not retry disabled. Legacy names (e.g. create_blackboard) map to canonical names. Batch calls[] and single form are mutually exclusive.",
    "search_capabilities": "**Primary entry** — call before any blueprint/Widget/material/asset question. Prefer `capabilityName=<exact>`; `query` uses 1-2 word AND match. Failures: `errorKind` `not_found` / `disabled` / `disabled_only` (see `disabledCapabilities[]`); `query=get_asset` zero-hit hints route to `get_asset_<type>`. ≤2 matches return full `parameters[]`.",
    "submit_feedback":    "Report capability/tool friction. Trigger: retry ≥2 with no progress, no suitable capability, schema guessing, or forced serial calls ≥3. `category`: `wrong_tool` / `misuse` / `schema_guess` / `search_zero` / `search_overflow` / `other`. Prefer structured fields (`attemptedArgs`, `actualError`, `expectedField`) over long `note`.",
}

DOC_WHEN_TO_USE: dict[str, str] = {}

# ── Regex 模式 ─────────────────────────────────────────────────────────────────

RE_NAME         = re.compile(r'Out\.Name\s*=\s*TEXT\("([^"]+)"\)')
RE_DESC         = re.compile(r'Out\.Description\s*=\s*TEXT\("([^"]+)"\)')
RE_RELATED      = re.compile(r'Out\.RelatedCapabilities\s*=\s*\{([^}]+)\}')
RE_PREREQ       = re.compile(r'Out\.Prerequisites\s*=\s*\{([^}]+)\}')
RE_WHEN         = re.compile(r'Out\.WhenToUse\s*=\s*TEXT\("([^"]+)"\)')
RE_TAG_ACCESS   = re.compile(r'FNexusMcpTags::(Readonly|Write)\b')
RE_TEXT_VALUES  = re.compile(r'TEXT\("([^"]+)"\)')
RE_USE_SECTIONS = re.compile(r'BuildSchemaWithSections\(\)')

# .Required({TEXT("a"), TEXT("b")})
RE_REQUIRED_LIST = re.compile(r'\.Required\s*\(\s*\{([^}]+)\}\s*\)')
# .Prop / .Required(TEXT("name"), FNexusSchema::Type(
RE_PROP_TYPED = re.compile(
    r'\.(Prop|Required)\s*\(\s*TEXT\("([^"]+)"\)\s*,\s*FNexusSchema::'
    r'(StrArr|Str|Int|Bool|Num|EnumArr|Enum|ArrayOf|ArrOfObj|AnyObject)\s*\(',
)
# GetSectionNames return { TEXT("a"), ... }
RE_SECTION_RETURN = re.compile(
    r'GetSectionNames\(\)\s*(?:const\s*)?\{.*?return\s*\{([^}]+)\}',
    re.DOTALL,
)


def extract_text_values(raw: str) -> list[str]:
    return RE_TEXT_VALUES.findall(raw)


def _extract_object_chain_after(text: str, start: int) -> str:
    """从 start 起提取第一个 FNexusSchema::Object()…匹配 .Build()，跳过嵌套 Object()。"""
    key = "FNexusSchema::Object()"
    idx = text.find(key, start)
    if idx < 0:
        return ""
    pos = idx + len(key)
    depth = 1
    i = pos
    n = len(text)
    while i < n and depth > 0:
        if text.startswith(key, i):
            depth += 1
            i += len(key)
            continue
        if text.startswith(".Build()", i):
            depth -= 1
            if depth == 0:
                return text[pos:i]
            i += len(".Build()")
            continue
        i += 1
    return ""


def extract_schema_object_chain(text: str, anchor: str | None = None) -> str:
    """
    从 C++ 源码提取 FNexusSchema::Object()…Build() 之间的链式调用文本。
    anchor 为 'BuildCapabilitySchema' 或 'InputSchema' 时仅在对应函数/赋值块内搜索。
    嵌套 Object()…Build()（如 ArrayOf 内联 item）不会被误截断。
    若 InputSchema 为 lambda/IIFE（先建 ItemSchema 再 return Object），优先取 return 链。
    """
    search_from = 0
    region_end = len(text)
    if anchor == "BuildCapabilitySchema":
        m = re.search(
            r"BuildCapabilitySchema\s*\(\s*\)\s*const\s*\{",
            text,
        )
        if m:
            search_from = m.start()
    elif anchor == "InputSchema":
        m = re.search(r"Out\.InputSchema\s*=", text)
        if m:
            search_from = m.start()

    region = text[search_from:region_end]
    mret = re.search(r"return\s+FNexusSchema::Object\(\)", region)
    if mret:
        chain = _extract_object_chain_after(region, mret.start())
        if chain:
            return chain
    return _extract_object_chain_after(region, 0)


def _first_text_literal(s: str) -> str:
    m = re.search(r'TEXT\("((?:[^"\\]|\\.)*)"\)', s)
    return m.group(1) if m else ""


def _extract_enum_values_after_desc(call_body: str) -> list[str]:
    """从 Enum(TEXT(desc), { TEXT(v)... } [, TEXT(default)]) 提取枚举值（不含 default）。"""
    m = re.search(r"\{([^}]*)\}", call_body)
    if not m:
        return []
    return extract_text_values(m.group(1))


# 参数说明兜底（C++ 未写描述或解析失败时）
COMMON_PARAM_DESCRIPTIONS: dict[str, str] = {
    "assetPath":       "Asset package path (from `search_asset`, format `/Game/...`)",
    "destAssetPath":   "Full destination asset path (package + asset name)",
    "sections":        "Query sections (multi-select); see each cap's section list",
    "propertyPaths":   "Reflection property paths (dotted, e.g. `Health` / `Mesh.RelativeLocation`)",
    "propertyPath":    "Single reflection property path (inside operations/updates entries)",
    "actorName":       "Runtime actor name (PIE world `GetName()`)",
    "widgetName":      "Runtime widget name",
    "ownerClass":      "Owner UserWidget class filter",
    "classFilter":     "Class name filter (substring/wildcard, optional)",
    "nameFilter":      "Name or tag filter (optional)",
    "tagFilter":       "Actor tag exact match (optional)",
    "pathFilter":      "Content path prefix (e.g. `/Game/Feature/`)",
    "assetType":       "Asset type (e.g. Blueprint, Widget, World); no bare `all` full scan",
    "query":           "Search keywords (1–2 words, AND match)",
    "offset":          "Pagination offset (from 0)",
    "limit":           "Max items per page",
    "capability":      "Exact capability name",
    "capabilityName":  "Exact capability name (`search_capabilities` shortcut)",
    "arguments":       "JSON object for the capability (nested; do not flatten to top level)",
    "calls":           "Batch call list: `[{capability, arguments}, ...]`",
    "operations":      "Batch operations: `[{action, ...}, ...]`",
    "updates":         "Batch property updates: `[{propertyPath, value, ...}, ...]`",
    "action":          "Operation name (see per-cap enum docs)",
    "mode":            "Mode (see per-cap enum docs)",
    "direction":       "Dependency direction: `dependencies` / `referencers`",
    "category":        "Feedback or log category",
    "note":            "Additional notes (optional)",
    "scriptPath":      "Lua script path (relative to Content/Script/)",
    "luaPath":         "Lua dotted path",
    "className":       "Native UClass name",
    "keepLoaded":      "When true, do not auto-unload packages introduced by this call",
}


def infer_category(cpp_path: Path) -> str:
    """
    从 Capability cpp 文件路径推断文档分类。
    规则：Capabilities/ 的直接子目录优先级最高（如 Lua/、Asset/），
    次深子目录仅在直接子目录无独立语义时才提升（如 Asset/Blueprint → Blueprint tools）。
    """
    parts = cpp_path.parts
    for i, p in enumerate(parts):
        if p == "Capabilities" and i + 1 < len(parts):
            top_sub = parts[i + 1]
            # Lua/ 下所有内容均属 Lua 运行时工具，不被其子目录 Runtime/ 覆盖
            if top_sub == "Lua":
                return DIR_CATEGORY["Lua"]
            # 其余情况：最深子目录（最具体）优先
            for seg in reversed(parts[i + 1 : -1]):
                if seg in DIR_CATEGORY:
                    return DIR_CATEGORY[seg]
            return DIR_CATEGORY.get(top_sub, "General asset tools")
    return "General asset tools"


def _map_type(schema_type: str) -> str:
    return {
        "Str":       "string",
        "StrArr":    "string[]",
        "Num":       "number",
        "Int":       "integer",
        "Bool":      "boolean",
        "Enum":      "string (enum)",
        "EnumArr":   "string[]",
        "AnyObject": "object",
        "ArrayOf":   "object[]",
        "ArrOfObj":  "object[]",
        "Array":     "array",
    }.get(schema_type, "string")


def _parse_item_schema_props(full_text: str, schema_chain: str, array_call_body: str) -> list[dict[str, Any]]:
    """尝试解析 ArrayOf 的 item Object Schema（变量引用或内联 Object）。"""
    # 内联：ArrayOf(desc, FNexusSchema::Object()…Build())
    inline = _extract_object_chain_after(array_call_body, 0)
    if inline.strip():
        nested, _ = parse_schema_block(inline)
        return nested

    # 变量：ArrayOf(desc, OpSchema.ToSharedRef()) / ItemSchema
    m = re.search(
        r',\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?:\.ToSharedRef\s*\(\s*\))?\s*$',
        array_call_body.strip(),
        re.DOTALL,
    )
    if not m:
        return []
    var = m.group(1)
    pat = re.compile(
        rf'(?:const\s+)?(?:TSharedPtr|TSharedRef)\s*<\s*FJsonObject\s*>\s+{re.escape(var)}\s*=\s*FNexusSchema::Object\(\)'
        rf'|(?:const\s+)?auto\s+{re.escape(var)}\s*=\s*FNexusSchema::Object\(\)'
        rf'|{re.escape(var)}\s*=\s*FNexusSchema::Object\(\)'
    )
    vm = pat.search(full_text)
    if not vm:
        return []
    item_chain = _extract_object_chain_after(full_text, vm.start())
    if not item_chain:
        return []
    nested, _ = parse_schema_block(item_chain)
    return nested


def parse_schema_block(
    text: str,
    *,
    full_source: str | None = None,
) -> tuple[list[dict[str, Any]], set[str]]:
    """
    从 C++ schema 文本片段中提取 (params_list, required_set)。
    params_list 每项为 {"name", "type", "description", ["enum"], ["items"]}。
    full_source 用于解析 ArrayOf 引用的 OpSchema / ItemSchema 变量。
    """
    params: list[dict[str, Any]] = []
    required_fields: set[str] = set()

    for m in RE_REQUIRED_LIST.finditer(text):
        for v in extract_text_values(m.group(1)):
            required_fields.add(v)

    def _add_param(
        method: str,
        pname: str,
        schema_type: str,
        desc: str,
        enum_vals: list[str] | None = None,
        items: list[dict[str, Any]] | None = None,
    ) -> None:
        if pname in seen:
            return
        seen.add(pname)
        if method == "Required":
            required_fields.add(pname)
        p: dict[str, Any] = {
            "name":        pname,
            "type":        _map_type(schema_type),
            "description": desc,
        }
        if schema_type in {"Enum", "EnumArr"} or enum_vals:
            if schema_type == "EnumArr":
                p["type"] = "string[]"
            else:
                p["type"] = "string (enum)"
            if enum_vals:
                p["enum"] = enum_vals
        if items:
            p["items"] = items
        params.append(p)

    seen: set[str] = set()
    src_for_items = full_source if full_source is not None else text

    for m in RE_PROP_TYPED.finditer(text):
        method, pname, schema_type = m.group(1), m.group(2), m.group(3)
        body_start = m.end()
        depth = 1
        i = body_start
        while i < len(text) and depth > 0:
            ch = text[i]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            i += 1
        call_body = text[body_start : i - 1]
        desc = _first_text_literal(call_body)
        enum_vals: list[str] | None = None
        items: list[dict[str, Any]] | None = None
        if schema_type in {"Enum", "EnumArr"}:
            enum_vals = _extract_enum_values_after_desc(call_body) or None
        if schema_type in {"ArrayOf", "ArrOfObj"}:
            nested = _parse_item_schema_props(src_for_items, text, call_body)
            if nested:
                items = nested
        _add_param(method, pname, schema_type, desc, enum_vals, items)

    return params, required_fields


def parse_section_names(text: str) -> list[str]:
    """从 GetSectionNames() 方法体提取 section 枚举值"""
    m = RE_SECTION_RETURN.search(text)
    if m:
        return extract_text_values(m.group(1))
    return []


MANAGE_COMPILE_CAPS = frozenset({
    "manage_asset_blueprint",
    "manage_asset_anim_blueprint",
    "manage_asset_user_widget",
})


def inject_manage_finalize_params(cap: dict[str, Any]) -> None:
    """与 FNexusCapability::GetDefinition 框架注入对齐：全 manage 加 saveToDisk；BP/ABP/WBP 加 compile。"""
    name = cap.get("name") or ""
    if not name.startswith("manage_asset_"):
        return
    params: list[dict[str, Any]] = cap.setdefault("params", [])
    existing = {p.get("name") for p in params}
    if "saveToDisk" not in existing:
        params.append({
            "name":        "saveToDisk",
            "type":        "boolean",
            "description": "Save the package to disk after success",
        })
    if name in MANAGE_COMPILE_CAPS and "compile" not in existing:
        params.append({
            "name":        "compile",
            "type":        "boolean",
            "description": "Compile blueprint if needed (BP/ABP/WBP only)",
        })


def parse_capability(cpp_path: Path) -> dict[str, Any] | None:
    """解析单个 Capability cpp 文件，返回 capability dict 或 None"""
    text = cpp_path.read_text(encoding="utf-8", errors="replace")

    m_name = RE_NAME.search(text)
    m_desc = RE_DESC.search(text)
    if not m_name or not m_desc:
        return None

    cap: dict[str, Any] = {
        "name":         m_name.group(1),
        "description":  m_desc.group(1),
        "category":     infer_category(cpp_path),
        "file":         str(cpp_path),
        "params":       [],
        "required":     set(),
        "related":      [],
        "prerequisites":[],
        "sections":     [],
        "access":       "readonly",
        "when_to_use":  "",
    }

    # 读写属性
    access_tags = RE_TAG_ACCESS.findall(text)
    if "Write" in access_tags:
        cap["access"] = "write"

    m_rel = RE_RELATED.search(text)
    if m_rel:
        cap["related"] = extract_text_values(m_rel.group(1))

    m_pre = RE_PREREQ.search(text)
    if m_pre:
        cap["prerequisites"] = extract_text_values(m_pre.group(1))

    m_when = RE_WHEN.search(text)
    if m_when:
        cap["when_to_use"] = m_when.group(1)

    # Schema 解析（Object()…Build() 链，避免 .Required({}) 导致早停）
    if RE_USE_SECTIONS.search(text):
        schema_text = extract_schema_object_chain(text, "BuildCapabilitySchema")
        if schema_text:
            params, req = parse_schema_block(schema_text, full_source=text)
            cap["params"]   = params
            cap["required"] = req
        cap["sections"] = parse_section_names(text)
    else:
        schema_text = extract_schema_object_chain(text, "InputSchema")
        if not schema_text:
            schema_text = extract_schema_object_chain(text)
        if schema_text:
            params, req = parse_schema_block(schema_text, full_source=text)
            cap["params"]   = params
            cap["required"] = req

    inject_manage_finalize_params(cap)
    return cap


def parse_meta_tool(cpp_path: Path) -> dict[str, Any] | None:
    """解析元工具（MCP Tool）cpp 文件"""
    text = cpp_path.read_text(encoding="utf-8", errors="replace")

    m_name = RE_NAME.search(text)
    m_desc = RE_DESC.search(text)
    if not m_name or not m_desc:
        return None

    tool: dict[str, Any] = {
        "name":         m_name.group(1),
        "description":  m_desc.group(1),
        "category":     "Meta tools",
        "file":         str(cpp_path),
        "params":       [],
        "required":     set(),
        "related":      [],
        "prerequisites":[],
        "sections":     [],
        "access":       "write",
        "when_to_use":  "",
    }

    schema_text = extract_schema_object_chain(text, "InputSchema")
    if schema_text:
        params, req = parse_schema_block(schema_text, full_source=text)
        tool["params"]   = params
        tool["required"] = req

    return tool


def render_cap_section(cap: dict[str, Any], locale: DocLocale) -> str:
    name = cap["name"]
    desc = locale.cap_description(name, cap["description"])
    labels = locale.labels
    lines = [f'### `{name}`\n', f'{desc}\n']

    if cap.get("prerequisites"):
        prereq_str = " / ".join(f"`{p}`" for p in cap["prerequisites"])
        lines.append(f'**{labels["prerequisites"]}**: {prereq_str}\n')

    when = locale.cap_when(name, cap.get("when_to_use", ""))
    if when:
        lines.append(f'**{labels["when_to_use"]}**: {when}\n')

    required_set: set[str] = cap.get("required", set())
    params_display: list[dict[str, Any]] = []

    sections: list[str] = cap.get("sections", [])
    if sections:
        sec_vals = " / ".join(f'`{s}`' for s in sections)
        params_display.append({
            "name":        "sections",
            "type":        "string[]",
            "required":    False,
            "description": f'{labels["sections"]}: {sec_vals}',
        })

    for p in cap.get("params", []):
        params_display.append({
            **p,
            "required": p["name"] in required_set,
        })

    if params_display:
        lines.append(
            f'| {labels["parameter"]} | {labels["type"]} | {labels["required"]} | {labels["description"]} |'
        )
        lines.append("|------|------|:----:|------|")
        for p in params_display:
            req_mark = "★" if p.get("required") else ""
            typ = p.get("type", "string")
            desc_p = locale.param_desc(p["name"], p.get("description") or "", COMMON_PARAM_DESCRIPTIONS)
            if "enum" in p:
                enum_vals = " / ".join(f'`{v}`' for v in p["enum"])
                enum_tag = labels["enum"]
                desc_p = f'{desc_p} {enum_tag}: {enum_vals}' if desc_p else f'{enum_tag}: {enum_vals}'
            items = p.get("items") or []
            if items:
                item_bits = []
                for it in items:
                    bit = f'`{it["name"]}`'
                    if it.get("enum"):
                        bit += "(" + "/".join(it["enum"][:8]) + ("…" if len(it["enum"]) > 8 else "") + ")"
                    item_bits.append(bit)
                desc_p = (desc_p + "; " if desc_p else "") + f'{labels["item"]}: ' + ", ".join(item_bits)
            lines.append(f'| `{p["name"]}` | `{typ}` | {req_mark} | {desc_p} |')
        lines.append("")

    if cap.get("related"):
        rel_str = ", ".join(f'`{r}`' for r in cap["related"])
        lines.append(f'**{labels["related"]}**: {rel_str}\n')

    return "\n".join(lines)


def render_markdown(
    categories: dict[str, list[dict]],
    header: str,
    locale: DocLocale,
) -> str:
    cap_count  = sum(len(v) for k, v in categories.items() if k != "Meta tools")
    tool_count = len(categories.get("Meta tools", []))

    parts: list[str] = [header.rstrip(), ""]
    parts.append("<!-- AUTO-GENERATED by build_tool_reference.py — do not edit below -->")
    parts.append(f"<!-- {cap_count} capabilities + {tool_count} meta-tools -->")
    parts.append("")

    parts.append(f'## {locale.labels["contents"]}\n')
    for cat in CATEGORY_ORDER:
        if cat not in categories:
            continue
        display = locale.category(cat)
        parts.append(f"- [{display}](#{heading_anchor(display)})")
    parts.append("")
    parts.append("---")
    parts.append("")

    for cat in CATEGORY_ORDER:
        if cat not in categories:
            continue
        caps = sorted(categories[cat], key=lambda c: c["name"])
        parts.append(f"## {locale.category(cat)}\n")
        for cap in caps:
            parts.append(render_cap_section(cap, locale))
            parts.append("---")
            parts.append("")

    return "\n".join(parts)


# ── live schema 补全（可选，需要运行中的 UE） ──────────────────────────────────

def load_live_schema(url: str, cap_name: str) -> list[dict] | None:
    try:
        import httpx  # type: ignore
    except ImportError:
        return None

    try:
        # initialize
        r = httpx.post(
            url,
            json={
                "jsonrpc": "2.0", "id": 1, "method": "initialize",
                "params": {
                    "protocolVersion": "2025-06-18",
                    "clientInfo": {"name": "build_tool_reference", "version": "1.0"},
                    "capabilities": {},
                },
            },
            timeout=10,
        )
        r.raise_for_status()
        session_id = r.headers.get("Mcp-Session-Id", "")
        headers = {"Mcp-Session-Id": session_id} if session_id else {}

        # search_capabilities
        r2 = httpx.post(
            url, headers=headers,
            json={
                "jsonrpc": "2.0", "id": 2, "method": "tools/call",
                "params": {
                    "name": "search_capabilities",
                    "arguments": {"capabilityName": cap_name},
                },
            },
            timeout=15,
        )
        r2.raise_for_status()
        result = r2.json().get("result", {})
        content = result.get("content", [])
        if content:
            text_item = next((c for c in content if c.get("type") == "text"), None)
            if text_item:
                data = json.loads(text_item.get("text", "{}"))
                return data.get("capability", {}).get("parameters")
    except Exception as e:
        print(f"  [live] {cap_name}: {e}", file=sys.stderr)

    return None


def find_nexuslink_root(start: Path) -> Path:
    """定位含 NexusLink.uplugin 的插件仓根目录。"""
    for base in (start, start.parent, start.parent.parent):
        if (base / "NexusLink.uplugin").is_file():
            return base
        nested = base / "Plugins" / "Developer" / "NexusLink"
        if (nested / "NexusLink.uplugin").is_file():
            return nested
    raise SystemExit(f"ERROR: NexusLink.uplugin not found near {start}")


def resolve_source_paths(link_root: Path) -> tuple[Path, Path, Path]:
    """返回 cap_root, tools_dir, docs_dir。"""
    plugin_src = link_root / "Source" / "NexusLink"
    cap_root = plugin_src / "Private" / "Capabilities"
    tools_dir = plugin_src / "Private" / "Tools"
    return cap_root, tools_dir, link_root / "docs"


def write_locale_doc(
    categories: dict[str, list[dict[str, Any]]],
    docs_dir: Path,
    locale: DocLocale,
) -> Path:
    header_path = docs_dir / locale.header_name
    output_path = docs_dir / locale.output_name
    if header_path.exists():
        header = header_path.read_text(encoding="utf-8")
    else:
        fallback = "# NexusLink Tool Reference\n\n" if locale.code == "en" else "# NexusLink 工具参考手册\n\n"
        header = fallback
        print(f"WARNING: header 文件不存在 {header_path}，使用最小 header", file=sys.stderr)

    if locale.warn_missing:
        names = [c["name"] for caps in categories.values() for c in caps]
        missing = [n for n in names if n not in locale.descriptions]
        if missing:
            print(
                f"WARNING: {len(missing)} caps 无中文 Description（已回落英文，请补 scripts/tool_reference_zh.json）："
                + ", ".join(missing[:12])
                + ("…" if len(missing) > 12 else ""),
                file=sys.stderr,
            )

    content = render_markdown(categories, header, locale)
    output_path.write_text(content, encoding="utf-8")
    size_kb = output_path.stat().st_size // 1024
    print(f"生成完毕：{output_path}（{size_kb} KB）", file=sys.stderr)
    return output_path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo-root", default=None, help="NexusLink 仓根目录（含 NexusLink.uplugin）；默认自动探测")
    parser.add_argument("--live", default=None, metavar="URL",
                        help="UE MCP 端点 URL，用于 live schema 补全（可选，CI 不需要）")
    parser.add_argument("--lang", choices=("all", "en", "zh"), default="all",
                        help="生成语言（默认 all=中英同时写出）")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    link_root = Path(args.repo_root) if args.repo_root else find_nexuslink_root(script_dir)
    cap_root, tools_dir, docs_dir = resolve_source_paths(link_root)

    if not cap_root.exists():
        print(f"ERROR: Capabilities dir not found: {cap_root}", file=sys.stderr)
        sys.exit(1)

    # ── 解析 Capabilities ──────────────────────────────────────────────────────
    categories: dict[str, list[dict[str, Any]]] = {}
    cap_count = 0
    for cpp_file in sorted(cap_root.rglob("*.cpp")):
        cap = parse_capability(cpp_file)
        if cap:
            cat = cap["category"]
            categories.setdefault(cat, []).append(cap)
            cap_count += 1

    # ── 解析元工具 ─────────────────────────────────────────────────────────────
    META_ORDER = ["search_capabilities", "call_capability", "submit_feedback"]
    meta_tools: dict[str, dict] = {}
    if tools_dir.exists():
        for cpp_file in tools_dir.glob("*.cpp"):
            tool = parse_meta_tool(cpp_file)
            if tool and tool["name"] in META_ORDER:
                meta_tools[tool["name"]] = tool
    categories["Meta tools"] = [
        meta_tools[n] for n in META_ORDER if n in meta_tools
    ]

    print(f"解析完成：{cap_count} capabilities, {len(meta_tools)} meta tools", file=sys.stderr)
    for cat, caps in categories.items():
        print(f"  {cat}: {len(caps)}", file=sys.stderr)

    # ── 可选：live schema 补全 ─────────────────────────────────────────────────
    if args.live:
        print(f"\nLive schema supplement: {args.live}", file=sys.stderr)
        for cap_list in categories.values():
            for cap in cap_list:
                live_params = load_live_schema(args.live, cap["name"])
                if live_params:
                    live_map = {p["name"]: p for p in live_params}
                    merged: list[dict] = []
                    seen_live: set[str] = set()
                    for p in cap["params"]:
                        lp = live_map.get(p["name"])
                        if lp:
                            merged.append({**p, **lp})
                            seen_live.add(p["name"])
                        else:
                            merged.append(p)
                    for lname, lp in live_map.items():
                        if lname not in seen_live:
                            merged.append(lp)
                    cap["params"] = merged
                    print(f"  ✓ {cap['name']} ({len(merged)} params)", file=sys.stderr)

    locales: list[DocLocale] = []
    if args.lang in ("all", "en"):
        locales.append(build_en_locale(
            doc_descriptions=DOC_DESCRIPTIONS,
            doc_when=DOC_WHEN_TO_USE,
            common_params=COMMON_PARAM_DESCRIPTIONS,
        ))
    if args.lang in ("all", "zh"):
        locales.append(build_zh_locale(common_param_zh={}))

    print("", file=sys.stderr)
    for loc in locales:
        write_locale_doc(categories, docs_dir, loc)
    print("禁止手工编辑 AUTO-GENERATED 段，改 schema 后重跑本脚本（中英同时更新）。", file=sys.stderr)


if __name__ == "__main__":
    main()
