"""海康读码器（MvCodeReader）C API 的 ctypes 封装：``HikCodeReader``。

从旧 ``hik_code_reader`` 包平移，DLL 定位/预置改走 ``_dll_utils``（运行时 DLL 为
``MvCodeReaderCtrl.dll``）。API 面保持不变：``enum_devices`` / ``start_device`` / ``stop_device`` /
``trigger_device`` / ``last_error`` / ``check``。
"""

from __future__ import annotations

import ctypes
from ctypes import CFUNCTYPE, POINTER, Structure, byref, c_char_p, c_int, c_size_t, c_void_p, cast
from collections.abc import Callable
from dataclasses import dataclass

from . import _dll_utils

__all__ = [
    "BcrCallback",
    "HikCodeReader",
    "HikCrDeviceInfo",
    "HikCrOpenParams",
    "HIK_CR_BCR_CLEAR",
    "HIK_CR_BCR_KEEP",
    "HIK_CR_BCR_SET",
    "HIK_CR_ERR_INVALID_ARG",
    "HIK_CR_ERR_LOGIC",
    "HIK_CR_ERR_NO_MEMORY",
    "HIK_CR_ERR_RUNTIME",
    "HIK_CR_ERR_UNKNOWN",
    "HIK_CR_IPV4_STR_MAX",
    "HIK_CR_OK",
    "HIK_CR_SERIAL_MAX",
    "OpenParams",
]

HIK_CR_SERIAL_MAX = 256
HIK_CR_IPV4_STR_MAX = 64
HIK_CR_MODEL_MAX = 64

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

_HIK_CR_RUNTIME_DLLS = ("MvCodeReaderCtrl.dll",)
_HIK_CR_ENV_VAR = "HIK_CODE_READER_DLL"
_HIK_CR_DLL_NAME = "hik_code_reader.dll"


@dataclass
class OpenParams:
    """起流前 GenICam 项；未填字段走 C++ 默认。"""

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
        ("model_name", ctypes.c_char * HIK_CR_MODEL_MAX),
    ]


class HikCodeReader:
    def __init__(self, dll_path: str | None = None) -> None:
        self._lib = _dll_utils.load_native_dll(
            _HIK_CR_DLL_NAME, _HIK_CR_ENV_VAR, _HIK_CR_RUNTIME_DLLS, dll_path
        )
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
        for name, argtypes in [
            ("hik_cr_stop_device", [c_char_p]),
            ("hik_cr_trigger_device", [c_char_p]),
        ]:
            getattr(L, name).argtypes = argtypes
            getattr(L, name).restype = c_int
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

    def enum_devices(self) -> list[tuple[str, str, str]]:
        """枚举在线读码器，返回 (序列号, GigE 导出 IP, 型号)。"""
        arr = POINTER(HikCrDeviceInfo)()
        n = c_int(0)
        self.check(self._lib.hik_cr_enum_devices(ctypes.byref(arr), ctypes.byref(n)))
        try:
            out: list[tuple[str, str, str]] = []
            for i in range(n.value):
                d = arr[i]
                out.append(
                    (
                        d.serial_number.split(b"\0", 1)[0].decode("utf-8", errors="replace"),
                        d.net_export_ip.split(b"\0", 1)[0].decode("utf-8", errors="replace"),
                        d.model_name.split(b"\0", 1)[0].decode("utf-8", errors="replace"),
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
        on_bcr: Callable[..., None] | None = None,
        clear_bcr: bool = False,
        bcr_user_data: int = 0,
    ) -> None:
        if clear_bcr and on_bcr is not None:
            raise ValueError("clear_bcr 与 on_bcr 不可同时指定")
        if clear_bcr:
            bcr_action = HIK_CR_BCR_CLEAR
            cb_arg = cast(0, BcrCallback)
        elif on_bcr is not None:
            bcr_action = HIK_CR_BCR_SET
            cb_arg = on_bcr if isinstance(on_bcr, BcrCallback) else BcrCallback(on_bcr)
        else:
            bcr_action = HIK_CR_BCR_KEEP
            cb_arg = cast(0, BcrCallback)

        raw_sn = sn.encode("utf-8")
        c_struct: HikCrOpenParams | None = None
        if params is not None:
            keep: list[bytes] = []
            c_struct = HikCrOpenParams()
            c_struct.code128 = -1 if params.code128 is None else (1 if params.code128 else 0)
            c_struct.qrcode = -1 if params.qrcode is None else (1 if params.qrcode else 0)
            if params.trigger_mode is not None:
                b = params.trigger_mode.encode("utf-8")
                keep.append(b)
                c_struct.trigger_mode = b
            else:
                c_struct.trigger_mode = None
            if params.trigger_source is not None:
                b = params.trigger_source.encode("utf-8")
                keep.append(b)
                c_struct.trigger_source = b
            else:
                c_struct.trigger_source = None
            # C 在 start 返回前已拷贝字符串；keep 仅保证调用期间有效
            _ = keep

        self.check(
            self._lib.hik_cr_start_device(
                raw_sn,
                byref(c_struct) if c_struct is not None else None,
                bcr_action,
                cb_arg,
                c_void_p(bcr_user_data),
            )
        )
        # keepalive 在 C 登记成功后再更新：失败时不留残余 thunk/闭包；CLEAR 成功后再弹出
        if clear_bcr:
            self._bcr_keepalive.pop(sn, None)
        elif on_bcr is not None:
            self._bcr_keepalive[sn] = cb_arg

    def stop_device(self, sn: str) -> None:
        self.check(self._lib.hik_cr_stop_device(sn.encode("utf-8")))

    def trigger_device(self, sn: str) -> None:
        self.check(self._lib.hik_cr_trigger_device(sn.encode("utf-8")))
