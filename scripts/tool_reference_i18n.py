#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""tool-reference 中英文档的文案包与参数描述翻译。

中文手册与英文手册由同一趟 `build_tool_reference.py` 写出。
Capability 中文 Description / WhenToUse 存在 `tool_reference_zh.json`；
参数说明先查精确表，再走短语替换，保证新 cap 的常见英文句式也能落到中文。
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

_DIR = Path(__file__).resolve().parent
_ZH_JSON = _DIR / "tool_reference_zh.json"


def has_cjk(s: str) -> bool:
    return any("\u4e00" <= c <= "\u9fff" for c in s)


# ── 界面标签 ──────────────────────────────────────────────────────────────────

LABELS_EN: dict[str, str] = {
    "contents": "Contents",
    "parameter": "Parameter",
    "type": "Type",
    "required": "Required",
    "description": "Description",
    "prerequisites": "Prerequisites",
    "when_to_use": "When to use",
    "related": "Related capabilities",
    "sections": "Sections (multi-select)",
    "enum": "enum",
    "item": "item",
}

LABELS_ZH: dict[str, str] = {
    "contents": "目录",
    "parameter": "参数",
    "type": "类型",
    "required": "必填",
    "description": "说明",
    "prerequisites": "前置条件",
    "when_to_use": "适用场景",
    "related": "相关 Capability",
    "sections": "查询段（可多选）",
    "enum": "枚举",
    "item": "条目",
}

# 内部分类名（DIR_CATEGORY 值）→ 中文展示名。英文文档保持原名。
CATEGORY_ZH: dict[str, str] = {
    "Meta tools": "元工具",
    "Editor tools": "编辑器工具",
    "General asset tools": "通用资产工具",
    "Blueprint tools": "蓝图工具",
    "Animation assets": "动画资产",
    "Material tools": "材质工具",
    "Struct tools": "结构体工具",
    "Data assets (DataAsset / DataTable)": "数据资产（DataAsset / DataTable）",
    "Widget blueprint tools": "控件蓝图工具",
    "Lua runtime tools": "Lua 运行时工具",
    "Runtime tools": "运行时工具",
    "AI tools": "AI 工具",
}

# ── 参数说明：短语表（长的优先）──────────────────────────────────────────────
# 新 cap 的英文 schema 只要套这些句式，中文文档会自动跟上。

