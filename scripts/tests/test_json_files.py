"""
仓库内 JSON 文件完整性测试
"""
import json
import os

REPO_ROOT = os.path.join(os.path.dirname(__file__), "..", "..")


def _load(rel: str) -> dict:
    with open(os.path.join(REPO_ROOT, rel), encoding="utf-8") as f:
        return json.load(f)


def test_uplugin_valid_json():
    data = _load("NexusLink.uplugin")
    assert isinstance(data, dict)


def test_uplugin_required_fields():
    data = _load("NexusLink.uplugin")
    for field in ("FileVersion", "FriendlyName", "Modules"):
        assert field in data, f"NexusLink.uplugin missing field: {field}"


def test_uplugin_modules_not_empty():
    data = _load("NexusLink.uplugin")
    assert len(data["Modules"]) > 0


def test_proxyconfig_valid_json():
    data = _load("Resources/ProxyConfig.json")
    assert isinstance(data, dict)


def test_changelog_has_unreleased():
    path = os.path.join(REPO_ROOT, "CHANGELOG.md")
    text = open(path, encoding="utf-8").read()
    assert "## [Unreleased]" in text, "CHANGELOG.md missing [Unreleased] section"


def test_version_is_semver():
    import re
    path = os.path.join(REPO_ROOT, "VERSION")
    v = open(path, encoding="utf-8").read().strip()
    assert re.fullmatch(r"\d+\.\d+\.\d+(\S*)?", v), f"Invalid VERSION: {v}"
