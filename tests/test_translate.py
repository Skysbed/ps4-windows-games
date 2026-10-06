#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from pcps4_translate import find_blockers, translate


class TranslationTests(unittest.TestCase):
    def test_supported_windows_surface(self):
        source = """
#include <windows.h>
const char *text = "Sleep(GetTickCount64())";
DWORD started = GetTickCount64();
Sleep(1);
BOOL ok = TRUE;
InterlockedIncrement(&counter);
"""
        translated, report, status = translate(source, strict=True)

        self.assertEqual(status, 0)
        self.assertIn('#include "pcps4_compat.h"', translated)
        self.assertIn('uint32_t started = pcps4_monotonic_ms();', translated)
        self.assertIn('pcps4_sleep_ms(1);', translated)
        self.assertIn('int ok = 1;', translated)
        self.assertIn('pcps4_interlocked_increment(&counter);', translated)
        self.assertEqual(report["blockers"], [])

    def test_comments_and_strings_are_not_rewritten(self):
        source = """
// Sleep(GetTickCount64()); DWORD fake;
const char *text = "Sleep(GetTickCount64()) DWORD";
"""
        translated, _, _ = translate(source)
        self.assertIn('// Sleep(GetTickCount64()); DWORD fake;', translated)
        self.assertIn('"Sleep(GetTickCount64()) DWORD"', translated)

    def test_unsupported_api_is_reported(self):
        source = """
#include <d3d11.h>
void *x = (void *)Direct3DCreate9(0);
"""
        blockers = find_blockers(source)
        self.assertEqual(len(blockers), 2)
        self.assertEqual(blockers[0].name, "DirectX header")


if __name__ == "__main__":
    unittest.main()
