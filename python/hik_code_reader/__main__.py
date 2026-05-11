"""
运行: ``python -m hik_code_reader``（需在已安装本包的环境中，且 Windows 上需海康 Runtime）。

环境变量:

- ``HIK_CR_SELFTEST_RELAXED=1``：若 native ``CDLL`` 全部失败仍退出 0（供无 Runtime 的 CI 打日志）。
- ``HIK_CR_SELFTEST_LIVE=1``：在枚举到设备时做短会话（Open 参数 → 软触发配置 → 取流 → 触发 2 次 → 停流）。
"""

from __future__ import annotations

import os
import sys


def main() -> int:
    from hik_code_reader import (
        HikCodeReader,
        diagnose_runtime_search_context,
        diagnose_windows_native_load,
    )

    print("=== hik_code_reader selftest ===")
    print("executable:", sys.executable)
    print("package file:", __file__)

    ctx = diagnose_runtime_search_context()
    print("[context] bundled_exists:", ctx["bundled_exists"])
    print("[context] bundled:", ctx["bundled_hik_code_reader_dll"])
    print("[context] mvcode_runtime_dirs count:", len(ctx["mvcode_runtime_dirs"]))
    for d in ctx["mvcode_runtime_dirs"][:12]:
        print("   ", d)
    if len(ctx["mvcode_runtime_dirs"]) > 12:
        print("    ...")

    rows = diagnose_windows_native_load()
    print("[cdll strategies]")
    any_ok = False
    for r in rows:
        ok = bool(r["ok"])
        any_ok = any_ok or ok
        print(f"   {r['strategy']}: {'OK' if ok else 'FAIL'} {r.get('error', '')}")

    if not any_ok:
        print("[result] native hik_code_reader.dll 未能加载（多为缺海康 DLL 或 VC++ 运行库）")
        return 0 if os.environ.get("HIK_CR_SELFTEST_RELAXED") else 2

    print("[api] HikCodeReader() + enum_devices()")
    try:
        cr = HikCodeReader()
        devs = cr.enum_devices()
    except OSError as e:
        print("[result] API FAIL:", type(e).__name__, e)
        return 0 if os.environ.get("HIK_CR_SELFTEST_RELAXED") else 3

    print("[result] enum_devices count:", len(devs))
    for sn, ip in devs:
        print(f"   SN={sn!r} ip={ip!r}")

    if os.environ.get("HIK_CR_SELFTEST_LIVE") and devs:
        sn = devs[0][0]
        print("[live] start/stop smoke on", repr(sn))
        try:
            cr.open_device_for_parameters(sn)
            cr.set_enum_value_by_string(sn, "TriggerMode", "On")
            cr.set_enum_value_by_string(sn, "TriggerSource", "Software")
            cr.start_device(sn)
            for i in range(2):
                cr.trigger_device(sn)
                print(f"   trigger {i + 1} ok")
            cr.stop_device(sn)
            print("[live] OK")
        except OSError as e:
            print("[live] FAIL:", type(e).__name__, e)
            return 4

    print("=== done ===")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
