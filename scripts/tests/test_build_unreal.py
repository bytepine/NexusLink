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
    build_zip,
)

_POINTER = (
    b"version https://git-lfs.github.com/spec/v1\n"
    b"oid sha256:afabb656d57dbd8ae80930344f6a645f16460a02c20e0043c833726a65fa83b7\n"
    b"size 5091\n"
)


def test_zip_includes_replay_tool_without_python_caches(tmp_path):
    import zipfile
    plugin = tmp_path / "plugin"
    tools = plugin / "tools" / "gas_retest"
    tools.mkdir(parents=True)
    (plugin / "NexusLink.uplugin").write_text('{"VersionName":"0.0.0"}', encoding="utf-8")
    (tools / "runner.py").write_text("pass\n", encoding="utf-8")
    (tools / "__pycache__").mkdir()
    (tools / "__pycache__" / "runner.pyc").write_bytes(b"cache")
    reports = plugin / "tools" / "gas-retest-results"
    reports.mkdir()
    (reports / "results.json").write_text('{}', encoding="utf-8")
    archive = build_zip(str(plugin), "0.0.0", str(tmp_path / "out"))
    with zipfile.ZipFile(archive) as result:
        assert "NexusLink/tools/gas_retest/runner.py" in result.namelist()
        assert not any("__pycache__" in name or name.endswith(".pyc") for name in result.namelist())
        assert not any("gas-retest-results" in name for name in result.namelist())


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


def test_patch_uplugin_sets_version_and_engine(tmp_path):
    uplugin = tmp_path / "NexusLink.uplugin"
    uplugin.write_text(
        '{"VersionName":"0.0.0","Modules":[{"Name":"NexusLink","Type":"UncookedOnly"}]}',
        encoding="utf-8",
    )
    patch_uplugin(str(uplugin), "1.17.0", engine_version="5.8")
    data = json.loads(uplugin.read_text(encoding="utf-8"))
    assert data["VersionName"] == "1.17.0"
    assert data["EngineVersion"] == "5.8"
    assert [m.get("Name") for m in data["Modules"]] == ["NexusLink"]


def test_patch_uplugin_beta_flag_follows_version(tmp_path):
    uplugin = tmp_path / "NexusLink.uplugin"
    body = '{"VersionName":"0.0.0","IsBetaVersion":true,"Modules":[]}'

    uplugin.write_text(body, encoding="utf-8")
    patch_uplugin(str(uplugin), "2.0.0")
    assert json.loads(uplugin.read_text(encoding="utf-8"))["IsBetaVersion"] is False

    uplugin.write_text(body, encoding="utf-8")
    patch_uplugin(str(uplugin), "2.1.0-beta.1")
    assert json.loads(uplugin.read_text(encoding="utf-8"))["IsBetaVersion"] is True
