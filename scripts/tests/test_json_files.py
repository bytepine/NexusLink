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


def test_uplugin_filters_do_not_duplicate_renamed_fields():
    data = _load("NexusLink.uplugin")
    # UE5.8 拒绝同时声明新旧字段；UE4 仍需要旧字段。
    aliases = (
        ("TargetAllowList", "WhitelistTargets"),
        ("TargetDenyList", "BlacklistTargets"),
        ("TargetConfigurationAllowList", "WhitelistTargetConfigurations"),
        ("TargetConfigurationDenyList", "BlacklistTargetConfigurations"),
    )
    for entry in data["Modules"] + data["Plugins"]:
        for current, legacy in aliases:
            assert not (current in entry and legacy in entry), (entry["Name"], current, legacy)


def test_uplugin_editor_dependencies_remain_editor_only_on_ue4():
    plugins = {entry["Name"]: entry for entry in _load("NexusLink.uplugin")["Plugins"]}
    for name in ("EditorScriptingUtilities", "SequencerScripting"):
        assert plugins[name]["WhitelistTargets"] == ["Editor"], name


def test_uplugin_websocket_dependency_excludes_shipping_on_ue4():
    plugins = {entry["Name"]: entry for entry in _load("NexusLink.uplugin")["Plugins"]}
    assert plugins["WebSocketNetworking"]["BlacklistTargetConfigurations"] == ["Shipping"]


def test_proxyconfig_valid_json():
    data = _load("Resources/ProxyConfig.json")
    assert isinstance(data, dict)


def test_proxyconfig_preserves_client_wire_contract():
    data = _load("Resources/ProxyConfig.json")
    for key in ("protocolVersion", "minProxyVersion", "initializePrefix", "localToolNames", "connectionTools", "errorMessages"):
        assert key in data, f"proxy configuration missing client field: {key}"
    tools = {tool["name"]: tool["inputSchema"] for tool in data["connectionTools"]}
    assert set(tools) == set(data["localToolNames"])
    connect = tools["connect_unreal_instance"]
    assert "port" in connect["properties"] and "port" in connect["required"]


def test_changelog_has_unreleased():
    path = os.path.join(REPO_ROOT, "CHANGELOG.md")
    text = open(path, encoding="utf-8").read()
    assert "## [Unreleased]" in text, "CHANGELOG.md missing [Unreleased] section"


def test_version_is_semver():
    import re
    path = os.path.join(REPO_ROOT, "VERSION")
    v = open(path, encoding="utf-8").read().strip()
    assert re.fullmatch(r"\d+\.\d+\.\d+(\S*)?", v), f"Invalid VERSION: {v}"
