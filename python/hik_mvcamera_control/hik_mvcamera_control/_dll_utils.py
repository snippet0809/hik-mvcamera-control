"""共享 DLL 定位/预置工具：`hik_code_reader.dll`（读码器）与 `hik_mvcamera.dll`（相机）。

两个原生 DLL 均依赖海康运行时（``MvCodeReaderCtrl.dll`` / ``MvCameraControl.dll``）；Windows 下加载前
需把这些运行时目录加入 ``PATH`` 与 ``add_dll_directory``。本模块把旧 ``hik_code_reader`` 包里的逻辑
参数化：``runtime_dlls`` 指定要探测的运行时 DLL 名，``env_var`` 指定覆盖默认路径的环境变量。
预置按目录集合增量进行——读码器与相机先后实例化时都覆盖到，且不重复前置 ``PATH``。
"""

from __future__ import annotations

import os
import subprocess
import sys
import ctypes
from pathlib import Path

__all__ = [
    "default_native_dll",
    "load_native_dll",
    "diagnose_runtime_search_context",
    "diagnose_windows_native_load",
]

# 常见 MVS Runtime（x64）；IDMVS 常不在此路径，需配合 rglob/where 探测。
_HIK_MV_RUNTIME_GUESSES: tuple[Path, ...] = (
    Path(r"C:\Program Files\MVS\Runtime\Win64_x64"),
    Path(r"C:\Program Files (x86)\MVS\Runtime\Win64_x64"),
    Path(r"C:\Program Files\Common Files\MVS\Runtime\Win64_x64"),
    Path(r"C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64"),
)

# 仅在这些根下 rglob 运行时 DLL（避免全盘扫描）。
_DLL_SEARCH_ROOTS: tuple[Path, ...] = (
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


def default_native_dll(dll_name: str) -> Path:
    """wheel 内嵌 DLL 的默认路径（包内 ``_native/<dll_name>``）。"""
    return Path(__file__).resolve().parent / "_native" / dll_name


def _is_64bit_python() -> bool:
    return sys.maxsize > 2**32


def _env_dir_if_exists(name: str) -> list[Path]:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return []
    p = Path(raw)
    return [p] if p.is_dir() else []


def _path_entries_hik_mvs() -> list[Path]:
    """``Path`` 中与 MVS / IDMVS / 读码器 / 相机相关的目录（安装器常自动追加）。"""
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
            or "mvcore" in low
            or "hikrobot" in low
            or "mvvision" in low
        ):
            p = Path(part)
            if p.is_dir():
                out.append(p)
    return out


_runtime_cache: dict[tuple[str, ...], list[Path]] = {}


