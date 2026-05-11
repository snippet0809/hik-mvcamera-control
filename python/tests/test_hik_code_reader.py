"""unittest：加载诊断 + API（无设备时 enum 仍应可调用或按环境跳过）。"""

from __future__ import annotations

import os
import sys
import unittest


@unittest.skipUnless(sys.platform == "win32", "Windows only")
class TestWindowsDiagnostics(unittest.TestCase):
    def test_diagnose_runtime_context_has_keys(self) -> None:
        from hik_code_reader import diagnose_runtime_search_context

        ctx = diagnose_runtime_search_context()
        self.assertIn("bundled_exists", ctx)
        self.assertIn("mvcode_runtime_dirs", ctx)

    def test_diagnose_load_returns_rows(self) -> None:
        from hik_code_reader import diagnose_windows_native_load

        rows = diagnose_windows_native_load()
        self.assertGreaterEqual(len(rows), 1)
        for r in rows:
            self.assertIn("strategy", r)
            self.assertIn("ok", r)


@unittest.skipUnless(sys.platform == "win32", "Windows only")
class TestHikCodeReaderApi(unittest.TestCase):
    def test_enum_devices_when_runtime_present(self) -> None:
        from hik_code_reader import HikCodeReader, diagnose_windows_native_load

        if not any(r["ok"] for r in diagnose_windows_native_load()):
            self.skipTest("native DLL 未加载，跳过 API（本机未装海康 Runtime 时正常）")
        cr = HikCodeReader()
        devs = cr.enum_devices()
        self.assertIsInstance(devs, list)


if __name__ == "__main__":
    unittest.main()
