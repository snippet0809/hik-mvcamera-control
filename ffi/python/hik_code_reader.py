"""
ctypes 封装 `hik_code_reader` C API（参考实现）。

正式发布与 wheel 构建以仓库内 ``python/hik_code_reader`` 为准（包内 ``_native/``，含 Windows 下 DLL 搜索路径与 ``LoadLibraryEx`` 行为）。
本文件为精简参考；生产环境请使用 ``python/hik_code_reader`` 包。
"""
from __future__ import annotations

import os
import ctypes
from ctypes import (
    CFUNCTYPE,
    POINTER,
    Structure,
    c_char_p,
    c_int,
    c_size_t,
    c_void_p,
)

HIK_CR_SERIAL_MAX = 256
HIK_CR_IPV4_STR_MAX = 64

HIK_CR_OK = 0
HIK_CR_ERR_UNKNOWN = 1
HIK_CR_ERR_LOGIC = 2
HIK_CR_ERR_RUNTIME = 3
HIK_CR_ERR_INVALID_ARG = 4
HIK_CR_ERR_NO_MEMORY = 5

BcrCallback = CFUNCTYPE(None, c_char_p, POINTER(c_char_p), c_int, c_void_p)


class HikCrDeviceInfo(Structure):
    _fields_ = [
        ("serial_number", ctypes.c_char * HIK_CR_SERIAL_MAX),
        ("net_export_ip", ctypes.c_char * HIK_CR_IPV4_STR_MAX),
    ]


def _load_dll(path: str | None) -> ctypes.CDLL:
    p = path or os.environ.get("HIK_CODE_READER_DLL", "hik_code_reader.dll")
    return ctypes.CDLL(p)


class HikCodeReader:
    def __init__(self, dll_path: str | None = None) -> None:
        self._lib = _load_dll(dll_path)
        self._setup_prototypes()

    def _setup_prototypes(self) -> None:
        L = self._lib
        L.hik_cr_enum_devices.argtypes = [POINTER(POINTER(HikCrDeviceInfo)), POINTER(c_int)]
        L.hik_cr_enum_devices.restype = c_int
        L.hik_cr_free_device_list.argtypes = [POINTER(HikCrDeviceInfo)]
        L.hik_cr_free_device_list.restype = None
        for name, argtypes in [
            ("hik_cr_start_device", [c_char_p]),
            ("hik_cr_stop_device", [c_char_p]),
            ("hik_cr_trigger_device", [c_char_p]),
        ]:
            getattr(L, name).argtypes = argtypes
            getattr(L, name).restype = c_int
        L.hik_cr_register_bcr_callback_for_serial.argtypes = [c_char_p, BcrCallback, c_void_p]
        L.hik_cr_register_bcr_callback_for_serial.restype = c_int
        L.hik_cr_last_error_copy.argtypes = [c_char_p, c_size_t]
        L.hik_cr_last_error_copy.restype = c_size_t

    def last_error(self) -> str:
        need = int(self._lib.hik_cr_last_error_copy(None, 0))
        buf = ctypes.create_string_buffer(max(need, 1))
        self._lib.hik_cr_last_error_copy(buf, len(buf))
        return buf.value.decode("utf-8", errors="replace")

    def check(self, code: int) -> None:
        if code != HIK_CR_OK:
            raise OSError(code, self.last_error())

    def enum_devices(self) -> list[tuple[str, str]]:
        arr = POINTER(HikCrDeviceInfo)()
        n = c_int(0)
        self.check(self._lib.hik_cr_enum_devices(ctypes.byref(arr), ctypes.byref(n)))
        try:
            out: list[tuple[str, str]] = []
            for i in range(n.value):
                d = arr[i]
                out.append(
                    (
                        d.serial_number.split(b"\0", 1)[0].decode("utf-8", errors="replace"),
                        d.net_export_ip.split(b"\0", 1)[0].decode("utf-8", errors="replace"),
                    )
                )
            return out
        finally:
            self._lib.hik_cr_free_device_list(arr)

    def start_device(self, sn: str) -> None:
        self.check(self._lib.hik_cr_start_device(sn.encode("utf-8")))

    def stop_device(self, sn: str) -> None:
        self.check(self._lib.hik_cr_stop_device(sn.encode("utf-8")))

    def register_bcr_callback_for_serial(self, sn: str, cb: BcrCallback | None, user_data: int = 0) -> None:
        self.check(
            self._lib.hik_cr_register_bcr_callback_for_serial(
                sn.encode("utf-8"), cb, c_void_p(user_data)
            )
        )

    def trigger_device(self, sn: str) -> None:
        self.check(self._lib.hik_cr_trigger_device(sn.encode("utf-8")))
