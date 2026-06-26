"""
extract_release_notes.py 单元测试
"""
import sys
import os
import textwrap
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from extract_release_notes import extract_section  # noqa: E402

SAMPLE = textwrap.dedent("""\
    # Changelog

    ## [Unreleased]

    ## [2.0.0] - 2026-06-01

    - feat: new feature A
    - fix: bug fix B

    ## [1.0.0] - 2026-01-01

    - initial release
""")


def test_extract_existing_version():
    notes = extract_section(SAMPLE, "2.0.0")
    assert "feat: new feature A" in notes
    assert "fix: bug fix B" in notes


def test_extract_stops_before_next_version():
    notes = extract_section(SAMPLE, "2.0.0")
    assert "initial release" not in notes


def test_extract_older_version():
    notes = extract_section(SAMPLE, "1.0.0")
    assert "initial release" in notes


def test_missing_version_raises():
    with pytest.raises(ValueError, match="未找到"):
        extract_section(SAMPLE, "9.9.9")


def test_empty_section_raises():
    changelog = textwrap.dedent("""\
        ## [Unreleased]

        ## [1.0.0] - 2026-01-01

        ## [0.9.0] - 2025-01-01

        - old stuff
    """)
    with pytest.raises(ValueError, match="段落为空"):
        extract_section(changelog, "1.0.0")


def test_unreleased_section_empty_raises():
    with pytest.raises(ValueError, match="段落为空"):
        extract_section(SAMPLE, "Unreleased")


def test_version_with_prerelease_suffix():
    changelog = textwrap.dedent("""\
        ## [Unreleased]

        ## [1.0.0-beta] - 2026-01-01

        - beta feature
    """)
    notes = extract_section(changelog, "1.0.0-beta")
    assert "beta feature" in notes


def test_multiline_entry():
    changelog = textwrap.dedent("""\
        ## [Unreleased]

        ## [1.0.0] - 2026-01-01

        - fix: multi-word description with colons: value
        - feat: another entry
    """)
    notes = extract_section(changelog, "1.0.0")
    assert "multi-word description with colons: value" in notes
    assert "another entry" in notes
