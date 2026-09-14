# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""audit_doc_sync.py — 插件仓路径下可跑通。"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from audit_doc_sync import load_caps, main  # noqa: E402


def test_load_caps_from_plugin_source():
    caps = load_caps()
    assert "search_asset" in caps
    assert caps["search_asset"]["file"].startswith("Source/")


def test_report_runs():
    assert main() == 0
