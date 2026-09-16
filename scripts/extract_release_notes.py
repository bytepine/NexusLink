"""
从 CHANGELOG.md 提取指定版本的 Release notes（Markdown）。

用法:
    python scripts/extract_release_notes.py --version <X.Y.Z> [--output <file>] [--verify]
    python scripts/extract_release_notes.py --version <X.Y.Z-beta.N> --verify

--verify  发版前门禁：VERSION 与 --version 一致，且 CHANGELOG [版本号] 段落非空。
          支持 Release（X.Y.Z）与 Pre-release（X.Y.Z-beta.N）。正式版若存在同系列
          [X.Y.Z-beta.N]，还须覆盖那些 beta 出现过的 ### Added（用户可见新功能须进正式段）。
          不强制覆盖 beta 的 Fixed/Changed（本版本新功能上的 fix 可舍去）。
          CI release.yml 与 release-version skill 均须带此参数。
"""

from __future__ import annotations

import argparse
import re
import sys

_HEADING_RE = re.compile(r"^##\s+\[([^\]]+)\](?:\s+-\s+[^\n]+)?\s*$", re.MULTILINE)
_H3_RE = re.compile(r"^###\s+(.+)$", re.MULTILINE)


def is_prerelease_version(version: str) -> bool:
    """版本号含 -beta 时为 GitHub Pre-release。"""
    return "-beta" in version


def read_version_file(path: str = "VERSION") -> str:
    with open(path, encoding="utf-8") as f:
        return f.read().strip()


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


def collect_h3(body: str) -> set[str]:
    return {m.group(1).strip() for m in _H3_RE.finditer(body)}


def collect_beta_h3s(changelog_text: str, release_version: str) -> set[str]:
    """同 Base 的 [X.Y.Z-beta.N] 小节标题，供正式版汇总门禁。"""
    found: set[str] = set()
    prefix = f"{release_version}-beta."
    for m in _HEADING_RE.finditer(changelog_text):
        name = m.group(1).strip()
        if not name.startswith(prefix):
            continue
        start = m.end()
        nxt = _HEADING_RE.search(changelog_text, start)
        end = nxt.start() if nxt else len(changelog_text)
        found |= collect_h3(changelog_text[start:end])
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description="提取 CHANGELOG Release notes")
    parser.add_argument("--version", required=True)
    parser.add_argument("--changelog", default="CHANGELOG.md")
    parser.add_argument("--version-file", default="VERSION")
    parser.add_argument("--output", default="-", help="输出文件，默认 stdout")
    parser.add_argument(
        "--verify",
        action="store_true",
        help="校验 VERSION 与 CHANGELOG 段落（发版门禁，CI 必带）",
    )
    args = parser.parse_args()

    try:
        if args.verify:
            file_version = read_version_file(args.version_file)
            if file_version != args.version:
                raise ValueError(
                    f"VERSION ({file_version}) != --version ({args.version})"
                )

        with open(args.changelog, encoding="utf-8") as f:
            text = f.read()
        notes = extract_section(text, args.version)
        if args.verify and not is_prerelease_version(args.version):
            missing_added = (
                "Added" in collect_beta_h3s(text, args.version)
                and "Added" not in collect_h3(notes)
            )
            if missing_added:
                raise ValueError(
                    f"正式版 CHANGELOG [{args.version}] 须汇总同系列 beta 的 Added"
                )
    except (OSError, ValueError) as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        return 1

    if args.output != "-":
        with open(args.output, "w", encoding="utf-8", newline="\n") as f:
            f.write(notes)
            if not notes.endswith("\n"):
                f.write("\n")

    if args.output == "-":
        sys.stdout.write(notes)
        if not notes.endswith("\n"):
            sys.stdout.write("\n")

    if args.verify:
        channel = "Pre-release" if is_prerelease_version(args.version) else "Release"
        print(
            f"[OK] GitHub {channel} 将使用 CHANGELOG [{args.version}]（{len(notes)} 字符）",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