_PHRASES: list[tuple[str, str]] = [
    ("When true, do not auto-unload packages introduced by this call (single or batch); default false",
     "为 true 时不自动卸载本次调用引入的包（单条或批量）；默认 false"),
    ("Save the package to disk after success", "成功后将包保存到磁盘"),
    ("Compile blueprint if needed (BP/ABP/WBP only)", "需要时编译蓝图（仅 BP/ABP/WBP）"),
    ("Save package to disk after compile", "编译后将包保存到磁盘"),
    ("Batch: ordered list [{capability,arguments?},...]", "批量：有序列表 [{capability,arguments?},...]"),
    ("Nested arguments for a single call", "单次调用的嵌套参数"),
    ("Nested arguments object for this item", "本条的嵌套参数对象"),
    ("Capability name for a single call", "单次调用的 Capability 名称"),
    ("Exact capability name", "Capability 精确名称"),
    ("search_capabilities query that caused the issue", "引发问题的 search_capabilities 查询词"),
    ("Summary of arguments that triggered the issue", "触发问题的参数摘要"),
    ("Snippet of the actual error received", "实际收到的错误信息片段"),
    ("Missing, ambiguous, or guessed field name", "缺失、歧义或需猜测的字段名"),
    ("Free-text problem description (recommended)", "问题自由文本描述（建议填写）"),
    ("MCP tool name involved", "涉及的 MCP 工具名"),
    ("Capability name involved", "涉及的 Capability 名称"),
    ("Feedback category", "反馈分类"),
    ("Hex address from Widget Reflector", "Widget Reflector 提供的十六进制地址"),
    ("Token AND match; matches name/path/tags", "分词 AND 匹配；匹配名称/路径/标签"),
    ("Feature path prefix (avoid bare /Game/ on large projects)", "功能级路径前缀（大项目勿用裸 /Game/）"),
    ("Blueprint/Widget/Material/AnimSequence/… or UClass; avoid all on large projects",
     "Blueprint/Widget/Material/AnimSequence/… 或 UClass；大项目避免 all"),
    ("dependencies=deps; referencers=referencers; children=direct subclasses; descendants=all descendants; parent=direct parent; ancestors=parent chain",
     "dependencies=依赖；referencers=引用者；children=直接子类；descendants=全部子孙；parent=直接父类；ancestors=父类链"),
    ("auto: rows if rowNames non-empty else schema; schema ignores rowNames; rows requires rowNames",
     "auto：rowNames 非空则 rows 否则 schema；schema 忽略 rowNames；rows 要求 rowNames"),
    ("Diagnostic preset: diagnose=newest+verbosity≥warning+includeSummary+limit≤50",
     "诊断预设：diagnose=newest+verbosity≥warning+includeSummary+limit≤50"),
    ("Return logs with Sequence greater than this (incremental; pass last latestSequence)",
     "返回 Sequence 大于此值的日志（增量；传入上次 latestSequence）"),
    ("Attach summaryByCategory/summaryByVerbosity (full filtered set, not this page)",
     "附加 summaryByCategory/summaryByVerbosity（完整过滤集，非本页）"),
    ("Summary only; entries empty (still returns totalCount/latestSequence)",
     "仅摘要；entries 为空（仍返回 totalCount/latestSequence）"),
    ("Trigger KEEPFLAGS GC after unload (default true)", "卸载后触发 KEEPFLAGS GC（默认 true）"),
    ("If true skip dirty packages (default true, recommended)", "为 true 时跳过脏包（默认 true，推荐）"),
    ("If true skip image; validate target/viewport only", "为 true 时跳过截图，仅校验 target/视口"),
    ("When true, do not auto-unload packages introduced by this call", "为 true 时不自动卸载本次调用引入的包"),
    ("Parent class or BP path. Interface for BPI; Actor/Pawn/Character for normal BP",
     "父类或 BP 路径。Interface 表示 BPI；Actor/Pawn/Character 为普通 BP"),
    ("New value formats; other structs via ImportText", "新值格式；其余结构体走 ImportText"),
    ("Text value, ImportText format (set_property)", "文本值，ImportText 格式（set_property）"),
    ("String value e.g. (X=100,Y=0,Z=50) or true", "字符串值，如 (X=100,Y=0,Z=50) 或 true"),
    ("Batch ops (at least one)", "批量操作（至少一项）"),
    ("Batch property ops (at least one)", "批量属性操作（至少一项）"),
    ("Batch row ops (at least one)", "批量行操作（至少一项）"),
    ("Batch socket ops (at least one)", "批量 Socket 操作（至少一项）"),
    ("Batch edit ops (at least one)", "批量编辑操作（至少一项）"),
    ("Batch material ops", "批量材质操作"),
    ("Batch field ops", "批量字段操作"),
    ("Batch key ops", "批量键操作"),
    ("Batch widget ops", "批量 Widget 操作"),
    ("Batch update", "批量更新"),
    ("Operation list; each item requires action", "操作列表；每项必须有 action"),
    ("Operation list", "操作列表"),
    ("Operation type", "操作类型"),
    ("Edit operation", "编辑操作"),
    ("Write operation", "写操作"),
    ("Field operation", "字段操作"),
    ("Key operation", "键操作"),
    ("Row operation", "行操作"),
    ("Widget operation", "Widget 操作"),
    ("Socket operation", "Socket 操作"),
    ("Property operation", "属性操作"),
    ("Queue operation", "队列操作"),
    ("PIE operation", "PIE 操作"),
    ("Animation command", "动画命令"),
    ("Pagination offset (along order direction)", "分页偏移（沿排序方向）"),
    ("Pagination offset (default 0)", "分页偏移（默认 0）"),
    ("Pagination offset", "分页偏移"),
    ("Max widgets per page 1-500 (default 100)", "每页最大 Widget 数 1–500（默认 100）"),
    ("Max count 1-500 (default 100)", "最大条数 1–500（默认 100）"),
    ("Max return count", "最大返回条数"),
    ("Max returned keys", "最大返回键数"),
    ("Max entries to return", "最大返回条目数"),
    ("Max list items (selection section)", "最大列表项数（selection 段）"),
    ("Max concurrent instances (default 16)", "最大并发实例数（默认 16）"),
    ("Max concurrent instances (≥1)", "最大并发实例数（≥1）"),
    ("Max count", "最大条数"),
    ("Max edge pixels (0=native)", "最大边长像素（0=原生）"),
    ("Asset package path", "资产包路径"),
    ("Asset path (package path)", "资产路径（包路径）"),
    ("Single asset path", "单个资产路径"),
    ("Source asset path", "源资产路径"),
    ("Target full asset path (package + name)", "目标完整资产路径（包 + 名称）"),
    ("Target full asset path", "目标完整资产路径"),
    ("New asset full package path", "新资产完整包路径"),
    ("Property path (set_property)", "属性路径（set_property）"),
    ("New property value (set_property)", "新属性值（set_property）"),
    ("New property value string", "新属性值字符串"),
    ("New property value", "新属性值"),
    ("New value string (set only)", "新值字符串（仅 set）"),
    ("New value string", "新值字符串"),
    ("New value when action=set", "action=set 时的新值"),
    ("Key filter; /regex/, ^prefix, suffix$", "键名过滤；支持 /regex/、^前缀、后缀$"),
    ("Dot-separated paths (batch)", "点分路径（批量）"),
    ("Dot-separated paths; omit=all editable properties", "点分路径；省略=全部可编辑属性"),
    ("Dot-separated path", "点分路径"),
    ("Dot path target for set", "set 的点路径目标"),
    ("Lua dot-separated path", "Lua 点分路径"),
    ("Skip output capture", "跳过输出捕获"),
    ("Minimum verbosity level", "最低详细级别"),
    ("Log category substring (case insensitive)", "日志分类子串（不区分大小写）"),
    ("Text filter (OR); overrides textFilter", "文本过滤（OR）；覆盖 textFilter"),
    ("Owner UserWidget class/name filter (optional)", "Owner UserWidget 类/名过滤（可选）"),
    ("Actor class name substring match (optional)", "Actor 类名子串匹配（可选）"),
    ("Actor name or tag substring match (optional)", "Actor 名或标签子串匹配（可选）"),
    ("Actor Tag exact match (optional)", "Actor Tag 精确匹配（可选）"),
    ("Actor name/tag filter (optional)", "Actor 名/标签过滤（可选）"),
    ("Child Widget name filter", "子 Widget 名过滤"),
    ("Child Widget name", "子 Widget 名"),
    ("Runtime Actor name", "运行时 Actor 名"),
    ("Runtime Widget name", "运行时 Widget 名"),
    ("Widget paginationoffset (default 0)", "Widget 分页偏移（默认 0）"),
    ("actors section pagination offset", "actors 段分页偏移"),
    ("actors section page size", "actors 段每页条数"),
    ("referencers section pagination offset", "referencers 段分页偏移"),
    ("Skeleton list pagination offset", "Skeleton 列表分页偏移"),
    ("Skeleton list page size", "Skeleton 列表每页条数"),
]