def _dirs_from_where(dll_name: str) -> list[Path]:
    if os.name != "nt":
        return []
    try:
        kwargs: dict = dict(
            args=["where.exe", dll_name],
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


def _dirs_from_rglob_under_roots(dll_name: str) -> list[Path]:
    if os.name != "nt":
        return []
    out: list[Path] = []
    seen: set[str] = set()
    for root in _DLL_SEARCH_ROOTS:
        if not root.is_dir():
            continue
        try:
            for dll in root.rglob(dll_name):
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


def _hik_runtime_dirs_discovered(runtime_dlls: tuple[str, ...]) -> list[Path]:
    """定位含任一运行时 DLL 的目录（按 runtime_dlls 缓存）。"""
    global _runtime_cache
    key = tuple(sorted(runtime_dlls))
    if key in _runtime_cache:
        return _runtime_cache[key]
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

    for dll in runtime_dlls:
        for p in _dirs_from_where(dll):
            push(p)
        for p in _dirs_from_rglob_under_roots(dll):
            push(p)
    for g in _HIK_MV_RUNTIME_GUESSES:
        if g.is_dir():
            push(g)
    _runtime_cache[key] = found
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


# 已预置过的目录集合：读码器与相机先后加载时增量补齐，避免 PATH 无限增长与 add_dll_directory 泄漏。
_WINDOWS_PREPARED_DIRS: list[Path] = []


def _windows_prepare_dll_search_path(dll_path: Path, runtime_dlls: tuple[str, ...]) -> None:
    """``PATH`` 前置 + ``add_dll_directory``：``_native``、探测到的运行时目录、海康环境变量与 PATH 项。"""
    if os.name != "nt":
        return
    ordered = _dedupe_existing_dirs(
        [dll_path.parent]
        + _hik_runtime_dirs_discovered(runtime_dlls)
        + _windows_official_hik_dll_dirs()
    )
    already = {str(p) for p in _WINDOWS_PREPARED_DIRS}
    new_dirs = [d for d in ordered if str(d) not in already]
    if not new_dirs:
        return
    _windows_prepend_path(new_dirs)
    add = getattr(os, "add_dll_directory", None)
    if add is not None:
        for p in new_dirs:
            try:
                add(str(p))
            except OSError:
                pass
    _WINDOWS_PREPARED_DIRS.extend(new_dirs)


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


def load_native_dll(
    dll_name: str,
    env_var: str,
    runtime_dlls: tuple[str, ...],
    path: str | None = None,
) -> ctypes.CDLL:
    """加载包内 ``_native/<dll_name>``；可用 ``env_var`` 或 ``path`` 覆盖默认路径。"""

    def load_at(dll_path: Path) -> ctypes.CDLL:
        dll_path = dll_path.resolve()
        if os.name == "nt":
            _windows_prepare_dll_search_path(dll_path, runtime_dlls)
            return _cdll_win(str(dll_path))
        return ctypes.CDLL(str(dll_path))

    if path:
        return load_at(Path(path))
    env = os.environ.get(env_var, "").strip()
    if env:
        return load_at(Path(env))
    bundled = default_native_dll(dll_name)
    if not bundled.is_file():
        raise FileNotFoundError(
            f"{dll_name}: 缺少 {bundled}（请安装含 _native 的 wheel；开发时可将 CMake 编出的 "
            f"{dll_name} 拷入该路径，或设置 {env_var}）"
        )
    return load_at(bundled)


def diagnose_runtime_search_context(
    dll_name: str,
    env_var: str,
    runtime_dlls: tuple[str, ...],
) -> dict[str, object]:
    """自助诊断：bundled DLL、探测到的运行时目录、PATH 前缀（便于核对与海康 Runtime 是否衔接）。"""
    bundled = default_native_dll(dll_name)
    discovered = [str(p.resolve()) for p in _hik_runtime_dirs_discovered(runtime_dlls) if p.is_dir()]
    path_head = os.environ.get("PATH", "")[:800]
    return {
        "bundled_dll": str(bundled),
        "bundled_exists": bundled.is_file(),
        "env_override": os.environ.get(env_var, "").strip(),
        "runtime_dirs": discovered,
        "path_env_prefix_800chars": path_head,
    }


def diagnose_windows_native_load(
    dll_name: str,
    env_var: str,
    runtime_dlls: tuple[str, ...],
) -> list[dict[str, str | bool]]:
    """在已执行与真实加载相同的 ``PATH``/``add_dll_directory`` 准备后，逐策略尝试 ``CDLL``。

    用于判断失败点是「找不到主 DLL」还是「其依赖（如 MvCodeReaderCtrl.dll / MvCameraControl.dll）」、
    以及哪种 ``LoadLibraryEx`` 标志在本机可用。
    """
    if os.name != "nt":
        return [{"strategy": "skip-non-windows", "ok": True, "error": ""}]
    env = os.environ.get(env_var, "").strip()
    dll_path = Path(env) if env else default_native_dll(dll_name)
    if not dll_path.is_file():
        b = default_native_dll(dll_name)
        return [
            {
                "strategy": f"{dll_name}-missing",
                "ok": False,
                "error": f"缺少 {dll_name}：请使用含 _native 的 wheel，或将 DLL 放到 {b}；也可用 {env_var} 指定路径",
            }
        ]
    dll_path = dll_path.resolve()
    _windows_prepare_dll_search_path(dll_path, runtime_dlls)
    spath = str(dll_path)
    specs: list[tuple[str, int | None]] = [
        ("altered_search_path_0x8", _WIN_LOAD_ALTERED_SEARCH_PATH),
        ("dll_load_dir_default_dirs_0x1100", _WIN_LOAD_DLL_FLAGS),
        ("default_cdll_no_winmode", None),
    ]
    out: list[dict[str, str | bool]] = []
    for name, flags in specs:
        try:
            if flags is None:
                ctypes.CDLL(spath)
            else:
                ctypes.CDLL(spath, winmode=flags)
            out.append({"strategy": name, "ok": True, "error": ""})
        except (OSError, TypeError) as e:
            out.append({"strategy": name, "ok": False, "error": f"{type(e).__name__}: {e}"})
    return out
