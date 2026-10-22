#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""报告 C++ 命名（CapabilitySpec §6.1 / §8.3）。默认不失败，不接入 CI。

检查：
- 头文件里至少一个类型对得上文件名（U/A/F/E/I/T 去掉前缀；Slate 的 S 保留在文件名里）
- 类型名带这些前缀
- bool 变量 / 参数是 b + 大写
- 标识符不要 k 前缀（kMax 这种）
- 协议工具 id、Prop / JSON 字段、Enum 取值：PascalCase；布尔字段 b 前缀
  JSON Schema 关键字、MCP 信封，以及 Server/ 里的 MCP 握手字段保持原样

不检查「变量名是否表达角色」和单字母。那是评审，不是正则。
名叫 type / description 的字段和 Schema 关键字分不开，所以这两个词全局放行。
"""

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
DEFAULT_ROOTS = [PLUGIN_ROOT / "Source"]
SKIP_DIRS = {"Intermediate", "Binaries", "ThirdParty"}
TYPE_PREFIXES = frozenset("UAFETI")

# JSON Schema 关键字 + MCP 信封。协议原样，不套 PascalCase / b 前缀。
# ponytail: type/description 全局放行。要按「是不是 Schema 对象」区分，得做类型分析。
ALLOW_KEYS = frozenset({
    "jsonrpc", "method", "params", "id", "result", "error",
    "isError", "structuredContent", "content", "text",
    "type", "properties", "required", "enum", "items", "description",
    "additionalProperties", "default", "minimum", "maximum",
    "minLength", "maxLength", "minItems", "maxItems",
    "pattern", "format", "anyOf", "oneOf", "allOf",
    "$schema", "$ref", "title", "const", "examples",
})
# MCP 握手 / tools/list 字段。只在 Server/ 放行，Capability 自己的 name 仍要查。
MCP_WIRE_KEYS = frozenset({
    "protocolVersion", "capabilities", "tools", "listChanged",
    "serverInfo", "name", "version", "instructions",
    "description", "inputSchema", "annotations", "readOnlyHint", "code", "message",
})
# 不是动作名的枚举取值（图片格式等）。
ALLOW_ENUM_VALUES = frozenset({"png", "jpg", "jpeg"})

RE_TYPE = re.compile(
    r"\b(?:class|struct|enum(?:\s+class)?)\s+"
    r"(?:[A-Z][A-Z0-9_]*_API\s+)?"
    r"([A-Za-z_][A-Za-z0-9_]*)"
)
RE_BOOL = re.compile(r"\bbool\b\s*([&*]\s*)?([A-Za-z_][A-Za-z0-9_]*)")
RE_K = re.compile(r"\b(k[A-Z][A-Za-z0-9_]*)\b")
RE_TEXT = re.compile(r'TEXT\(\s*"([^"]*)"\s*\)')
RE_OUT_NAME = re.compile(r'Out\.Name\s*=\s*TEXT\(\s*"([^"]*)"\s*\)')
RE_PROP = re.compile(r'\.Prop\(\s*TEXT\(\s*"([^"]*)"\s*\)')
RE_PROP_BOOL = re.compile(
    r'\.Prop\(\s*TEXT\(\s*"([^"]*)"\s*\)\s*,\s*FNexusSchema::Bool\b'
)
RE_FIELD = re.compile(
    r"\b(?:Set|Get|TryGet|Has)(?:Bool|String|Number|Array|Object|Raw)?Field"
    r"(?:\s*<[^;\n]{0,80}>)?\s*\(\s*TEXT\(\s*\"([^\"]*)\"\s*\)"
)
RE_BOOL_FIELD = re.compile(
    r"\b(?:Set|Get|TryGet)BoolField"
    r"(?:\s*<[^;\n]{0,80}>)?\s*\(\s*TEXT\(\s*\"([^\"]*)\"\s*\)"
)
RE_ARGS = re.compile(
    r"\.(?:Str|Num|StrArr|EnumInt|Bool)\(\s*TEXT\(\s*\"([^\"]*)\"\s*\)"
)
RE_ARGS_BOOL = re.compile(r"\.Bool\(\s*TEXT\(\s*\"([^\"]*)\"\s*\)")
RE_REQUIRED = re.compile(r"\.Required\s*\(\s*\{([^}]*)\}", re.S)
RE_PASCAL = re.compile(r"(?:[A-Z]|b[A-Z])[A-Za-z0-9]*\Z")
RE_BOOL_NAME = re.compile(r"b[A-Z][A-Za-z0-9]*\Z")
SKIP_TYPE_NAMES = frozenset({
    "final", "public", "private", "protected", "override",
    "const", "volatile", "virtual", "typename",
})


def _blank(text: str, *, strings: bool) -> str:
    """注释换成空格，换行保留。strings=True 时字符串内容也抹掉。"""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            out.append("  ")
            i += 2
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if c == "/" and nxt == "*":
            out.append("  ")
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append("  ")
                i += 2
            continue
        if c == '"':
            out.append('"')
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\" and i + 1 < n:
                    out.append("  " if strings else text[i:i + 2])
                    i += 2
                    continue
                out.append("\n" if text[i] == "\n" else (" " if strings else text[i]))
                i += 1
            if i < n:
                out.append('"')
                i += 1
            continue
        if c == "'":
            out.append("'")
            i += 1
            while i < n and text[i] != "'":
                if text[i] == "\\" and i + 1 < n:
                    out.append("  " if strings else text[i:i + 2])
                    i += 2
                    continue
                out.append(" " if strings else text[i])
                i += 1
            if i < n:
                out.append("'")
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def _line(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def _is_ident(name: str) -> bool:
    return bool(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name)) or name in ALLOW_KEYS


def _is_slate(name: str) -> bool:
    return len(name) >= 2 and name[0] == "S" and name[1].isupper()


def _has_prefix(name: str) -> bool:
    return name[:1] in TYPE_PREFIXES or _is_slate(name)


def _matches_file(name: str, stem: str) -> bool:
    # Slate 文件名带 S（SButton.h / class SButton）。其余去掉一个前缀字母。
    if _is_slate(name):
        return stem == name
    return name[:1] in TYPE_PREFIXES and stem == name[1:]


def check_types(stem: str, code: str, *, header: bool) -> list[str]:
    names = [m.group(1) for m in RE_TYPE.finditer(code)]
    names = [n for n in names if n not in SKIP_TYPE_NAMES]
    msgs: list[str] = []
    for name in dict.fromkeys(names):
        if not _has_prefix(name):
            msgs.append(f"类型 {name} 缺少 U/A/F/E/I/T/S 前缀")
    if header and names and not any(_matches_file(n, stem) for n in names):
        prefixed = [n for n in names if _has_prefix(n)]
        if prefixed:
            shown = ", ".join(dict.fromkeys(names))
            msgs.append(f"文件名 {stem} 与类型不一致（{shown}）")
    return msgs


def check_bools(code: str) -> list[tuple[int, str]]:
    hits: list[tuple[int, str]] = []
    for m in RE_BOOL.finditer(code):
        name = m.group(2)
        rest = code[m.end():]
        if re.match(r"\s*(::|\(|<)", rest):
            continue
        if RE_BOOL_NAME.fullmatch(name):
            continue
        hits.append((_line(code, m.start()), f"bool {name}"))
    return hits


def check_k_prefix(code: str) -> list[tuple[int, str]]:
    return [(_line(code, m.start()), m.group(1)) for m in RE_K.finditer(code)]


def _want_pascal(name: str, *, mcp: bool) -> str | None:
    if not _is_ident(name):
        return None
    if name in ALLOW_KEYS or (mcp and name in MCP_WIRE_KEYS) or RE_PASCAL.fullmatch(name):
        return None
    return f"协议名 {name} 不是 PascalCase"


def _want_bool_field(name: str) -> str | None:
    if not _is_ident(name):
        return None
    if name in ALLOW_KEYS or RE_BOOL_NAME.fullmatch(name):
        return None
    return f"布尔字段 {name} 缺少 b 前缀"


def check_protocol(code: str, *, mcp: bool = False) -> list[tuple[int, str]]:
    hits: list[tuple[int, str]] = []

    def add(index: int, msg: str | None) -> None:
        if msg:
            hits.append((_line(code, index), msg))

    for m in RE_OUT_NAME.finditer(code):
        add(m.start(), _want_pascal(m.group(1), mcp=mcp))
    for m in RE_PROP.finditer(code):
        add(m.start(), _want_pascal(m.group(1), mcp=mcp))
    for m in RE_PROP_BOOL.finditer(code):
        add(m.start(), _want_bool_field(m.group(1)))
    for m in RE_FIELD.finditer(code):
        add(m.start(), _want_pascal(m.group(1), mcp=mcp))
    for m in RE_BOOL_FIELD.finditer(code):
        add(m.start(), _want_bool_field(m.group(1)))
    for m in RE_ARGS.finditer(code):
        add(m.start(), _want_pascal(m.group(1), mcp=mcp))
    for m in RE_ARGS_BOOL.finditer(code):
        add(m.start(), _want_bool_field(m.group(1)))
    for block in RE_REQUIRED.finditer(code):
        for m in RE_TEXT.finditer(block.group(1)):
            add(block.start() + m.start(), _want_pascal(m.group(1), mcp=mcp))

    key = "FNexusSchema::Enum("
    start = 0
    while True:
        found = code.find(key, start)
        if found < 0:
            break
        i = found + len(key)
        depth = 1
        k = i
        while k < len(code) and depth:
            if code[k] == "(":
                depth += 1
            elif code[k] == ")":
                depth -= 1
            k += 1
        body = code[i:k - 1]
        lits = list(RE_TEXT.finditer(body))
        for lit in lits[1:]:
            value = lit.group(1)
            if value in ALLOW_ENUM_VALUES or RE_PASCAL.fullmatch(value):
                continue
            if _is_ident(value):
                add(found + lit.start(), f"枚举值 {value} 不是 PascalCase")
        start = k
    return hits


def _rel(path: Path) -> str:
    try:
        return path.relative_to(PLUGIN_ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def iter_sources(roots: list[Path]) -> list[Path]:
    files: list[Path] = []
    for root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in {".h", ".cpp"}:
                continue
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            if path.name.endswith(".generated.h"):
                continue
            files.append(path)
    return sorted(files)


def audit(roots: list[Path]) -> list[str]:
    findings: list[str] = []
    for path in iter_sources(roots):
        raw = path.read_text(encoding="utf-8", errors="replace")
        code = _blank(raw, strings=False)
        plain = _blank(raw, strings=True)
        rel = _rel(path)
        for msg in check_types(path.stem, plain, header=path.suffix == ".h"):
            findings.append(f"[type] {rel}: {msg}")
        for line, msg in check_bools(plain):
            findings.append(f"[bool] {rel}:{line}: {msg}")
        for line, msg in check_k_prefix(plain):
            findings.append(f"[k] {rel}:{line}: {msg}")
        mcp = "/Server/" in f"/{rel}"
        for line, msg in check_protocol(code, mcp=mcp):
            findings.append(f"[protocol] {rel}:{line}: {msg}")
    findings.sort()
    return findings


def _self_check() -> None:
    header = _blank(
        "class FNexusFoo {};\nstruct FOther {};\n// class BadName {}\n",
        strings=True,
    )
    assert check_types("NexusFoo", header, header=True) == []
    mismatch = check_types("NexusMcpSchemaBuilder", "struct FNexusSchema {};", header=True)
    assert any("不一致" in m for m in mismatch)
    bare = check_types("Foo", "struct Score {};", header=True)
    assert any("缺少" in m for m in bare)
    assert check_types("NexusVersionCompat", "#define X 1\n", header=True) == []
    assert check_types(
        "SNexusPanel", "class SNexusPanel {};\nclass SWidget;", header=True,
    ) == []

    bools = _blank(
        "bool bOk = true;\nbool enabled = false;\nbool IsReady();\nbool FCls::Run();\n"
        "// bool hidden;\nconst char* S = \"bool hidden\";\n",
        strings=True,
    )
    bool_msgs = [m for _, m in check_bools(bools)]
    assert bool_msgs == ["bool enabled"], bool_msgs

    ks = [m for _, m in check_k_prefix("const int32 kMax = 1;\nint32 Key = 0;\n")]
    assert ks == ["kMax"], ks

    proto = "\n".join([
        'Out.Name = TEXT("GetAsset");',
        '.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("x")))',
        '.Prop(TEXT("bSave"), FNexusSchema::Bool(TEXT("x")))',
        '.Prop(TEXT("Save"), FNexusSchema::Bool(TEXT("x")))',
        'Item->SetStringField(TEXT("type"), TEXT("object"));',
        'Item->SetStringField(TEXT("name"), Name);',
        'Args.Bool(TEXT("enabled"));',
        'FNexusSchema::Enum(TEXT("Image format"), { TEXT("png"), TEXT("Jpg") }, TEXT("png"))',
        'FNexusSchema::Enum(TEXT("Action"), { TEXT("start") })',
    ])
    msgs = [m for _, m in check_protocol(proto)]
    assert "协议名 assetPath 不是 PascalCase" in msgs, msgs
    assert "布尔字段 Save 缺少 b 前缀" in msgs, msgs
    assert "协议名 name 不是 PascalCase" in msgs, msgs
    assert "布尔字段 enabled 缺少 b 前缀" in msgs, msgs
    assert "枚举值 start 不是 PascalCase" in msgs, msgs
    assert not any("type" in m or "png" in m or "GetAsset" in m or "bSave" in m for m in msgs), msgs
    wire = check_protocol('Params->TryGetStringField(TEXT("protocolVersion"), V);', mcp=True)
    assert wire == [], wire


def main() -> int:
    _self_check()
    roots = [Path(a) for a in sys.argv[1:]] or DEFAULT_ROOTS
    findings = audit(roots)
    for line in findings:
        print(line)
    print(f"[audit] {len(findings)} finding(s) (report only)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
