#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""translate_param_text 回归：部分短语命中后仍走 glossary；标识符不拆。"""
from __future__ import annotations

import unittest

from tool_reference_i18n import DocLocale, has_cjk, residual_english, translate_param_text


class TranslateParamTextTests(unittest.TestCase):
    def test_partial_phrase_continues_glossary(self) -> None:
        out = translate_param_text("Batch ops (at least one item)")
        self.assertIn("批量操作", out)
        self.assertIn("至少一项", out)
        self.assertNotIn("at least one", out.lower())

    def test_existing_cjk_untouched(self) -> None:
        src = "资产路径（package path）"
        self.assertEqual(translate_param_text(src), src)

    def test_identifier_not_split(self) -> None:
        out = translate_param_text("assetPath of the package")
        self.assertIn("assetPath", out)

    def test_keep_path(self) -> None:
        out = translate_param_text("Feature path prefix (avoid bare /Game/ on large projects)")
        self.assertIn("/Game/", out)
        self.assertTrue(has_cjk(out))

    def test_residual_english_detects_hybrids(self) -> None:
        self.assertTrue(residual_english("批量操作 (至少一项 item)"))
        self.assertFalse(residual_english("批量操作（至少一项）"))


class ParamDescTests(unittest.TestCase):
    """param_text（按英文原文）与 param_name（按参数名）的优先级。"""

    LUA_ZH = "Lua 脚本路径（相对 Content/Script/）"

    def _locale(self, param_by_en: dict[str, str], param_by_name: dict[str, str]) -> DocLocale:
        return DocLocale(
            code="zh", header_name="", output_name="", labels={}, categories={},
            descriptions={}, when_to_use={},
            param_by_en=param_by_en, param_by_name=param_by_name,
        )

    def test_exact_overlay_wins_over_name_fallback(self) -> None:
        """同名参数在不同 cap 里语义可能完全不同（scriptPath 在 Lua 指 Content/Script/，
        在 Python 指 Content/Python/），精确命中不得被按名兜底覆盖。"""
        en = "Python file path relative to Content/Python/; required in file mode"
        locale = self._locale({en: "相对 Content/Python/ 的 .py 路径；file 模式必填"},
                              {"scriptPath": self.LUA_ZH})
        out = locale.param_desc("scriptPath", en, {})
        self.assertIn("Content/Python/", out)
        self.assertNotIn("Lua", out)

    def test_name_fallback_still_applies_without_exact(self) -> None:
        """没有精确译文且启发式译不动时，仍要回落到按名兜底。"""
        locale = self._locale({}, {"scriptPath": self.LUA_ZH})
        out = locale.param_desc("scriptPath", "Arbitrary opaque payload blob", {})
        self.assertEqual(out, self.LUA_ZH)


if __name__ == "__main__":
    unittest.main()
