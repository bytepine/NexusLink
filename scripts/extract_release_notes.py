"""
从 CHANGELOG.md 提取指定版本的 Release notes（Markdown）。

用法:
    python scripts/extract_release_notes.py --version <X.Y.Z> [--output <file>]
"""

from __future__ import annotations

import argparse
import re
import sys

_HEADING_RE = re.compile(r"^##\s+\[([^\]]+)\](?:\s+-\s+[^\n]+)?\s*$", re.MULTILINE)


def extract_section(changelog_text: str, version: str) -> str:
    for m in _HEADING_RE.finditer(changelog_text):
        if m.group(1).strip() == version:
            start = m.end()
            nxt = _HEADING_RE.search(changelog_text, start)
            end = nxt.start() if nxt else len(changelog_text)
            body = changelog_text[start:end].strip()
            if not body:
                raise ValueError(f"CHANGELOG [{version}] 段落为空")
            return body
    raise ValueError(f"CHANGELOG 中未找到 [{version}] 段落")


def main() -> int:
    parser = argparse.ArgumentParser(description="提取 CHANGELOG Release notes")
    parser.add_argument("--version", required=True)
    parser.add_argument("--changelog", default="CHANGELOG.md")
    parser.add_argument("--output", default="-", help="输出文件，默认 stdout")
    args = parser.parse_args()

    try:
        with open(args.changelog, encoding="utf-8") as f:
            text = f.read()
        notes = extract_section(text, args.version)
    except (OSError, ValueError) as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        return 1

    if args.output == "-":
        sys.stdout.write(notes)
        if not notes.endswith("\n"):
            sys.stdout.write("\n")
    else:
        with open(args.output, "w", encoding="utf-8", newline="\n") as f:
            f.write(notes)
            if not notes.endswith("\n"):
                f.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
