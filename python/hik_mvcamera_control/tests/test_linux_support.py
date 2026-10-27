"""unittest：Linux 支持中**可离线验证**的部分。

覆盖三件事：
1. 库名映射（``hik_mvcamera.dll`` ↔ ``libhik_mvcamera.so``）与默认路径；
2. ``_LINUX_PRELOAD_ORDER`` 与仓库里随包提交的厂商运行时**逐一对齐**——厂商包升级后
   若新增/改名 .so，这条会红，逼着同步清单（漏预加载的表现是现场 0x8002000B）；
3. 非 Linux 平台上诊断函数优雅跳过（同一套自检脚本要能跨平台调）。

真正需要硬件的那部分（dlopen 厂商库、枚举、起流）不在这里：由
``scripts/build_linux_wheel.sh`` 末尾的冒烟段和目标机验收负责。
"""

from __future__ import annotations

import os
import sys
import unittest

# 允许从仓库直接 `python tests/test_linux_support.py`（未 pip install 时）。
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hik_mvcamera_control import _dll_utils  # noqa: E402

WRAPPERS = ("libhik_mvcamera.so", "libhik_code_reader.so")
RUNTIME_EXTRA = ("CodeReaderSdkConfig.ini", "CommonParameters.ini", "MvFGProducerGEV.cti")


class TestPlatformLibName(unittest.TestCase):
    def test_dll_maps_to_so(self) -> None:
        self.assertEqual(_dll_utils.platform_lib_name("hik_mvcamera.dll", "linux"), "libhik_mvcamera.so")
        self.assertEqual(_dll_utils.platform_lib_name("hik_code_reader.dll", "linux"), "libhik_code_reader.so")

    def test_windows_name_unchanged(self) -> None:
        self.assertEqual(_dll_utils.platform_lib_name("hik_mvcamera.dll", "win32"), "hik_mvcamera.dll")

    def test_so_name_is_idempotent(self) -> None:
        self.assertEqual(_dll_utils.platform_lib_name("libhik_mvcamera.so", "linux"), "libhik_mvcamera.so")

    def test_default_native_path_per_platform(self) -> None:
        linux_path = _dll_utils.default_native_dll("hik_mvcamera.dll", "linux")
        self.assertEqual(linux_path.parent.name, "_native")
        self.assertEqual(linux_path.name, "libhik_mvcamera.so")
        win_path = _dll_utils.default_native_dll("hik_mvcamera.dll", "win32")
        self.assertEqual(win_path.name, "hik_mvcamera.dll")


class TestPreloadList(unittest.TestCase):
    def test_no_duplicates(self) -> None:
        order = _dll_utils._LINUX_PRELOAD_ORDER
        self.assertEqual(len(order), len(set(order)))

    def test_no_wrapper_in_preload_list(self) -> None:
        for w in WRAPPERS:
            self.assertNotIn(w, _dll_utils._LINUX_PRELOAD_ORDER)

    def test_matches_committed_vendor_runtime(self) -> None:
        """预加载清单 == runtime/linux-x86_64/lib 里的厂商 .so 集合（漂移守卫）。"""
        runtime = _dll_utils.linux_runtime_dir()
        if runtime is None:
            self.skipTest("不在 SDK 仓库内（找不到 runtime/linux-x86_64/lib）")
        vendor = {p.name for p in runtime.glob("lib*.so") if p.name not in WRAPPERS}
        self.assertEqual(vendor, set(_dll_utils._LINUX_PRELOAD_ORDER))


class TestRuntimeLayout(unittest.TestCase):
    def setUp(self) -> None:
        runtime = _dll_utils.linux_runtime_dir()
        if runtime is None:
            self.skipTest("不在 SDK 仓库内（找不到 runtime/linux-x86_64/lib）")
        self.runtime = runtime

    def test_wrappers_present(self) -> None:
        for name in WRAPPERS:
            self.assertTrue((self.runtime / name).is_file(), name)

    def test_runtime_data_present(self) -> None:
        for name in RUNTIME_EXTRA:
            self.assertTrue((self.runtime / name).is_file(), name)


class TestDiagnosticsCrossPlatform(unittest.TestCase):
    def test_linux_diagnose_is_skip_on_windows(self) -> None:
        if sys.platform.startswith("linux"):
            self.skipTest("Linux 上该函数会真的 dlopen")
        out = _dll_utils.diagnose_linux_native_load("hik_mvcamera.dll", "HIK_MVCAMERA_DLL", ("MvCameraControl.dll",))
        self.assertEqual(len(out), 1)
        self.assertEqual(out[0]["strategy"], "skip-non-linux")

    def test_windows_diagnose_is_skip_on_linux(self) -> None:
        if not sys.platform.startswith("linux"):
            self.skipTest("Windows 上该函数会真的 LoadLibraryEx")
        out = _dll_utils.diagnose_windows_native_load("hik_mvcamera.dll", "HIK_MVCAMERA_DLL", ("MvCameraControl.dll",))
        self.assertEqual(out[0]["strategy"], "skip-non-windows")

    def test_context_has_platform_and_native_name(self) -> None:
        ctx = _dll_utils.diagnose_runtime_search_context("hik_mvcamera.dll", "HIK_MVCAMERA_DLL", ("MvCameraControl.dll",))
        for key in ("platform", "native_lib", "bundled_dll", "bundled_exists", "runtime_dirs"):
            self.assertIn(key, ctx)
        # 文件名的平台映射必须和 default_native_dll 一致，否则诊断会指错文件
        self.assertEqual(ctx["native_lib"], _dll_utils.default_native_dll("hik_mvcamera.dll").name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