# 较短词/词组，按长度降序替换；保留标识符与路径。
_GLOSSARY: list[tuple[str, str]] = [
    ("at least one", "至少一项"),
    ("asset path", "资产路径"),
    ("package path", "包路径"),
    ("Blueprint path", "蓝图路径"),
    ("Blueprint asset path", "蓝图资产路径"),
    ("full path", "完整路径"),
    ("Property path", "属性路径"),
    ("property path", "属性路径"),
    ("propertypath", "属性路径"),
    ("Socket name", "Socket 名"),
    ("socket name", "Socket 名"),
    ("display name", "显示名"),
    ("class name", "类名"),
    ("variable name", "变量名"),
    ("node GUID", "节点 GUID"),
    ("node id", "节点 id"),
    ("pin name", "引脚名"),
    ("slot name", "槽位名"),
    ("row name", "行名"),
    ("field name", "字段名"),
    ("key name", "键名"),
    ("file path", "文件路径"),
    ("frame rate", "帧率"),
    ("half-extent", "半长"),
    ("half-height", "半高"),
    ("substring match", "子串匹配"),
    ("substring", "子串"),
    ("case-insensitive", "不区分大小写"),
    ("case insensitive", "不区分大小写"),
    ("read-only", "只读"),
    ("required", "必填"),
    ("optional", "可选"),
    ("default", "默认"),
    ("Batch ops", "批量操作"),
    ("batch ops", "批量操作"),
    ("Batch edit", "批量编辑"),
    ("Operation", "操作"),
]


