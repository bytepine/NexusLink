#!/usr/bin/env python3
# Copyright byteyang. All Rights Reserved.
"""检查指定 Windows 插件 DLL 的直接导入；不递归检查官方引擎内部依赖。"""
from __future__ import annotations

import argparse
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import subprocess
import sys


def parse_imports(output: str) -> list[str]:
    # /DEPENDENTS 同时列出常规导入和延迟导入；只取缩进的 DLL 行，排除被检查文件标题。
    imports = sorted(set(re.findall(r"^[ \t]{4,}([^\r\n]+?\.dll)[ \t\r]*$", output, re.MULTILINE | re.IGNORECASE)))
    if not imports:
        raise ValueError("dumpbin output contains no DLL import rows")
    return imports


def forbidden_imports(imports: list[str]) -> list[str]:
    return [name for name in imports if "toolset" in PureWindowsPath(name).name.casefold()]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binaries", nargs="+", type=Path, help="NexusLink/NexusLinkEditor DLL paths")
    parser.add_argument("--dumpbin", default=shutil.which("dumpbin"), help="MSVC dumpbin.exe path (or put it on PATH)")
    args = parser.parse_args(argv)
    if not args.dumpbin:
        print("FAIL: dumpbin is unavailable; pass --dumpbin or use a VS Native Tools prompt", file=sys.stderr)
        return 1
    failed = False
    for binary in args.binaries:
        try:
            if not binary.is_file():
                raise ValueError("DLL file does not exist")
            result = subprocess.run([args.dumpbin, "/DEPENDENTS", str(binary)], check=True, capture_output=True,
                                    text=True, encoding="utf-8", errors="replace", env={**os.environ, "VSLANG": "1033"})
            imports = parse_imports(result.stdout)
            forbidden = forbidden_imports(imports)
            if forbidden:
                print(f"FAIL: {binary}: forbidden direct imports: {', '.join(forbidden)}", file=sys.stderr)
                failed = True
            else:
                print(f"PASS: {binary}: {len(imports)} direct DLL imports; 0 Toolset imports")
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            print(f"FAIL: {binary}: {error}", file=sys.stderr)
            failed = True
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
