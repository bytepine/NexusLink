"""
build_unreal.py — NexusLink UE 插件打包（独立仓）

用法:
    python scripts/build_unreal.py --version <版本号> [--output <输出目录>]

说明:
    1. 复制插件源码到临时目录（排除 docs/、scripts/、release/ 等仓级文件）
    2. 注入 NexusLink.uplugin VersionName
    3. 输出 release/nexus-mcp-unreal-<ver>.zip（zip 内顶层为 NexusLink/）
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import tempfile
import zipfile

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if sys.stderr.encoding and sys.stderr.encoding.lower() != "utf-8":
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

# 仓级目录（不打进 UE 插件 zip；Fab 提交亦应排除）
EXCLUDE_DIRS = {
    "binaries",
    "intermediate",
    "deriveddatacache",
    "saved",
    "build",
    "release",
    "docs",
    "scripts",
    ".github",
    ".pytest_cache",
    ".vs",
    ".idea",
    ".git",
    "content",
}

# 仓级根文件（不打进 zip）
EXCLUDE_ROOT_FILES = {
    "readme.md",
    "changelog.md",
    "version",
    "license",
    ".gitignore",
    ".gitattributes",
    ".mlc-config.json",
}

EXCLUDE_EXTS = {
    ".pdb", ".obj", ".lib", ".exp", ".dll", ".so", ".dylib",
    ".log", ".tmp", ".bak",
}


def repo_root() -> str:
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def patch_uplugin_version(uplugin_path: str, version: str) -> None:
    with open(uplugin_path, encoding="utf-8") as f:
        data = json.load(f)
    data["VersionName"] = version
    with open(uplugin_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent="\t", ensure_ascii=False)
        f.write("\n")


def should_exclude(rel_path: str, is_root_file: bool) -> bool:
    norm = rel_path.replace("\\", "/")
    parts = norm.split("/")
    if is_root_file and len(parts) == 1 and parts[0].lower() in EXCLUDE_ROOT_FILES:
        return True
    for part in parts[:-1]:
        if part.lower() in EXCLUDE_DIRS:
            return True
    _, ext = os.path.splitext(norm)
    return ext.lower() in EXCLUDE_EXTS


def build_zip(plugin_dir: str, version: str, output_dir: str) -> str:
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, f"nexus-mcp-unreal-{version}.zip")

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_plugin = os.path.join(tmpdir, "NexusLink")
        shutil.copytree(
            plugin_dir,
            tmp_plugin,
            ignore=shutil.ignore_patterns(
                "Binaries",
                "Intermediate",
                "release",
                "docs",
                "scripts",
                ".github",
                ".git",
                ".pytest_cache",
            ),
        )

        tmp_uplugin = os.path.join(tmp_plugin, "NexusLink.uplugin")
        if os.path.isfile(tmp_uplugin):
            patch_uplugin_version(tmp_uplugin, version)

        with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED) as zf:
            for root, dirs, files in os.walk(tmp_plugin):
                dirs[:] = [d for d in dirs if d.lower() not in EXCLUDE_DIRS]
                for filename in files:
                    abs_file = os.path.join(root, filename)
                    rel_in_plugin = os.path.relpath(abs_file, tmp_plugin)
                    rel_in_zip = os.path.join("NexusLink", rel_in_plugin)
                    is_root = "/" not in rel_in_plugin.replace("\\", "/")
                    if should_exclude(rel_in_plugin, is_root):
                        continue
                    zf.write(abs_file, rel_in_zip.replace("\\", "/"))

    return output_path


def main() -> int:
    parser = argparse.ArgumentParser(description="打包 NexusLink UE 插件")
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", default=None, help="默认 <repo>/release/")
    args = parser.parse_args()

    root = repo_root()
    output_dir = args.output or os.path.join(root, "release")

    if not os.path.isfile(os.path.join(root, "NexusLink.uplugin")):
        print(f"[ERROR] 非 NexusLink 仓根目录: {root}", file=sys.stderr)
        return 1

    print(f"[build] NexusLink v{args.version}")
    try:
        path = build_zip(root, args.version, output_dir)
    except Exception as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        return 1
    print(f"[OK] {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
