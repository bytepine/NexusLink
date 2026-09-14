#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""审计 Capability 命名与 RelatedCapabilities 引用（CI / run_e2e 门禁）。"""

from __future__ import annotations

import re
import sys
from pathlib import Path

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if sys.stderr.encoding and sys.stderr.encoding.lower() != "utf-8":
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

SCRIPT_DIR = Path(__file__).resolve().parent
PLUGIN_ROOT = SCRIPT_DIR.parent
NEXUSLINK_PLUGIN_SOURCE = PLUGIN_ROOT / "Source"
NEXUSLINK_SOURCE = NEXUSLINK_PLUGIN_SOURCE / "NexusLink"
NEXUSLINKEDITOR_SOURCE = NEXUSLINK_PLUGIN_SOURCE / "NexusLinkEditor"
# 双模块拆分后 cap 分散在两个模块根的 Private/Capabilities/ 下
CAP_ROOTS = [
    NEXUSLINK_SOURCE / "Private/Capabilities",
    NEXUSLINKEDITOR_SOURCE / "Private/Capabilities",
]
VERSION_COMPAT_FILE = NEXUSLINK_SOURCE / "Public/Utils/NexusVersionCompat.h"

RE_NAME = re.compile(r'Out\.Name\s*=\s*TEXT\("([^"]+)"\)')
RE_RELATED = re.compile(r"Out\.RelatedCapabilities\s*=\s*\{([^}]+)\}")
RE_TEXT = re.compile(r'TEXT\("([^"]+)"\)')
RE_AT_LEAST_IN_CAP = re.compile(r"NX_UE_AT_LEAST\s*\(")

ALLOWED_FIRST_VERBS = frozenset({
    "search", "list", "get", "set", "manage", "create", "delete", "rename",
    "duplicate", "save", "unload", "spawn", "destroy", "diff", "interact",
    "control", "exec", "eval", "dofile", "gc", "hotreload", "capture",
    "compile", "export", "reimport",
})

FORBIDDEN_NAMES = frozenset({
    "manage_animation",
    "set_runtime_actor_animation",
    "get_asset_generic",
})

META_TOOLS = frozenset({
    "search_capabilities",
    "call_capability",
    "submit_feedback",
})

# 计划缺口：允许存在且无 interact 兄弟（仅登记，不 fail）
PLANNED_GAP_READ_CAPS = frozenset({
})

EXPECTED_CAP_COUNT = 229


def _scan_capability_cpp_files() -> list[Path]:
    files: list[Path] = []
    for root in CAP_ROOTS:
        if root.is_dir():
            files.extend(root.rglob("Nexus*Capability.cpp"))
    return sorted(files)


def _scan_nexuslink_sources_for_raw_at_least() -> list[tuple[Path, int]]:
    """除 NexusVersionCompat.h 外，NexusLink 插件源码禁止裸 NX_UE_AT_LEAST。"""
    hits: list[tuple[Path, int]] = []
    if not NEXUSLINK_PLUGIN_SOURCE.is_dir():
        return hits
    for path in sorted(NEXUSLINK_PLUGIN_SOURCE.rglob("*")):
        if not path.is_file() or path.suffix not in {".cpp", ".h"}:
            continue
        if path.resolve() == VERSION_COMPAT_FILE.resolve():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in RE_AT_LEAST_IN_CAP.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            hits.append((path, line))
    return hits


def _first_verb(name: str) -> str:
    idx = name.find("_")
    return name if idx < 0 else name[:idx]


def _rel(path: Path) -> str:
    try:
        return str(path.relative_to(PLUGIN_ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def main() -> int:
    errors: list[str] = []
    warnings: list[str] = []

    names: set[str] = set()
    related_map: dict[str, list[str]] = {}

    for path in _scan_capability_cpp_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        m = RE_NAME.search(text)
        if not m:
            continue
        name = m.group(1)
        if name in names:
            errors.append(f"duplicate Out.Name '{name}' in {_rel(path)}")
        names.add(name)

        if name in FORBIDDEN_NAMES:
            errors.append(f"forbidden capability name '{name}' in {_rel(path)}")

        verb = _first_verb(name)
        if verb not in ALLOWED_FIRST_VERBS:
            errors.append(
                f"unknown first verb '{verb}' in '{name}' ({_rel(path)})"
            )

        rm = RE_RELATED.search(text)
        if rm:
            related_map[name] = RE_TEXT.findall(rm.group(1))

    # Meta tools（非 Capability cpp 命名，手动登记）
    names.update(META_TOOLS)

    for cap, related in related_map.items():
        for ref in related:
            if ref not in names:
                errors.append(
                    f"{cap}: RelatedCapabilities references unknown '{ref}'"
                )
            if ref in FORBIDDEN_NAMES:
                errors.append(
                    f"{cap}: RelatedCapabilities references forbidden '{ref}'"
                )

    for path, line in _scan_nexuslink_sources_for_raw_at_least():
        errors.append(
            f"{_rel(path)}:{line}: 禁止裸 NX_UE_AT_LEAST；仅在 NexusVersionCompat.h 定义，业务代码用 NX_UE_HAS_*（§1 / CapabilitySpec §8.5）"
        )

    cap_only = names - META_TOOLS
    if len(cap_only) != EXPECTED_CAP_COUNT:
        errors.append(
            f"expected exactly {EXPECTED_CAP_COUNT} capability names under Capabilities/, found {len(cap_only)}"
        )

    for w in warnings:
        print(f"[warn] {w}", file=sys.stderr)
    for e in errors:
        print(f"[error] {e}", file=sys.stderr)

    if errors:
        print(f"[audit] FAIL ({len(errors)} error(s))", file=sys.stderr)
        return 1
    print(f"[audit] PASS ({len(names)} names checked)", file=sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