_PHRASES_SORTED = sorted(_PHRASES, key=lambda x: len(x[0]), reverse=True)
_GLOSSARY_SORTED = sorted(_GLOSSARY, key=lambda x: len(x[0]), reverse=True)

# 已是标识符/路径的片段不要再拆词
_KEEP = re.compile(
    r"`[^`]+`"
    r"|/[A-Za-z][\w/]*"
    r"|[A-Za-z][A-Za-z0-9]*(?:_[A-Za-z0-9]+)+"
    r"|[A-Za-z][A-Za-z0-9]+(?:[./][A-Za-z][A-Za-z0-9]+)+"
)

_REGEXES: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"^(.+?) asset path$", re.I), r"\1 资产路径"),
    (re.compile(r"^(.+?) package path$", re.I), r"\1 包路径"),
    (re.compile(r"^(.+?) Blueprint path$", re.I), r"\1 蓝图路径"),
    (re.compile(r"^(.+?) path$", re.I), r"\1 路径"),
    (re.compile(r"^(.+?) name$", re.I), r"\1 名"),
    (re.compile(r"^(.+?) index$", re.I), r"\1 索引"),
    (re.compile(r"^Enable (.+)$", re.I), r"启用\1"),
    (re.compile(r"^(.+?) \(optional\)$", re.I), r"\1（可选）"),
]


_EN_WORD = re.compile(r"[A-Za-z]{4,}")


def residual_english(text: str) -> bool:
    """翻译后仍残留较长英文词（标识符/路径除外）时视为未译完。"""
    held: list[str] = []

    def _hold(m: re.Match[str]) -> str:
        held.append(m.group(0))
        return " "

    stripped = _KEEP.sub(_hold, text)
    return bool(_EN_WORD.search(stripped))


