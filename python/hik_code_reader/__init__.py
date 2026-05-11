"""
海康读码器 C API 的 ctypes 封装。wheel 内附带 Windows x64 的 ``hik_code_reader.dll``（``_native`` 目录）。

- ``HIK_CODE_READER_DLL``：显式指定 ``hik_code_reader.dll`` 路径（可选）。
- 海康 **MvCodeReaderCtrl.dll** 等不在 wheel 内。Windows 上加载前会按 **MVS / IDMVS 安装器写入的环境变量** 与 ``Path``
  补充 DLL 搜索目录（``os.add_dll_directory``）：``GENICAM_GENTL64_PATH`` / ``GENICAM_GENTL32_PATH``（随 Python 位数）、
  ``MVCAM_GENICAM_CLPROTOCOL``，以及 ``Path`` 中含 ``MVS``、``IDMVS``、``MvSDK`` 等字样的目录（与常见 IDMVS 配置一致）。
"""

from __future__ import annotations

import os
import sys
import ctypes
from ctypes import (
    CFUNCTYPE,
    POINTER,
    Structure,
    c_char_p,
    c_int,
    c_int32,
    c_size_t,
    c_uint32,
    c_float,
    c_void_p,
)
from pathlib import Path

__all__ = [
    "BcrCallback",
    "HikCodeReader",
    "HikCrDeviceInfo",
    "HIK_CR_ERR_INVALID_ARG",
    "HIK_CR_ERR_LOGIC",
    "HIK_CR_ERR_NO_MEMORY",
    "HIK_CR_ERR_RUNTIME",
    "HIK_CR_ERR_UNKNOWN",
    "HIK_CR_IPV4_STR_MAX",
    "HIK_CR_OK",
    "HIK_CR_SERIAL_MAX",
    "default_native_dll",
]

HIK_CR_SERIAL_MAX = 256
HIK_CR_IPV4_STR_MAX = 64

HIK_CR_OK = 0
HIK_CR_ERR_UNKNOWN = 1
HIK_CR_ERR_LOGIC = 2
HIK_CR_ERR_RUNTIME = 3
HIK_CR_ERR_INVALID_ARG = 4
HIK_CR_ERR_NO_MEMORY = 5

BcrCallback = CFUNCTYPE(None, POINTER(c_char_p), c_int, c_void_p)


class HikCrDeviceInfo(Structure):
    _fields_ = [
        ("serial_number", ctypes.c_char * HIK_CR_SERIAL_MAX),
        ("net_export_ip", ctypes.c_char * HIK_CR_IPV4_STR_MAX),
    ]


def default_native_dll() -> Path:
    """wheel 内嵌 DLL 的默认路径（包内 ``_native/hik_code_reader.dll``）。"""
    return Path(__file__).resolve().parent / "_native" / "hik_code_reader.dll"


def _is_64bit_python() -> bool:
    return sys.maxsize > 2**32


def _env_dir_if_exists(name: str) -> list[Path]:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return []
    p = Path(raw)
    return [p] if p.is_dir() else []


def _path_entries_hik_mvs() -> list[Path]:
    """``Path`` 中与 MVS / IDMVS / 读码器相关的目录（安装器常自动追加）。"""
    out: list[Path] = []
    for part in os.environ.get("PATH", "").split(os.pathsep):
        part = part.strip().strip('"')
        if not part:
            continue
        low = part.lower()
        if (
            r"\mvs" in low
            or "/mvs/" in low
            or "idmvs" in low
            or "mvsdk" in low
            or "mvcode" in low
        ):
            p = Path(part)
            if p.is_dir():
                out.append(p)
    return out


def _windows_official_hik_dll_dirs() -> list[Path]:
    """海康安装程序常见环境变量与 ``Path`` 项，去重且顺序稳定。"""
    seen: set[Path] = set()
    ordered: list[Path] = []

    def push(p: Path) -> None:
        try:
            key = p.resolve()
        except OSError:
            key = p
        if key not in seen:
            seen.add(key)
            ordered.append(p)

    if _is_64bit_python():
        for p in _env_dir_if_exists("GENICAM_GENTL64_PATH"):
            push(p)
    else:
        for p in _env_dir_if_exists("GENICAM_GENTL32_PATH"):
            push(p)
    for p in _env_dir_if_exists("MVCAM_GENICAM_CLPROTOCOL"):
        push(p)
    for p in _path_entries_hik_mvs():
        push(p)
    return ordered


def _windows_add_dll_search_paths(dll_path: Path) -> None:
    """加载 ``hik_code_reader.dll`` 前，按海康官方变量与 ``Path`` 补充搜索路径（Python 3.8+ ``add_dll_directory``）。"""
    if os.name != "nt":
        return
    add = getattr(os, "add_dll_directory", None)
    if add is None:
        return
    for p in _windows_official_hik_dll_dirs():
        add(str(p))
    parent = dll_path.parent
    if parent.is_dir():
        add(str(parent))


