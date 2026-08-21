"""ctypes 封装 `hik_code_reader` C API（参考）。正式发布以 ``python/hik_code_reader`` 为准。"""
from __future__ import annotations

import os
import ctypes
from ctypes import CFUNCTYPE, POINTER, Structure, byref, c_char_p, c_int, c_size_t, c_void_p, cast
from dataclasses import dataclass

HIK_CR_SERIAL_MAX = 256
HIK_CR_IPV4_STR_MAX = 64
HIK_CR_OK = 0
HIK_CR_ERR_UNKNOWN = 1
HIK_CR_ERR_LOGIC = 2
HIK_CR_ERR_RUNTIME = 3
HIK_CR_ERR_INVALID_ARG = 4
HIK_CR_ERR_NO_MEMORY = 5
HIK_CR_BCR_KEEP = 0
HIK_CR_BCR_SET = 1
HIK_CR_BCR_CLEAR = 2

BcrCallback = CFUNCTYPE(None, c_char_p, POINTER(c_char_p), c_int, c_void_p)


@dataclass
class OpenParams:
    trigger_mode: str | None = None
    trigger_source: str | None = None
    code128: bool | None = None
    qrcode: bool | None = None


class HikCrOpenParams(Structure):
    _fields_ = [
        ("trigger_mode", c_char_p),
        ("trigger_source", c_char_p),
        ("code128", c_int),
        ("qrcode", c_int),
    ]


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
        self._bcr_keepalive: dict[str, BcrCallback] = {}
        self._setup_prototypes()

    def _setup_prototypes(self) -> None:
        L = self._lib
        L.hik_cr_enum_devices.argtypes = [POINTER(POINTER(HikCrDeviceInfo)), POINTER(c_int)]
        L.hik_cr_enum_devices.restype = c_int
        L.hik_cr_free_device_list.argtypes = [POINTER(HikCrDeviceInfo)]
        L.hik_cr_free_device_list.restype = None
        L.hik_cr_start_device.argtypes = [c_char_p, POINTER(HikCrOpenParams), c_int, BcrCallback, c_void_p]
        L.hik_cr_start_device.restype = c_int
        L.hik_cr_stop_device.argtypes = [c_char_p]
        L.hik_cr_stop_device.restype = c_int
        L.hik_cr_trigger_device.argtypes = [c_char_p]
        L.hik_cr_trigger_device.restype = c_int
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

    def start_device(
        self,
        sn: str,
        *,
        params: OpenParams | None = None,
        on_bcr: BcrCallback | None = None,
        clear_bcr: bool = False,
        bcr_user_data: int = 0,
    ) -> None:
        if clear_bcr and on_bcr is not None:
            raise ValueError("clear_bcr and on_bcr conflict")
        if clear_bcr:
            act, cb_arg = HIK_CR_BCR_CLEAR, cast(0, BcrCallback)
        elif on_bcr is not None:
            # 普通可调用对象需包成 CFUNCTYPE 并保活，否则 ctypes 临时包装会被 GC 而 C 侧仍持有指针
            act, cb_arg = HIK_CR_BCR_SET, on_bcr if isinstance(on_bcr, BcrCallback) else BcrCallback(on_bcr)
        else:
            act, cb_arg = HIK_CR_BCR_KEEP, cast(0, BcrCallback)
        raw = sn.encode("utf-8")
        c_open = None
        if params is not None:
            keep: list[bytes] = []
            c_open = HikCrOpenParams()
            c_open.code128 = -1 if params.code128 is None else (1 if params.code128 else 0)
            c_open.qrcode = -1 if params.qrcode is None else (1 if params.qrcode else 0)
            if params.trigger_mode is not None:
                b = params.trigger_mode.encode("utf-8")
                keep.append(b)
                c_open.trigger_mode = b
            else:
                c_open.trigger_mode = None
            if params.trigger_source is not None:
                b = params.trigger_source.encode("utf-8")
                keep.append(b)
                c_open.trigger_source = b
            else:
                c_open.trigger_source = None
            _ = keep
        self.check(
            self._lib.hik_cr_start_device(raw, byref(c_open) if c_open else None, act, cb_arg, c_void_p(bcr_user_data))
        )
        # keepalive 在 C 登记成功后再更新；CLEAR 成功后再弹出
        if clear_bcr:
            self._bcr_keepalive.pop(sn, None)
        elif on_bcr is not None:
            self._bcr_keepalive[sn] = cb_arg

    def stop_device(self, sn: str) -> None:
        self.check(self._lib.hik_cr_stop_device(sn.encode("utf-8")))

    def trigger_device(self, sn: str) -> None:
        self.check(self._lib.hik_cr_trigger_device(sn.encode("utf-8")))