def translate_param_text(text: str, exact: dict[str, str] | None = None) -> str:
    """英文参数说明 → 中文。精确表优先，其次短语，再次词组/句式；已含汉字则原样返回。"""
    src = (text or "").strip()
    if not src:
        return src
    if has_cjk(src):
        return src
    if exact and src in exact:
        return exact[src]

    def _swap(haystack: str, en: str, zh: str, *, ignore_case: bool) -> str:
        flags = re.IGNORECASE if ignore_case else 0
        if not ignore_case and en not in haystack:
            return haystack
        if re.search(r"[A-Za-z]$", en) and re.search(r"^[A-Za-z]", en):
            return re.sub(
                r"(?<![A-Za-z])" + re.escape(en) + r"(?![A-Za-z])",
                zh,
                haystack,
                flags=flags,
            )
        if ignore_case:
            return re.sub(re.escape(en), zh, haystack, flags=flags)
        return haystack.replace(en, zh)

    out = src
    for en, zh in _PHRASES_SORTED:
        out = _swap(out, en, zh, ignore_case=False)
    held: list[str] = []

    def _hold(m: re.Match[str]) -> str:
        held.append(m.group(0))
        return f"\x00{len(held) - 1}\x00"

    protected = _KEEP.sub(_hold, out)
    for en, zh in _GLOSSARY_SORTED:
        protected = _swap(protected, en, zh, ignore_case=True)
    restored = re.sub(r"\x00(\d+)\x00", lambda m: held[int(m.group(1))], protected)
    if restored == src:
        for pat, repl in _REGEXES:
            m = pat.fullmatch(src)
            if m:
                return m.expand(repl)
    return restored


@dataclass(frozen=True)
class DocLocale:
    code: str
    header_name: str
    output_name: str
    labels: dict[str, str]
    categories: dict[str, str]
    descriptions: dict[str, str]
    when_to_use: dict[str, str]
    param_by_en: dict[str, str]
    param_by_name: dict[str, str]
    warn_missing: bool = False

    def category(self, internal: str) -> str:
        return self.categories.get(internal, internal)

    def cap_description(self, name: str, fallback: str) -> str:
        return self.descriptions.get(name) or fallback

    def cap_when(self, name: str, fallback: str) -> str:
        return self.when_to_use.get(name) or fallback

    def param_desc(self, name: str, desc: str, common_en: dict[str, str]) -> str:
        text = (desc or "").strip()
        if not text:
            text = self.param_by_name.get(name) or common_en.get(name, "")
        if self.code == "en":
            return text
        if not text:
            return self.param_by_name.get(name, "")
        translated = translate_param_text(text, self.param_by_en)
        if name in self.param_by_name and not has_cjk(text):
            if translated == text or residual_english(translated):
                return self.param_by_name[name]
        return translated


def heading_anchor(title: str) -> str:
    """与历史英文 TOC 一致：小写，空白/中文括号/斜线变连字符（保留 ASCII 括号）。"""
    anchor = re.sub(r"[（）/\s]+", "-", title.lower()).strip("-")
    return re.sub(r"-+", "-", anchor)


def load_zh_overlay() -> dict[str, Any]:
    if not _ZH_JSON.is_file():
        return {"descriptions": {}, "when_to_use": {}, "param_text": {}, "param_name": {}}
    return json.loads(_ZH_JSON.read_text(encoding="utf-8"))


def build_zh_locale() -> DocLocale:
    overlay = load_zh_overlay()
    descriptions = dict(overlay.get("descriptions") or {})
    when_to_use = dict(overlay.get("when_to_use") or {})
    param_by_en = dict(overlay.get("param_text") or {})
    param_by_name = dict(overlay.get("param_name") or {})
    return DocLocale(
        code="zh",
        header_name="tool-reference.header.zh.md",
        output_name="tool-reference.zh.md",
        labels=LABELS_ZH,
        categories=CATEGORY_ZH,
        descriptions=descriptions,
        when_to_use=when_to_use,
        param_by_en=param_by_en,
        param_by_name=param_by_name,
        warn_missing=True,
    )


def build_en_locale(
    *,
    doc_descriptions: dict[str, str],
    doc_when: dict[str, str],
    common_params: dict[str, str],
) -> DocLocale:
    return DocLocale(
        code="en",
        header_name="tool-reference.header.md",
        output_name="tool-reference.md",
        labels=LABELS_EN,
        categories={},
        descriptions=dict(doc_descriptions),
        when_to_use=dict(doc_when),
        param_by_en={},
        param_by_name=dict(common_params),
        warn_missing=False,
    )
