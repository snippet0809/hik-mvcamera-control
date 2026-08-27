"""unittest：统一包 import + 读码器/相机加载诊断 + 无设备时 enum 冒烟（按环境跳过）。"""

from __future__ import annotations

import os
import sys
import unittest

# 允许从仓库直接 `python tests/test_package.py`（未 pip install 时）。
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


@unittest.skipUnless(sys.platform == "win32", "Windows only")
class TestWindowsDiagnostics(unittest.TestCase):
    def test_import_exports(self) -> None:
        import hik_mvcamera_control as m

        for name in ("HikCamera", "HikCodeReader", "CameraOpenParams", "OpenParams", "CameraDeviceInfo"):
            self.assertTrue(hasattr(m, name), name)

    def test_reader_diagnose_context_has_keys(self) -> None:
        from hik_mvcamera_control import diagnose_runtime_search_context

        ctx = diagnose_runtime_search_context()
        self.assertIn("bundled_exists", ctx)
        self.assertIn("runtime_dirs", ctx)

    def test_camera_diagnose_context_has_keys(self) -> None:
        from hik_mvcamera_control import diagnose_camera_runtime_context

        ctx = diagnose_camera_runtime_context()
        self.assertIn("bundled_exists", ctx)
        self.assertIn("runtime_dirs", ctx)
        # 相机 DLL 应从 _native 定位（本包内置）
        self.assertTrue(ctx["bundled_dll"].endswith("hik_mvcamera.dll"))

    def test_reader_diagnose_load_returns_rows(self) -> None:
        from hik_mvcamera_control import diagnose_windows_native_load

        rows = diagnose_windows_native_load()
        self.assertGreaterEqual(len(rows), 1)
        for r in rows:
            self.assertIn("strategy", r)
            self.assertIn("ok", r)

    def test_camera_diagnose_load_returns_rows(self) -> None:
        from hik_mvcamera_control import diagnose_camera_windows_native_load

        rows = diagnose_camera_windows_native_load()
        self.assertGreaterEqual(len(rows), 1)
        for r in rows:
            self.assertIn("strategy", r)
            self.assertIn("ok", r)


@unittest.skipUnless(sys.platform == "win32", "Windows only")
class TestHikCameraApi(unittest.TestCase):
    def test_enum_devices_when_runtime_present(self) -> None:
        from hik_mvcamera_control import HikCamera, diagnose_camera_windows_native_load

        if not any(r["ok"] for r in diagnose_camera_windows_native_load()):
            self.skipTest("hik_mvcamera.dll 未加载，跳过 API（本机未装海康 Runtime 时正常）")
        cam = HikCamera()
        devs = cam.enum_devices()
        self.assertIsInstance(devs, list)
        for d in devs:
            self.assertIsInstance(d.serial_number, str)
            self.assertIsInstance(d.model_name, str)


@unittest.skipUnless(sys.platform == "win32", "Windows only")
class TestHikCodeReaderApi(unittest.TestCase):
    def test_enum_devices_when_runtime_present(self) -> None:
        from hik_mvcamera_control import HikCodeReader, diagnose_windows_native_load

        if not any(r["ok"] for r in diagnose_windows_native_load()):
            self.skipTest("native DLL 未加载，跳过 API（本机未装海康 Runtime 时正常）")
        cr = HikCodeReader()
        devs = cr.enum_devices()
        self.assertIsInstance(devs, list)


if __name__ == "__main__":
    unittest.main()
