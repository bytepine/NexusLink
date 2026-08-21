#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""translate_param_text 回归：部分短语命中后仍走 glossary；标识符不拆。"""
from __future__ import annotations

import unittest

from tool_reference_i18n import has_cjk, residual_english, translate_param_text


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


if __name__ == "__main__":
    unittest.main()
