"""二进制依赖检查须同时拒绝常规与延迟导入，并对无法解析的输出报错。"""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audit_binary_dependencies import forbidden_imports, main, parse_imports


@pytest.mark.parametrize("newline", ["\n", "\r\n"])
def test_regular_and_delay_imports_are_both_checked(newline):
    output = """Dump of file C:\\ToolsetAudit\\UnrealEditor-NexusLink.dll
  Image has the following dependencies:
    UnrealEditor-Core.dll
    UnrealEditor-Engine.dll
  Image has the following delay load dependencies:
    UnrealEditor-ToolsetRegistry.dll
  Summary
"""
    imports = parse_imports(output.replace("\n", newline))
    assert imports == ["UnrealEditor-Core.dll", "UnrealEditor-Engine.dll", "UnrealEditor-ToolsetRegistry.dll"]
    assert forbidden_imports(imports) == ["UnrealEditor-ToolsetRegistry.dll"]


@pytest.mark.parametrize("name", [
    "UnrealEditor-ToolsetCore.dll", "UE4Editor-AllToolsets.dll", "UnrealEditor-PCGToolset.dll",
    "UNREALEDITOR-TOOLSETREGISTRYEDITOR.DLL",
])
def test_official_adapters_and_core_are_rejected(name):
    assert forbidden_imports([name]) == [name]


def test_engine_domain_imports_are_not_recursively_rejected():
    assert not forbidden_imports(["UnrealEditor-UnrealEd.dll", "UnrealEditor-UMGEditor.dll",
                                  "UnrealEditor-PCG.dll", r"C:\ToolsetAudit\UnrealEditor-Core.dll"])


@pytest.mark.parametrize("output", ["", "LINK : fatal error LNK1107: invalid or corrupt file", "  Summary\n  1000 .text"])
def test_missing_import_table_fails_closed(output):
    with pytest.raises(ValueError, match="no DLL import rows"):
        parse_imports(output)


def test_missing_tool_fails_instead_of_skipping():
    assert main(["--dumpbin", "", "UnrealEditor-NexusLink.dll"]) == 1
