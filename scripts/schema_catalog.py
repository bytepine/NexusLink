#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""注册表导出的 InputSchema 清单（scripts/generated/capability_schemas.json）。"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

_GENERATED = Path(__file__).resolve().parent / "generated" / "capability_schemas.json"


def catalog_path() -> Path:
    return _GENERATED


def load_capability_schemas() -> dict[str, Any] | None:
    if not _GENERATED.is_file():
        return None
    data = json.loads(_GENERATED.read_text(encoding="utf-8"))
    return data if isinstance(data, dict) else None


def schema_top_prop_names(schema: dict[str, Any]) -> set[str]:
    props = schema.get("properties")
    if not isinstance(props, dict):
        return set()
    return set(props.keys())


def params_from_input_schema(schema: dict[str, Any]) -> tuple[list[dict[str, Any]], set[str]]:
    """JSON Schema → tool-reference params 列表。"""
    props = schema.get("properties") or {}
    required = set(schema.get("required") or [])
    params: list[dict[str, Any]] = []
    if not isinstance(props, dict):
        return params, required
    for name, spec in props.items():
        if not isinstance(spec, dict):
            continue
        if name == "sections":
            continue
        p: dict[str, Any] = {
            "name": name,
            "type": spec.get("type") or "string",
            "description": spec.get("description") or "",
        }
        if spec.get("enum"):
            p["enum"] = spec["enum"]
            p["type"] = "string (enum)"
        if spec.get("type") == "array":
            items = spec.get("items") or {}
            if isinstance(items, dict):
                if items.get("type") == "string":
                    p["type"] = "string[]"
                    if items.get("enum"):
                        p["enum"] = items["enum"]
                elif items.get("type") == "object":
                    p["type"] = "object[]"
                    nested = items.get("properties") or {}
                    p["items"] = []
                    if isinstance(nested, dict):
                        for nk, nv in nested.items():
                            bit: dict[str, Any] = {"name": nk}
                            if isinstance(nv, dict) and nv.get("enum"):
                                bit["enum"] = nv["enum"]
                            p["items"].append(bit)
        params.append(p)
    return params, required
