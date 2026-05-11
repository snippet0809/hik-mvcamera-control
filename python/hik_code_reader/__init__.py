"""
海康读码器 C API 的 ctypes 封装。wheel 内 ``_native`` 含 ``hik_code_reader.dll`` 与海康 ``*.lib``（供再链）。
**海康 MvCodeReader 运行时 ``*.dll`` 不在公共 CI 产物中打包**（避免依赖「某台开发机是否装了 MVS」）；运行环境需能解析这些依赖（见仓库 README）。

- ``HIK_CODE_READER_DLL``：显式指定 ``hik_code_reader.dll`` 路径（可选）。
- Windows 加载前：将 ``hik_code_reader.dll`` 所在目录、**探测到的 MvCodeReader 运行时目录**（``where
  MvCodeReaderCtrl.dll``、常见 ``Program Files`` 下安装树）、固定 ``MVS\\Runtime\\Win64_x64`` 猜测路径，以及
  海康安装器写入的环境变量 / ``Path`` 项一并加入 ``PATH`` 与 ``add_dll_directory``（结果模块级缓存，只算一次）。
- 使用 ``LoadLibraryEx`` 兼容标志，便于从 ``_native`` 同目录解析同目录放置的依赖 DLL（若你自行随应用分发）。
"""

from __future__ import annotations

import os
import subprocess
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
from collections.abc import Callable
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
            or "hikrobot" in low
            or "mvvision" in low
        ):
            p = Path(part)
            if p.is_dir():
                out.append(p)
    return out


# 常见 MVS Runtime（x64）；IDMVS 常不在此路径，需配合 ``_mvcode_runtime_dirs_discovered``。
_HIK_MV_RUNTIME_GUESSES: tuple[Path, ...] = (
    Path(r"C:\Program Files\MVS\Runtime\Win64_x64"),
    Path(r"C:\Program Files (x86)\MVS\Runtime\Win64_x64"),
    Path(r"C:\Program Files\Common Files\MVS\Runtime\Win64_x64"),
    Path(r"C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64"),
)

# 仅在这些根下 rglob ``MvCodeReaderCtrl.dll``（避免全盘扫描）。
_MVCR_DLL_SEARCH_ROOTS: tuple[Path, ...] = (
    Path(r"C:\Program Files\MVS"),
    Path(r"C:\Program Files\IDMVS"),
    Path(r"C:\Program Files\Hikrobot"),
    Path(r"C:\Program Files\MvVision"),
    Path(r"C:\Program Files (x86)\MVS"),
    Path(r"C:\Program Files (x86)\IDMVS"),
    Path(r"C:\Program Files (x86)\Hikrobot"),
    Path(r"C:\Program Files (x86)\MvVision"),
    Path(r"C:\Program Files\Common Files\MVS"),
    Path(r"C:\Program Files (x86)\Common Files\MVS"),
)

_mvcode_runtime_cache: list[Path] | None = None


def _mv_dirs_from_where_mvcode_reader_ctrl() -> list[Path]:
    if os.name != "nt":
        return []
    try:
        kwargs: dict = dict(
            args=["where.exe", "MvCodeReaderCtrl.dll"],
            capture_output=True,
            text=True,
            timeout=8,
        )
        if sys.platform == "win32":
            kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
        r = subprocess.run(**kwargs)
    except (OSError, subprocess.TimeoutExpired):
        return []
    if r.returncode != 0 or not r.stdout.strip():
        return []
    out: list[Path] = []
    seen: set[str] = set()
    for line in r.stdout.strip().splitlines():
        p = Path(line.strip())
        if p.is_file():
            d = p.parent.resolve()
            k = str(d)
            if k not in seen:
                seen.add(k)
                out.append(d)
    return out


def _mv_dirs_from_rglob_under_roots() -> list[Path]:
    if os.name != "nt":
        return []
    out: list[Path] = []
    seen: set[str] = set()
    for root in _MVCR_DLL_SEARCH_ROOTS:
        if not root.is_dir():
            continue
        try:
            for dll in root.rglob("MvCodeReaderCtrl.dll"):
                if not dll.is_file():
                    continue
                d = dll.parent.resolve()
                k = str(d)
                if k not in seen:
                    seen.add(k)
                    out.append(d)
        except OSError:
            continue
    return out


