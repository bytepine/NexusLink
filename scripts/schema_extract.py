#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""从 C++ FNexusSchema::Object()…Build() 链提取文本（build_tool_reference / audit 共用）。"""
from __future__ import annotations


def extract_object_chain_after(text: str, start: int) -> str:
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
