#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""审计：代码 Capability 与 tool-reference / 中文 overlay 对齐（报告，默认不 fail）。"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if sys.stderr.encoding and sys.stderr.encoding.lower() != "utf-8":
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

PLUGIN = Path(__file__).resolve().parent.parent
CAP_DIRS = [
    PLUGIN / "Source/NexusLink/Private/Capabilities",
    PLUGIN / "Source/NexusLinkEditor/Private/Capabilities",
]
TOOL_REF = PLUGIN / "docs/tool-reference.md"
TOOL_REF_ZH = PLUGIN / "docs/tool-reference.zh.md"
ZH_JSON = PLUGIN / "scripts/tool_reference_zh.json"

RE_NAME = re.compile(r'Out\.Name\s*=\s*TEXT\("([^"]+)"\)')
RE_DESC = re.compile(r'Out\.Description\s*=\s*TEXT\("([^"]+)"\)')
RE_TOOL_HEAD = re.compile(r"^### `([^`]+)`", re.M)
META = {"search_capabilities", "call_capability", "submit_feedback"}

CAP_DIR_PREFIXES = (
    "Source/NexusLink/Private/Capabilities",
    "Source/NexusLinkEditor/Private/Capabilities",
)


def load_caps() -> dict[str, dict]:
    caps: dict[str, dict] = {}
    cpp_files = [p for d in CAP_DIRS if d.is_dir() for p in d.rglob("*Capability.cpp")]
    for cpp in sorted(cpp_files):
        text = cpp.read_text(encoding="utf-8", errors="ignore")
        m = RE_NAME.search(text)
        if not m:
            continue
        name = m.group(1)
        d = RE_DESC.search(text)
        caps[name] = {
            "file": str(cpp.relative_to(PLUGIN)).replace("\\", "/"),
            "text": text,
            "description": d.group(1) if d else "",
        }
    return caps


def load_zh_descriptions() -> set[str]:
    data = json.loads(ZH_JSON.read_text(encoding="utf-8"))
    return set(data.get("descriptions") or {})


def load_zh_when() -> set[str]:
    data = json.loads(ZH_JSON.read_text(encoding="utf-8"))
    return set(data.get("when_to_use") or {})


def load_tool_ref() -> set[str]:
    return set(RE_TOOL_HEAD.findall(TOOL_REF.read_text(encoding="utf-8")))


def section(title: str, items: list[str], caps: dict[str, dict] | None = None, limit: int = 80):
    print(f"## {title} ({len(items)})")
    for x in items[:limit]:
        extra = ""
        if caps and x in caps:
            extra = f" — {caps[x]['description'][:70]}"
        print(f"  - {x}{extra}")
    if len(items) > limit:
        print(f"  ... +{len(items) - limit} more")
    print()


def main() -> int:
    caps = load_caps()
    code_names = set(caps)
    tool_ref = load_tool_ref()
    zh = load_zh_descriptions()
    zh_when = load_zh_when()
    tool_ref_zh = set(RE_TOOL_HEAD.findall(TOOL_REF_ZH.read_text(encoding="utf-8"))) if TOOL_REF_ZH.is_file() else set()
    tool_ref_caps = tool_ref - META

    print(f"代码 Capability: {len(code_names)}")
    print(f"tool-reference: {len(tool_ref)} (cap {len(tool_ref_caps)} + meta {len(tool_ref & META)})")
    print(f"tool-reference.zh: {len(tool_ref_zh)}")
    print(f"ZH descriptions: {len(zh)}")
    print()

    section("代码有、tool-reference 无", sorted(code_names - tool_ref_caps), caps)
    section("tool-reference 有、代码无", sorted(tool_ref_caps - code_names))
    section("代码有、中文 Description 无", sorted(code_names - zh), caps)
    section("中文 Description 无对应代码", sorted(zh - code_names - META))
    section("英文/中文 tool-reference 标题不一致", sorted(tool_ref.symmetric_difference(tool_ref_zh)))

    gas = sorted(n for n in code_names if "gameplay" in n or "attribute_set" in n)
    section("GAS 相关 Capability（WITH_GAS）", gas, caps)

    by_dir: dict[str, list[str]] = {}
    for name, info in caps.items():
        file_str = info["file"]
        prefix = next(p for p in CAP_DIR_PREFIXES if file_str.startswith(p))
        rel = Path(file_str).relative_to(prefix)
        parts = rel.parts
        key = parts[0] if len(parts) == 2 else f"{parts[0]}/{parts[1]}"
        by_dir.setdefault(key, []).append(name)
    print("## 域目录 Capability 计数")
    for k in sorted(by_dir):
        print(f"  {len(by_dir[k]):3}  {k}")
    print(f"  SUM {sum(len(v) for v in by_dir.values())}")
    print()

    section("代码有、中文 WhenToUse 无（可空，仅统计）", sorted(code_names - zh_when), caps)

    return 0


if __name__ == "__main__":
    sys.exit(main())
