"""build_unreal.py — LFS pointer 检测与打包门禁"""
from __future__ import annotations

import json
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from build_unreal import (  # noqa: E402
    assert_no_lfs_pointers,
    is_lfs_pointer,
    patch_uplugin,
    should_exclude,
)

_POINTER = (
    b"version https://git-lfs.github.com/spec/v1\n"
    b"oid sha256:afabb656d57dbd8ae80930344f6a645f16460a02c20e0043c833726a65fa83b7\n"
    b"size 5091\n"
)


def test_is_lfs_pointer_true(tmp_path):
    p = tmp_path / "Icon128.png"
    p.write_bytes(_POINTER)
    assert is_lfs_pointer(str(p)) is True


def test_is_lfs_pointer_false_for_real_bytes(tmp_path):
    p = tmp_path / "Icon128.png"
    p.write_bytes(b"\x89PNG\r\n\x1a\n" + b"\x00" * 100)
    assert is_lfs_pointer(str(p)) is False


def test_assert_no_lfs_pointers_raises(tmp_path):
    resources = tmp_path / "Resources"
    resources.mkdir()
    (resources / "Icon128.png").write_bytes(_POINTER)
    (tmp_path / "NexusLink.uplugin").write_text("{}", encoding="utf-8")
    with pytest.raises(RuntimeError, match="Git LFS pointer"):
        assert_no_lfs_pointers(str(tmp_path))


def test_assert_no_lfs_pointers_ok(tmp_path):
    resources = tmp_path / "Resources"
    resources.mkdir()
    (resources / "Icon128.png").write_bytes(b"\x89PNG\r\n\x1a\n" + b"\x00" * 100)
    assert_no_lfs_pointers(str(tmp_path))


def test_should_exclude_nexuslinktests():
    assert should_exclude("Source/NexusLinkTests/NexusLinkTests.Build.cs", False) is True
    assert should_exclude("Source/NexusLink/NexusLink.Build.cs", False) is False


def test_patch_uplugin_strips_tests_module(tmp_path):
    uplugin = tmp_path / "NexusLink.uplugin"
    uplugin.write_text(
        '{"VersionName":"0.0.0","Modules":['
        '{"Name":"NexusLink","Type":"Runtime"},'
        '{"Name":"NexusLinkTests","Type":"DeveloperTool"}'
        "]}",
        encoding="utf-8",
    )
    patch_uplugin(str(uplugin), "1.17.0", engine_version="5.8")
    data = json.loads(uplugin.read_text(encoding="utf-8"))
    assert data["VersionName"] == "1.17.0"
    assert data["EngineVersion"] == "5.8"
    names = [m.get("Name") for m in data["Modules"]]
    assert names == ["NexusLink"]
