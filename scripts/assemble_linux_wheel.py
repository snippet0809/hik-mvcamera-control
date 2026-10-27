#!/usr/bin/env python3
"""把 Python 包源码 + Linux 运行时组装成 ``py3-none-linux_x86_64`` wheel。

为什么不用 ``python -m build``
------------------------------
``python/hik_mvcamera_control/setup.cfg`` 里的 ``[bdist_wheel] plat_name`` 写死成
``win_amd64``（Windows 发版依赖它），Linux 侧没法用同一个配置产出 linux 标签；
而 wheel 本身只是「zip + METADATA + WHEEL + RECORD」，自己组装反而更可控：
标签明确、时间戳可复现（同输入产出同哈希）、打包前可校验运行时文件是否带全。

产物
----
``dist/hik_mvcamera_control-<版本>-py3-none-linux_x86_64.whl``，解包后::

    hik_mvcamera_control/            # 纯 Python（ctypes 封装）
    hik_mvcamera_control/_native/    # wrapper + 厂商运行时，同目录，靠 DT_RPATH=$ORIGIN

``_native/`` 里必须同时有 wrapper（``libhik_mvcamera.so`` / ``libhik_code_reader.so``）
和全部厂商 ``.so``：wrapper 与它们是邻居，CMake 写的 ``DT_RPATH=$ORIGIN`` 才能解析；
分开摆放会让 ``import`` 直接失败（报 ``libMvCameraControl.so: cannot open ...``）。

用法
----
::

    # 用仓库里已提交的 runtime 组装（没有 Linux 编译环境时也能出包）
    python3 scripts/assemble_linux_wheel.py

    # CI：用刚从源码编出来的 wrapper 覆盖同名的两个文件，再组装
    python3 scripts/assemble_linux_wheel.py --built-dir build

    # 只校验输入齐不齐，不写文件
    python3 scripts/assemble_linux_wheel.py --check-only
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path

try:  # Python 3.11+
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - 构建机要求 3.11+
    tomllib = None  # type: ignore[assignment]

WHEEL_TAG = "py3-none-linux_x86_64"

# wrapper：Linux 侧由 CMake 的 OUTPUT_NAME 决定（带 lib 前缀、.so 后缀）。
WRAPPER_SO = ("libhik_mvcamera.so", "libhik_code_reader.so")

# 厂商运行时里「缺了就跑不起来」的最小集，依据 runtime/VERSION 注意 3/4。
# 少了任何一个都应该在打包前失败，而不是等现场 import 时才报 0x8002000B。
REQUIRED_VENDOR = (
    "libMvCameraControl.so",
    "libMvCodeReaderCtrl.so",
    "libMvSmtDevCtrl.so",
    "libMVGigEVision.so",
    "libMVGigEVisionSDK.so",
    "libMvCamLVision.so",
    "libMvSDKVersion.so",
    "libGCBase_gcc485_v3_0.so",
    "libCLProtocol_gcc485_v3_0.so",
    "libCLAllSerial_gcc485_v3_0.so",
    "libCLSerCOM.so",
    "libGenCP_gcc485_v3_0.so",
    "liblog4cpp_gcc485_v3_0.so",
    "libLog_gcc485_v3_0.so",
)

# 非 .so 但运行时要读的配置文件（读码器 SDK 按名字找 ini）。
REQUIRED_DATA = ("CodeReaderSdkConfig.ini", "CommonParameters.ini")

# 可复现 zip：固定时间戳（zip 的最小合法时间）。
ZIP_DATE = (2020, 1, 1, 0, 0, 0)


@dataclass
class WheelFile:
    arcname: str
    data: bytes


def read_version(pyproject: Path) -> str:
    """从 ``[project] version`` 读版本号（不引入 packaging 依赖）。"""
    text = pyproject.read_text(encoding="utf-8")
    if tomllib is not None:
        return str(tomllib.loads(text)["project"]["version"])
    for line in text.splitlines():  # 兜底：极简解析（构建机正常都有 tomllib）
        if line.startswith("version"):
            return line.split("=", 1)[1].strip().strip('"')
    raise RuntimeError(f"{pyproject} 里找不到 project.version")


def read_project_meta(pyproject: Path) -> dict[str, object]:
    if tomllib is None:
        return {}
    return dict(tomllib.loads(pyproject.read_text(encoding="utf-8")).get("project", {}))


def collect_python_files(pkg_dir: Path) -> list[Path]:
    """包内全部 ``.py``（跳过 ``__pycache__`` 与 ``_native``，后者单独处理）。"""
    out: list[Path] = []
    for p in sorted(pkg_dir.rglob("*.py")):
        if "__pycache__" in p.parts or "_native" in p.parts:
            continue
        out.append(p)
    return out


def collect_native_files(runtime_dir: Path, built_dir: Path | None) -> list[tuple[str, bytes]]:
    """``_native/`` 的目标内容：厂商运行时全量 + wrapper（可用新编的覆盖）。"""
    if not runtime_dir.is_dir():
        raise SystemExit(f"运行时目录不存在：{runtime_dir}")

    missing_runtime = [n for n in (*WRAPPER_SO, *REQUIRED_VENDOR, *REQUIRED_DATA) if not (runtime_dir / n).is_file()]
    if missing_runtime:
        raise SystemExit(
            f"{runtime_dir} 缺少必需文件：{', '.join(missing_runtime)}\n"
            "（先在 Linux 上跑 scripts/build_linux_wheel.sh 或核对 runtime/VERSION）"
        )

    out: list[tuple[str, bytes]] = []
    for p in sorted(runtime_dir.iterdir()):
        if not p.is_file():
            continue
        if p.suffix not in (".so", ".ini", ".cti"):
            continue
        out.append((p.name, p.read_bytes()))

    if built_dir is not None:
        for name in WRAPPER_SO:
            src = built_dir / name
            if not src.is_file():
                raise SystemExit(f"{built_dir} 里没有新编的 {name}（cmake --build 没跑或目标名变了）")
            out = [(n, b) for (n, b) in out if n != name]
            out.append((name, src.read_bytes()))
        out.sort(key=lambda kv: kv[0])
    return out


def sha256_b64(data: bytes) -> str:
    return "sha256=" + base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()


def dist_info_files(version: str, meta: dict[str, object], arc_prefix: str) -> list[WheelFile]:
    name = str(meta.get("name") or "hik-mvcamera-control")
    summary = str(meta.get("description") or "")
    requires_python = str(meta.get("requires-python") or ">=3.10")
    license_expr = str(meta.get("license") or "LicenseRef-Proprietary")

    metadata = (
        "Metadata-Version: 2.1\n"
        f"Name: {name}\n"
        f"Version: {version}\n"
        f"Summary: {summary}\n"
        f"License: {license_expr}\n"
        f"Requires-Python: {requires_python}\n"
        "Classifier: Programming Language :: Python :: 3\n"
        "Classifier: Operating System :: POSIX :: Linux\n"
        "Classifier: Topic :: System :: Hardware :: Hardware Drivers\n"
    )
    wheel = (
        "Wheel-Version: 1.0\n"
        "Generator: hik-assemble-linux (1.0)\n"
        "Root-Is-Purelib: false\n"
        f"Tag: {WHEEL_TAG}\n"
    )
    return [
        WheelFile(f"{arc_prefix}/METADATA", metadata.encode()),
        WheelFile(f"{arc_prefix}/WHEEL", wheel.encode()),
        WheelFile(f"{arc_prefix}/top_level.txt", b"hik_mvcamera_control\n"),
    ]


def build_wheel(
    version: str,
    meta: dict[str, object],
    python_files: list[tuple[str, bytes]],
    native_files: list[tuple[str, bytes]],
    out_dir: Path,
) -> Path:
    dist_name = "hik_mvcamera_control"
    arc_prefix = f"{dist_name}-{version}.dist-info"
    files: list[WheelFile] = []
    for arcname, data in python_files:
        files.append(WheelFile(arcname, data))
    for name, data in native_files:
        files.append(WheelFile(f"hik_mvcamera_control/_native/{name}", data))
    files.extend(dist_info_files(version, meta, arc_prefix))

    record_lines: list[str] = []
    for f in files:
        record_lines.append(f"{f.arcname},{sha256_b64(f.data)},{len(f.data)}")
    record_lines.append(f"{arc_prefix}/RECORD,,")
    files.append(WheelFile(f"{arc_prefix}/RECORD", ("\n".join(record_lines) + "\n").encode()))

    out_dir.mkdir(parents=True, exist_ok=True)
    wheel_path = out_dir / f"{dist_name}-{version}-{WHEEL_TAG}.whl"
    with zipfile.ZipFile(wheel_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for f in files:
            info = zipfile.ZipInfo(f.arcname, date_time=ZIP_DATE)
            info.external_attr = 0o644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, f.data)
    return wheel_path


def main(argv: list[str] | None = None) -> int:
    repo_root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description="组装 hik-mvcamera-control 的 Linux wheel")
    ap.add_argument("--package-dir", type=Path, default=repo_root / "python/hik_mvcamera_control/hik_mvcamera_control")
    ap.add_argument("--pyproject", type=Path, default=repo_root / "python/hik_mvcamera_control/pyproject.toml")
    ap.add_argument("--runtime-dir", type=Path, default=repo_root / "runtime/linux-x86_64/lib")
    ap.add_argument("--built-dir", type=Path, default=None, help="cmake 产物目录（覆盖 wrapper .so）")
    ap.add_argument("--out", type=Path, default=repo_root / "dist")
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args(argv)

    if not args.package_dir.is_dir():
        raise SystemExit(f"包目录不存在：{args.package_dir}")

    version = read_version(args.pyproject)
    meta = read_project_meta(args.pyproject)
    native = collect_native_files(args.runtime_dir, args.built_dir)
    # as_posix()：zip 内路径分隔符必须是 "/"。在 Windows 上组装（没有 Linux 编译环境时的
    # 退化路径）时 str(Path) 会给出反斜杠，写进 zip 后 Linux 上 pip 会装出一堆
    # "hik_mvcamera_control\__init__.py" 这种文件，import 直接失败。
    py_files = [
        (p.relative_to(args.package_dir.parent).as_posix(), p.read_bytes()) for p in collect_python_files(args.package_dir)
    ]

    wrappers = [n for (n, _) in native if n in WRAPPER_SO]
    vendor = [n for (n, _) in native if n in REQUIRED_VENDOR]
    total = sum(len(b) for (_, b) in native)
    print(f"版本        : {version}")
    print(f"Python 文件 : {len(py_files)}")
    print(f"_native     : {len(native)} 个文件 / {total / 1024 / 1024:.1f} MiB（wrapper {len(wrappers)}，厂商 {len(vendor)}）")
    print(f"标签        : {WHEEL_TAG}")

    if args.check_only:
        print("check-only：输入齐备")
        return 0

    wheel = build_wheel(version, meta, py_files, native, args.out)
    print(f"产物        : {wheel}  ({wheel.stat().st_size / 1024 / 1024:.1f} MiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
