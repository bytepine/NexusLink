#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""审计 Capability 入参命名（Schema / Execute）— CI / run_e2e 门禁。

权威：Resources/CapabilitySpec.md §8.11 / §8.12。
扫描 Source/**/Capabilities/**/*.cpp，禁止 Breaking 旧键与多目标数组。
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

_SCRIPTS_DIR = Path(__file__).resolve().parent
if str(_SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS_DIR))
from schema_extract import extract_object_chain_after  # noqa: E402
from schema_catalog import load_capability_schemas, schema_top_prop_names  # noqa: E402

PLUGIN_ROOT = _SCRIPTS_DIR.parent
DEFAULT_CAP_ROOT = PLUGIN_ROOT / "Source" / "NexusLink" / "Private" / "Capabilities"

RE_NAME = re.compile(r'Out\.Name\s*=\s*TEXT\("([^"]+)"\)')
RE_PROP_NAME = re.compile(r'\.(?:Prop|Required)\s*\(\s*TEXT\("([^"]+)"\)')
RE_REQUIRED_LIST = re.compile(r'\.Required\s*\(\s*\{([^}]+)\}\s*\)')
RE_TEXT = re.compile(r'TEXT\("([^"]+)"\)')

# Arguments / Args 侧读入（不含 Entry->SetStringField 响应回显）
RE_ARG_READ = re.compile(
    r"(?:"
    r'RequireString\s*\(\s*\w+\s*,\s*TEXT\("([^"]+)"\)'
    r"|TryGet(?:String|Array|Bool|Number)Field\s*\(\s*(?:Arguments|Args)\s*,\s*TEXT\(\"([^\"]+)\"\)"
    r"|(?:Arguments|Args)\s*->\s*(?:TryGet\w*Field|HasField|Get(?:String|Array|Bool|Number)Field)\s*\(\s*TEXT\(\"([^\"]+)\"\)"
	r"|FNexusJsonUtils::(?:GetStringArray|ExtractOperations)\s*\(\s*(?:Arguments|Args)\s*,\s*TEXT\(\"([^\"]+)\"\)"
    r"|\.(?:Str|Num|Bool|StrArr)\s*\(\s*TEXT\(\"([^\"]+)\""
    r")"
)

# 绝对禁止出现在顶层 Schema 或 Arguments 读入中的旧键
FORBIDDEN_ALWAYS = frozenset(
    {
        "assetPaths",
        "actorNames",
        "widgetNames",
        "newPath",
        "ownerWidget",
        "ops",
        "blueprintPath",
        "classPath",
        "filePath",
    }
)

# manage 顶层禁止作为操作容器的领域数组键
MANAGE_TOP_CONTAINERS = frozenset({"fields", "rows", "keys", "widgets"})


@dataclass
class Violation:
    path: Path
    cap: str
    rule: str
    detail: str

    def format(self, root: Path | None = None) -> str:
        try:
            rel = self.path.relative_to(root) if root else self.path
        except ValueError:
            rel = self.path
        return f"{rel}: [{self.cap}] {self.rule}: {self.detail}"


@dataclass
class CapScan:
    path: Path
    name: str
    top_props: set[str] = field(default_factory=set)
    arg_reads: set[str] = field(default_factory=set)
    is_lua: bool = False


def extract_top_level_schema_text(text: str) -> str:
    """提取顶层 InputSchema / BuildCapabilitySchema 的 Object() 链（不含 Op/Item Schema 前置块）。"""
    m = re.search(r"Out\.InputSchema\s*=", text)
    if m:
        region = text[m.start() :]
        # 常见写法：lambda/IIFE 内先建 ItemSchema，再 return FNexusSchema::Object()…
        mret = re.search(r"return\s+FNexusSchema::Object\(\)", region)
        if mret:
            chain = extract_object_chain_after(region, mret.start())
            if chain:
                return chain
        chain = extract_object_chain_after(region, 0)
        if chain:
            # 若首个 Object 是 item 块（无 assetPath/operations 等顶层键），尝试后续 Object
            props = _parse_schema_prop_names_depth0(chain)
            if not props.intersection({"assetPath", "operations", "updates", "actorName", "widgetName"}):
                # 从第一个 Object 结束之后再找
                key = "FNexusSchema::Object()"
                idx = region.find(key)
                if idx >= 0:
                    after = idx + len(key) + len(chain) + len(".Build()")
                    chain2 = extract_object_chain_after(region, after)
                    if chain2:
                        return chain2
            return chain

    m = re.search(r"BuildCapabilitySchema\s*\(\s*\)\s*const\s*\{", text)
    if m:
        region = text[m.start() :]
        mret = re.search(r"return\s+FNexusSchema::Object\(\)", region)
        if mret:
            chain = extract_object_chain_after(region, mret.start())
            if chain:
                return chain
        chain = extract_object_chain_after(region, 0)
        if chain:
            return chain
    return extract_object_chain_after(text, 0)


def _parse_schema_prop_names_depth0(schema_text: str) -> set[str]:
    """只收集嵌套 Object() 深度为 0 的 Prop/Required 名（忽略 item schema 内字段）。"""
    names: set[str] = set()
    depth = 0
    i = 0
    key = "FNexusSchema::Object()"
    n = len(schema_text)
    while i < n:
        if schema_text.startswith(key, i):
            depth += 1
            i += len(key)
            continue
        if schema_text.startswith(".Build()", i):
            if depth > 0:
                depth -= 1
            i += len(".Build()")
            continue
        if depth == 0:
            m = RE_PROP_NAME.match(schema_text, i)
            if m:
                names.add(m.group(1))
                i = m.end()
                continue
            m2 = RE_REQUIRED_LIST.match(schema_text, i)
            if m2:
                names.update(RE_TEXT.findall(m2.group(1)))
                i = m2.end()
                continue
        i += 1
    return names


