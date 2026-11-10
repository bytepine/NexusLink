# Copyright byteyang. All Rights Reserved.
"""阻止构建、头文件和 Python 桥接重新依赖官方 Toolset。"""
import json
import re
from pathlib import Path

import pytest


PLUGIN = Path(__file__).resolve().parents[2]
# 兼容名称和自有 Nexus 类型可保留；其余 Toolset 标识同样涵盖头文件、反射路径及动态导入。
FORBIDDEN = re.compile(
    r'\b(?!(?:(?:[A-Z]?Nexus\w*)|GetProjectToolsets|Toolsets?|ToolsetName)\b)\w*Toolset\w*\b'
    r'|\b(?:UToolCallAsyncResultString|WITH_NEXUS_SLATE_REFS)\b',
    re.IGNORECASE,
)


def test_official_toolsets_are_not_linked_or_called():
    errors = []
    for root in (PLUGIN / "Source", PLUGIN / "Content"):
        for path in root.rglob("*"):
            if path.suffix.lower() not in {".cpp", ".h", ".hpp", ".inl", ".cc", ".cxx", ".cs", ".py"}:
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
                if FORBIDDEN.search(line):
                    errors.append(f"{path.relative_to(PLUGIN)}:{number}: {line.strip()}")
    descriptor = json.loads((PLUGIN / "NexusLink.uplugin").read_text(encoding="utf-8-sig"))
    errors.extend(f"Plugin dependency: {item['Name']}" for item in descriptor["Plugins"] if "toolset" in item["Name"].lower())
    assert not errors, "\n".join(errors)


@pytest.mark.parametrize("source", [
    '#include "ConfigSettingsToolset.h"',
    '#include <UMGToolSet/UMGToolSetLibrary.h>',
    'LoadObject<UClass>(nullptr, TEXT("/Script/PCGToolset.PCGToolsetLibrary"));',
    'unreal.get_editor_subsystem(unreal.ToolsetRegistry)',
    '__import__("semantic_search_toolset")',
    'importlib.import_module("pcg_toolset")',
    'from gameplay_tags_toolset import editor',
    'editor_toolset.run()',
    'PublicDependencyModuleNames.Add("PhysicsToolsets");',
    'FModuleManager::LoadModuleChecked<FToolsetRegistryModule>(Name);',
    'WITH_NEXUS_TOOLSETS',
    'UToolCallAsyncResultString',
])
def test_dependency_gate_rejects_direct_and_dynamic_references(source):
    assert FORBIDDEN.search(source), source


@pytest.mark.parametrize("source", [
    '#include "Capabilities/Editor/Toolsets/NexusGetProjectToolsetsCapability.h"',
    'FNexusGetProjectToolsetsCapability',
    'FNexusEditorToolsets',
    'Out.Name = TEXT("GetProjectToolsets");',
    'Args.Str(TEXT("ToolsetName"))',
    'return {TEXT("Toolsets"), TEXT("Tools")};',
    '// 不依赖官方 Toolset。',
    'FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");',
    'PluginEnabled:SemanticSearch',
])
def test_dependency_gate_preserves_independent_names_and_domain_apis(source):
    assert not FORBIDDEN.search(source), source
