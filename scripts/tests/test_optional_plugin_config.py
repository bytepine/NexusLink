"""在 Windows 上执行生产 C# 解析方法，防止其他插件的启用值覆盖目标状态。"""
import json
from pathlib import Path
import re
import shutil
import subprocess

import pytest


def test_target_plugin_override_is_scoped_to_its_record(tmp_path):
    powershell = shutil.which("pwsh") or shutil.which("powershell")
    if not powershell:
        pytest.skip("C# Add-Type regression requires PowerShell")
    source = (Path(__file__).resolve().parents[2] / "Source/NexusLink/NexusLink.Build.cs").read_text(encoding="utf-8-sig")
    methods = re.search(r"public static bool JsonHasEnabledLiteral.*?(?=\n\s*public static void ApplyCustomEngineCompatDefines)", source, re.S)
    assert methods
    calls = []
    for name, plugins in (
        ("enabled", [{"Name": "Other", "Enabled": False}, {"Name": "GameFeatures", "Enabled": True}]),
        ("disabled", [{"Name": "Other", "Enabled": True}, {"Name": "GameFeatures", "Enabled": False}]),
        ("absent", [{"Name": "Other", "Enabled": True}]),
    ):
        folder = tmp_path / name
        folder.mkdir()
        (folder / "Fixture.uproject").write_text(json.dumps({"Plugins": plugins}, indent=2), encoding="utf-8")
        literal = str(folder).replace("'", "''")
        calls.append(f"[NexusPluginFixture]::IsPluginExplicitlyDisabledInUproject('{literal}', 'GameFeatures')")
    script = "Add-Type -TypeDefinition @'\npublic static class NexusPluginFixture {\n" + methods.group() + "\n}\n'@\n"
    script += "@(" + ";".join(calls) + ") | ConvertTo-Json -Compress"
    result = subprocess.run([powershell, "-NoProfile", "-NonInteractive", "-Command", script], capture_output=True, text=True, check=True)
    assert json.loads(result.stdout.strip()) == [False, True, False]
