"""共享原生库定位/预置工具：``hik_code_reader`` / ``hik_mvcamera``。

两个平台的分发模型不同，本模块按平台分支：

- **Windows**：wrapper 是 ``hik_mvcamera.dll`` / ``hik_code_reader.dll``，运行时
  （``MvCodeReaderCtrl.dll`` / ``MvCameraControl.dll``）**由使用方安装 MVS/IDMVS 提供**，
  加载前需把探测到的运行时目录前置到 ``PATH`` 与 ``add_dll_directory``。
- **Linux x86_64**：wrapper 是 ``libhik_mvcamera.so`` / ``libhik_code_reader.so``，
  厂商运行时（``libMvCameraControl.so`` / ``libMvCodeReaderCtrl.so`` 等）**随 wheel 的
  ``_native/`` 分发**，与 wrapper 同目录；wrapper 由 CMake 写成 ``DT_RPATH=$ORIGIN``，
  所以它的 ``DT_NEEDED`` 闭包不用配环境变量就能解析。

参数约定与旧包一致：``runtime_dlls`` 指定运行时库名（Linux 下由 ``_platform_lib_name``
映射成 ``lib*.so``），``env_var`` 指定覆盖 wrapper 路径的环境变量，``path`` 为显式路径。

Linux 侧有一个 Windows 没有的坑：Python 进程的主程序是 ``python3``、自身没有 RPATH，
而厂商库内部会 **按名字 dlopen** 一批库（``libMvSmtDevCtrl.so`` 等，见 ``runtime/VERSION``
注意 4——它们既不在 ``DT_NEEDED`` 里、也搜不到 strings），这些名字靠 ld.so 的搜索路径解析，
``ctypes`` 这边改 ``LD_LIBRARY_PATH`` 已经来不及（ld.so 在进程启动时就固化了解析路径）。
所以本模块对这些库**按依赖顺序预加载**（``RTLD_GLOBAL``），把 SONAME 提前登记进全局
命名空间；``start.sh`` 里另外导出 ``LD_LIBRARY_PATH`` 作为双保险。
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
    "diagnose_linux_native_load",
    "platform_lib_name",
    "linux_runtime_dir",
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

# ── Linux x86_64 ────────────────────────────────────────────────────────────
# 厂商运行时的预加载顺序（先依赖、后使用方）。这 14 个是 runtime/linux-x86_64/lib 里
# 除 wrapper 之外的必需闭包，依据 runtime/VERSION「注意 3/4」：
#   - libGCBase/libMVGigEVisionSDK 的 DT_NEEDED 里有 CLProtocol/CLAllSerial/CLSerCOM/GenCP；
#   - 读码器另需 libMvSmtDevCtrl / libMVGigEVision / CodeReaderSdkConfig.ini（漏了报 0x8002000B）；
#   - 相机用 libMVGigEVisionSDK，读码器用 libMVGigEVision，两套独立实现，都要预加载。
# 顺序只是"先依赖后使用方"，不严格时由 _linux_preload 的多轮重试兜底。
_LINUX_PRELOAD_ORDER: tuple[str, ...] = (
    "libCLAllSerial_gcc485_v3_0.so",
    "libCLSerCOM.so",
    "libCLProtocol_gcc485_v3_0.so",
    "libGenCP_gcc485_v3_0.so",
    "liblog4cpp_gcc485_v3_0.so",
    "libLog_gcc485_v3_0.so",
    "libGCBase_gcc485_v3_0.so",
    "libMvSDKVersion.so",
    "libMVGigEVisionSDK.so",
    "libMVGigEVision.so",
    "libMvSmtDevCtrl.so",
    "libMvCodeReaderCtrl.so",
    "libMvCamLVision.so",
    "libMvCameraControl.so",
)

# 预加载结果缓存：dlopen 幂等，重复调用只是白跑一遍，但诊断要保留首次的失败原因。
_linux_preload_reports: dict[str, dict[str, object]] = {}


def _is_linux(platform_name: str | None = None) -> bool:
    """是否 Linux 分支；``platform_name`` 仅供测试注入（默认取 ``sys.platform``）。"""
    return (platform_name or sys.platform).startswith("linux")


def platform_lib_name(name: str, platform_name: str | None = None) -> str:
    """把 Windows 风格的库名映射成当前平台的真实文件名。

    ``hik_mvcamera.dll`` → ``libhik_mvcamera.so``（Linux）；Windows 下原样返回，
    已经是 ``*.so`` 的名字也原样返回（幂等）。映射而非另起常量，是为了让
    ``_camera.py`` / ``_reader.py`` 两个调用点保持平台无关。
    """
    if not _is_linux(platform_name):
        return name
    if name.endswith(".so"):
        return name
    stem = name[:-4] if name.lower().endswith(".dll") else name
    if not stem.startswith("lib"):
        stem = "lib" + stem
    return stem + ".so"


def default_native_dll(dll_name: str, platform_name: str | None = None) -> Path:
    """wheel 内嵌原生库的默认路径（包内 ``_native/<按平台映射后的文件名>``）。

    ``platform_name`` 仅测试用，默认取 ``sys.platform``。
    """
    return Path(__file__).resolve().parent / "_native" / platform_lib_name(dll_name, platform_name)


def linux_runtime_dir() -> Path | None:
    """开发态（未装 wheel）下仓库里随包提交的 Linux 运行时目录；不存在返回 None。

    包目录布局：``<repo>/python/hik_mvcamera_control/hik_mvcamera_control/``，
    因此仓库根是 ``parents[3]``。
    """
    pkg = Path(__file__).resolve().parent
    for base in (pkg, *pkg.parents[:5]):
        cand = base / "runtime" / "linux-x86_64" / "lib"
        if cand.is_dir():
            return cand
    return None


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


def _linux_lib_dirs(dll_path: Path) -> list[Path]:
    """Linux 下的运行时目录候选（按优先级）：wrapper 同目录 → 环境变量 → MVS 安装位 → 仓库。

    命中哪个目录就把其中的厂商 .so 全部预加载，因此加一个新候选不会改变语义。
    """
    out: list[Path] = []
    seen: set[str] = set()

    def push(p: Path | None) -> None:
        if p is None or not p.is_dir():
            return
        try:
            key = str(p.resolve())
        except OSError:
            return
        if key not in seen:
            seen.add(key)
            out.append(p)

    push(dll_path.parent)
    for env_name in ("HIK_RUNTIME_LIB_DIR", "HIK_MVCAMERA_LIB_DIR"):
        raw = os.environ.get(env_name, "").strip()
        if raw:
            push(Path(raw))
    push(Path("/opt/MVS/lib/64"))
    push(linux_runtime_dir())
    return out


def _linux_preload(dll_path: Path) -> list[dict[str, object]]:
    """把厂商 .so 以 ``RTLD_GLOBAL`` 预加载进全局命名空间（幂等）。

    为什么必须做：厂商库内部会**按名字 dlopen** 一批库（``libMvSmtDevCtrl.so`` 等，见
    ``runtime/VERSION`` 注意 4），它们不在任何 ``DT_NEEDED`` 里；而 Python 主程序没有 RPATH，
    ``ctypes`` 又改不了已经固化的 ld.so 搜索路径。预加载后按 SONAME 就能命中。
    顺序按 ``_LINUX_PRELOAD_ORDER``（先依赖后使用方），失败条目第二轮重试一次，仍失败只记录不抛。
    """
    key = str(dll_path.resolve().parent)
    cached = _linux_preload_reports.get(key)
    if cached is not None:
        return list(cached["items"])  # type: ignore[call-overload]

    dirs = _linux_lib_dirs(dll_path)
    pending: list[Path] = []
    seen_names: set[str] = set()
    for d in dirs:
        for name in _LINUX_PRELOAD_ORDER:
            p = d / name
            if p.is_file() and name not in seen_names:
                seen_names.add(name)
                pending.append(p)
        # 顺序表之外的同目录 lib*.so（厂商包升级多文件时兜底），但绝不预加载 wrapper 自身
        try:
            for p in sorted(d.glob("lib*.so")):
                if p.name not in seen_names and p.name != dll_path.name:
                    seen_names.add(p.name)
                    pending.append(p)
        except OSError:
            continue

    results: dict[str, dict[str, object]] = {}
    for p in pending:
        if not p.is_file():
            results[p.name] = {"name": p.name, "ok": False, "error": "文件不存在"}
    remaining = [p for p in pending if p.is_file()]
    mode = ctypes.RTLD_GLOBAL | getattr(os, "RTLD_LAZY", 0)
    for _round in range(2):
        if not remaining:
            break
        nxt: list[Path] = []
        for p in remaining:
            try:
                ctypes.CDLL(str(p), mode=mode)
            except OSError as e:
                results[p.name] = {"name": p.name, "ok": False, "error": f"{type(e).__name__}: {e}"}
                nxt.append(p)
                continue
            results[p.name] = {"name": p.name, "ok": True, "error": ""}
        if len(nxt) == len(remaining):  # 一轮没有任何进展，再试也是白试
            break
        remaining = nxt

    items = [results[p.name] for p in pending if p.name in results]
    _linux_preload_reports[key] = {"items": items, "dirs": [str(d) for d in dirs]}
    return list(items)


def load_native_dll(
    dll_name: str,
    env_var: str,
    runtime_dlls: tuple[str, ...],
    path: str | None = None,
) -> ctypes.CDLL:
    """加载包内 ``_native/<按平台映射的文件名>``；可用 ``env_var`` 或 ``path`` 覆盖默认路径。

    ``dll_name`` 按 Windows 名传入（``hik_mvcamera.dll``），Linux 下由 ``platform_lib_name``
    映射成 ``libhik_mvcamera.so``，因此两个平台共用同一份调用代码。
    """

    def load_at(dll_path: Path) -> ctypes.CDLL:
        dll_path = dll_path.resolve()
        if os.name == "nt":
            _windows_prepare_dll_search_path(dll_path, runtime_dlls)
            return _cdll_win(str(dll_path))
        if _is_linux():
            _linux_preload(dll_path)
        try:
            return ctypes.CDLL(str(dll_path))
        except OSError:
            if not _is_linux():
                raise
            # 预加载的符号默认是全局的；wrapper 若被别的扩展反引用，用 GLOBAL 再试一次
            return ctypes.CDLL(str(dll_path), mode=ctypes.RTLD_GLOBAL | getattr(os, "RTLD_LAZY", 0))

    if path:
        return load_at(Path(path))
    env = os.environ.get(env_var, "").strip()
    if env:
        return load_at(Path(env))
    bundled = default_native_dll(dll_name)
    if not bundled.is_file():
        raise FileNotFoundError(
            f"{bundled.name}: 缺少 {bundled}（请安装含 _native 的 {sys.platform} wheel；开发时可将 "
            f"CMake 编出的 {bundled.name} 拷入该路径，或设置 {env_var}）"
        )
    return load_at(bundled)


def diagnose_runtime_search_context(
    dll_name: str,
    env_var: str,
    runtime_dlls: tuple[str, ...],
) -> dict[str, object]:
    """自助诊断：bundled 库、探测到的运行时目录、搜索路径（便于核对与海康运行时是否衔接）。

    两个平台返回同一套键：``bundled_dll`` 是当前平台的真实文件名，``runtime_dirs`` 在
    Windows 下是 MVS/IDMVS 安装目录、在 Linux 下是候选 runtime 目录。
    """
    bundled = default_native_dll(dll_name)
    if _is_linux():
        discovered = [str(p.resolve()) for p in _linux_lib_dirs(bundled) if p.is_dir()]
        extra: dict[str, object] = {
            "ld_library_path": os.environ.get("LD_LIBRARY_PATH", ""),
            "preloaded": _linux_preload(bundled) if bundled.is_file() else [],
        }
    else:
        discovered = [str(p.resolve()) for p in _hik_runtime_dirs_discovered(runtime_dlls) if p.is_dir()]
        extra = {"path_env_prefix_800chars": os.environ.get("PATH", "")[:800]}
    return {
        "platform": sys.platform,
        "native_lib": bundled.name,
        "bundled_dll": str(bundled),
        "bundled_exists": bundled.is_file(),
        "env_override": os.environ.get(env_var, "").strip(),
        "runtime_dirs": discovered,
        **extra,
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


def diagnose_linux_native_load(
    dll_name: str,
    env_var: str,
    runtime_dlls: tuple[str, ...],
) -> list[dict[str, str | bool]]:
    """Linux 自助诊断：逐条报告「厂商 .so 预加载」与「wrapper dlopen」的结果。

    失败时的判读：
    - 某个厂商 .so ``ok=False`` 且 error 含 ``cannot open shared object file`` → 该库缺失，
      或它自己的依赖缺失（Windows 上叫"缺 DLL"，这里同理是闭包没带全）；
    - wrapper 失败但厂商库全绿 → ``DT_RPATH=$ORIGIN`` 没落地（构建时漏了
      ``--disable-new-dtags``/``INSTALL_RPATH``），或 wrapper 与运行时不在同目录；
    - 全绿但枚举报错 → 已到 SDK/驱动层（GigE 网卡、IP 冲突、过滤驱动），不在加载器范围内。

    非 Linux 平台返回 ``skip-non-linux``，便于同一套自检脚本跨平台调用。
    """
    if not _is_linux():
        return [{"strategy": "skip-non-linux", "ok": True, "error": ""}]

    env = os.environ.get(env_var, "").strip()
    dll_path = Path(env) if env else default_native_dll(dll_name)
    bundled = default_native_dll(dll_name)
    if not dll_path.is_file():
        return [
            {
                "strategy": f"{bundled.name}-missing",
                "ok": False,
                "error": (
                    f"缺少 {bundled.name}：请使用含 _native 的 linux wheel，或把它放到 {bundled.parent}；"
                    f"也可用 {env_var} 指定路径"
                ),
            }
        ]

    dll_path = dll_path.resolve()
    out: list[dict[str, str | bool]] = []
    for item in _linux_preload(dll_path):
        out.append(
            {
                "strategy": f"preload:{item.get('name')}",
                "ok": bool(item.get("ok")),
                "error": str(item.get("error") or ""),
            }
        )
    for strategy, mode in (
        ("dlopen_default", None),
        ("dlopen_rtld_global", ctypes.RTLD_GLOBAL | getattr(os, "RTLD_LAZY", 0)),
    ):
        try:
            if mode is None:
                ctypes.CDLL(str(dll_path))
            else:
                ctypes.CDLL(str(dll_path), mode=mode)
            out.append({"strategy": strategy, "ok": True, "error": ""})
        except OSError as e:
            out.append({"strategy": strategy, "ok": False, "error": f"{type(e).__name__}: {e}"})
    return out
