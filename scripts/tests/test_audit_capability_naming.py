# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""audit_capability_naming.py — 动词规则 + 活树门禁。"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from audit_capability_naming import (  # noqa: E402
    ALLOWED_FIRST_VERBS,
    EXPECTED_CAP_COUNT,
    _first_verb,
    _scan_capability_cpp_files,
    main,
)


def test_first_verb():
    assert _first_verb("get_asset_blueprint") == "get"
    assert _first_verb("search") == "search"
    assert _first_verb("manage_asset_level") == "manage"


def test_allowed_verbs_cover_common_prefixes():
    for verb in ("get", "manage", "create", "search", "spawn"):
        assert verb in ALLOWED_FIRST_VERBS


def test_scan_finds_both_modules():
    files = _scan_capability_cpp_files()
    assert len(files) >= EXPECTED_CAP_COUNT
    joined = "\n".join(p.as_posix() for p in files)
    assert "Source/NexusLink/" in joined
    assert "Source/NexusLinkEditor/" in joined


def test_live_tree_passes():
    assert main() == 0