def parse_schema_prop_names(schema_text: str) -> set[str]:
    return _parse_schema_prop_names_depth0(schema_text)


def parse_arg_reads(text: str) -> set[str]:
    names: set[str] = set()
    for m in RE_ARG_READ.finditer(text):
        for g in m.groups():
            if g:
                names.add(g)
    return names


def scan_capability_text(text: str, path: Path | None = None) -> CapScan | None:
    m = RE_NAME.search(text)
    if not m:
        return None
    name = m.group(1)
    catalog = load_capability_schemas() or {}
    dumped = catalog.get(name)
    if isinstance(dumped, dict) and dumped.get("properties"):
        top_props = schema_top_prop_names(dumped)
    else:
        schema = extract_top_level_schema_text(text)
        top_props = parse_schema_prop_names(schema)
    is_lua = False
    if path is not None:
        parts = {p.lower() for p in path.parts}
        is_lua = "lua" in parts
    elif "/Lua/" in text or "\\Lua\\" in text:
        is_lua = True
    return CapScan(
        path=path or Path("<memory>"),
        name=name,
        top_props=top_props,
        arg_reads=parse_arg_reads(text),
        is_lua=is_lua,
    )


def audit_cap(cap: CapScan) -> list[Violation]:
    errs: list[Violation] = []
    name = cap.name
    props = cap.top_props
    reads = cap.arg_reads
    combined = props | reads

    def add(rule: str, detail: str) -> None:
        errs.append(Violation(cap.path, name, rule, detail))

    for key in sorted(FORBIDDEN_ALWAYS & combined):
        where = []
        if key in props:
            where.append("Schema")
        if key in reads:
            where.append("Execute")
        add("forbidden_param", f"禁止旧键 `{key}`（{'+'.join(where)}）→ 见 CapabilitySpec §8.12")

    if "packagePath" in combined and "assetName" in combined:
        add(
            "packagePath_assetName",
            "禁止 packagePath+assetName 组合；统一 `assetPath`",
        )
    elif "packagePath" in combined:
        add("packagePath", "禁止 packagePath；统一 `assetPath`")

    # get 侧单数 propertyPath（顶层 Schema / Arguments）
    if name.startswith("get_") and "propertyPath" in combined:
        add(
            "get_singular_propertyPath",
            "get 侧禁止单数 `propertyPath`；只用 `propertyPaths[]`",
        )

    # Lua 入参 path / filePath（filePath 已在 FORBIDDEN_ALWAYS）
    if cap.is_lua and "path" in combined:
        add("lua_path", "Lua 入参禁止 `path`；改用 `luaPath`（dofile 用 `scriptPath`）")

    # manage：必须 operations；禁止顶层领域容器与顶层 action
    if name.startswith("manage_"):
        if "operations" not in props:
            add("manage_operations", "manage_* Schema 必须暴露 `operations[]`")
        for key in sorted(MANAGE_TOP_CONTAINERS & props):
            add(
                "manage_top_container",
                f"禁止顶层 `{key}` 作为 manage 操作容器；改用 `operations[]`",
            )
        if "action" in props:
            add(
                "manage_top_action",
                "禁止 manage 顶层裸 `action`；操作项放在 `operations[].action`",
            )

    # set_*_property：必须 updates
    if re.match(r"set_.*_property$", name) and "updates" not in props:
        add("set_updates", "set_*_property Schema 必须暴露 `updates[]`")

    return errs


def audit_file(path: Path) -> list[Violation]:
    text = path.read_text(encoding="utf-8-sig", errors="replace")
    cap = scan_capability_text(text, path)
    if not cap:
        return []
    return audit_cap(cap)


def audit_tree(cap_root: Path) -> list[Violation]:
    if not cap_root.is_dir():
        return [
            Violation(
                cap_root,
                "-",
                "missing_root",
                f"Capabilities 目录不存在: {cap_root}",
            )
        ]
    errs: list[Violation] = []
    for path in sorted(cap_root.rglob("*.cpp")):
        errs.extend(audit_file(path))
    return errs


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cap-root",
        type=Path,
        default=DEFAULT_CAP_ROOT,
        help="Capabilities 根目录（默认同插件 Source/.../Capabilities）",
    )
    args = parser.parse_args(argv)

    gen = _SCRIPTS_DIR / "gen_legacy_capability_names.py"
    if gen.is_file():
        import subprocess
        chk = subprocess.run([sys.executable, str(gen), "--check"], cwd=str(PLUGIN_ROOT))
        if chk.returncode != 0:
            return chk.returncode

    cap_root = args.cap_root.resolve()
    errors = audit_tree(cap_root)

    for e in errors:
        print(f"[error] {e.format(PLUGIN_ROOT)}", file=sys.stderr)

    if errors:
        print(f"[audit_params] FAIL ({len(errors)} error(s))", file=sys.stderr)
        return 1

    n_files = len(list(cap_root.rglob("*.cpp"))) if cap_root.is_dir() else 0
    print(f"[audit_params] PASS ({n_files} cpp files scanned)", file=sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