def _load_dll(path: str | None) -> ctypes.CDLL:
    if path:
        dll_path = Path(path).resolve()
        _windows_add_dll_search_paths(dll_path)
        return ctypes.CDLL(str(dll_path))
    env = os.environ.get("HIK_CODE_READER_DLL")
    if env:
        dll_path = Path(env).resolve()
        _windows_add_dll_search_paths(dll_path)
        return ctypes.CDLL(str(dll_path))
    bundled = default_native_dll()
    if bundled.is_file():
        dll_path = bundled.resolve()
        _windows_add_dll_search_paths(dll_path)
        return ctypes.CDLL(str(dll_path))
    _windows_add_dll_search_paths(Path("hik_code_reader.dll"))
    return ctypes.CDLL("hik_code_reader.dll")


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
            ("hik_cr_open_device_for_parameters", [c_char_p]),
            ("hik_cr_trigger_device", [c_char_p]),
        ]:
            getattr(L, name).argtypes = argtypes
            getattr(L, name).restype = c_int
        L.hik_cr_set_ip.argtypes = [c_char_p, c_char_p, c_char_p, c_char_p]
        L.hik_cr_set_ip.restype = c_int
        L.hik_cr_register_bcr_callback.argtypes = [BcrCallback, c_void_p]
        L.hik_cr_register_bcr_callback.restype = c_int
        L.hik_cr_set_int_value.argtypes = [c_char_p, c_char_p, c_int32]
        L.hik_cr_set_int_value.restype = c_int
        L.hik_cr_set_string_value.argtypes = [c_char_p, c_char_p, c_char_p]
        L.hik_cr_set_string_value.restype = c_int
        L.hik_cr_set_bool_value.argtypes = [c_char_p, c_char_p, c_int32]
        L.hik_cr_set_bool_value.restype = c_int
        L.hik_cr_set_float_value.argtypes = [c_char_p, c_char_p, c_float]
        L.hik_cr_set_float_value.restype = c_int
        L.hik_cr_set_enum_value.argtypes = [c_char_p, c_char_p, c_uint32]
        L.hik_cr_set_enum_value.restype = c_int
        L.hik_cr_set_enum_value_by_string.argtypes = [c_char_p, c_char_p, c_char_p]
        L.hik_cr_set_enum_value_by_string.restype = c_int
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

    def open_device_for_parameters(self, sn: str) -> None:
        self.check(self._lib.hik_cr_open_device_for_parameters(sn.encode("utf-8")))

    def set_ip(self, sn: str, ip: str, mask: str, gateway: str) -> None:
        self.check(
            self._lib.hik_cr_set_ip(
                sn.encode("utf-8"),
                ip.encode("utf-8"),
                mask.encode("utf-8"),
                gateway.encode("utf-8"),
            )
        )

    def register_bcr_callback(self, cb: BcrCallback | None, user_data: int = 0) -> None:
        self.check(self._lib.hik_cr_register_bcr_callback(cb, c_void_p(user_data)))

    def trigger_device(self, sn: str) -> None:
        self.check(self._lib.hik_cr_trigger_device(sn.encode("utf-8")))

    def set_int_value(self, sn: str, key: str, value: int) -> None:
        self.check(self._lib.hik_cr_set_int_value(sn.encode("utf-8"), key.encode("utf-8"), c_int32(value)))

    def set_string_value(self, sn: str, key: str, value: str) -> None:
        self.check(
            self._lib.hik_cr_set_string_value(
                sn.encode("utf-8"), key.encode("utf-8"), value.encode("utf-8")
            )
        )

    def set_bool_value(self, sn: str, key: str, value: bool) -> None:
        self.check(
            self._lib.hik_cr_set_bool_value(sn.encode("utf-8"), key.encode("utf-8"), c_int32(1 if value else 0))
        )

    def set_float_value(self, sn: str, key: str, value: float) -> None:
        self.check(self._lib.hik_cr_set_float_value(sn.encode("utf-8"), key.encode("utf-8"), c_float(value)))

    def set_enum_value(self, sn: str, key: str, value: int) -> None:
        self.check(
            self._lib.hik_cr_set_enum_value(sn.encode("utf-8"), key.encode("utf-8"), c_uint32(value))
        )

    def set_enum_value_by_string(self, sn: str, key: str, symbolic: str) -> None:
        self.check(
            self._lib.hik_cr_set_enum_value_by_string(
                sn.encode("utf-8"), key.encode("utf-8"), symbolic.encode("utf-8")
            )
        )