def _mvcode_runtime_dirs_discovered() -> list[Path]:
    """定位含 ``MvCodeReaderCtrl.dll`` 的目录（模块级缓存）。"""
    global _mvcode_runtime_cache
    if _mvcode_runtime_cache is not None:
        return _mvcode_runtime_cache
    found: list[Path] = []
    seen: set[str] = set()

    def push(p: Path) -> None:
        if not p.is_dir():
            return
        try:
            k = str(p.resolve())
        except OSError:
            return
        if k not in seen:
            seen.add(k)
            found.append(p)

    for p in _mv_dirs_from_where_mvcode_reader_ctrl():
        push(p)
    for p in _mv_dirs_from_rglob_under_roots():
        push(p)
    for g in _HIK_MV_RUNTIME_GUESSES:
        if g.is_dir():
            push(g)
    _mvcode_runtime_cache = found
    return found


def _dedupe_existing_dirs(paths: list[Path]) -> list[Path]:
    out: list[Path] = []
    seen: set[str] = set()
    for p in paths:
        if not p.is_dir():
            continue
        try:
            k = str(p.resolve())
        except OSError:
            continue
        if k not in seen:
            seen.add(k)
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


def _windows_prepend_path(dirs: list[Path]) -> None:
    """把目录插到 ``PATH`` 最前，便于依赖 DLL 按传统搜索链解析。"""
    if os.name != "nt":
        return
    parts: list[str] = []
    for d in dirs:
        try:
            if d.is_dir():
                parts.append(str(d.resolve()))
        except OSError:
            continue
    if parts:
        os.environ["PATH"] = os.pathsep.join(parts) + os.pathsep + os.environ.get("PATH", "")


def _windows_prepare_dll_search_path(dll_path: Path) -> None:
    """``PATH`` 前置 + ``add_dll_directory``：``_native``、探测到的 MvCodeReader 目录、海康环境变量与 PATH 项。"""
    if os.name != "nt":
        return
    ordered = _dedupe_existing_dirs(
        [dll_path.parent]
        + _mvcode_runtime_dirs_discovered()
        + _windows_official_hik_dll_dirs()
    )
    _windows_prepend_path(ordered)
    add = getattr(os, "add_dll_directory", None)
    if add is None:
        return
    for p in ordered:
        try:
            add(str(p))
        except OSError:
            pass


# LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
_WIN_LOAD_DLL_FLAGS = 0x100 | 0x1000
# LOAD_WITH_ALTERED_SEARCH_PATH：在部分系统上比 0x1100 更稳，用于解析与主 DLL 同目录的依赖
_WIN_LOAD_ALTERED_SEARCH_PATH = 0x8


def _cdll_win(path: str) -> ctypes.CDLL:
    """按多种 ``LoadLibraryEx`` 策略依次尝试（altered search path → 组合标志 → 默认）。"""
    last: OSError | TypeError | None = None
    for flags in (_WIN_LOAD_ALTERED_SEARCH_PATH, _WIN_LOAD_DLL_FLAGS, None):
        try:
            if flags is None:
                return ctypes.CDLL(path)
            return ctypes.CDLL(path, winmode=flags)
        except (OSError, TypeError) as e:
            last = e
            continue
    if last is not None:
        raise last
    return ctypes.CDLL(path)


def _load_dll(path: str | None) -> ctypes.CDLL:
    def load_at(dll_path: Path) -> ctypes.CDLL:
        dll_path = dll_path.resolve()
        if os.name == "nt":
            _windows_prepare_dll_search_path(dll_path)
            return _cdll_win(str(dll_path))
        return ctypes.CDLL(str(dll_path))

    if path:
        return load_at(Path(path))
    env = os.environ.get("HIK_CODE_READER_DLL")
    if env:
        return load_at(Path(env))
    bundled = default_native_dll()
    if bundled.is_file():
        return load_at(bundled)
    if os.name == "nt":
        _windows_prepare_dll_search_path(Path.cwd() / "hik_code_reader.dll")
        return _cdll_win("hik_code_reader.dll")
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

    def register_bcr_callback(self, cb: Callable[..., None] | None, user_data: int = 0) -> None:
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
