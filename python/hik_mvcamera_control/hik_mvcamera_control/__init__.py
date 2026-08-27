"""海康 MVS 相机 + 读码器统一 Python 封装（`hik-mvcamera-control`）。

- ``HikCodeReader``：读码器（MvCodeReaderCtrl），见 ``_reader``。
- ``HikCamera``：工业相机（MvCameraControl），见 ``_camera``。
- 原生 DLL（``hik_code_reader.dll`` / ``hik_mvcamera.dll``）在 wheel 的 ``_native`` 内；
  运行时依赖 ``MvCodeReaderCtrl.dll`` / ``MvCameraControl.dll`` 来自海康 MVS 安装，加载前自动
  把探测到的目录加入 ``PATH`` 与 ``add_dll_directory``。
- ``HIK_CODE_READER_DLL`` / ``HIK_MVCAMERA_DLL`` 可分别覆盖两个 DLL 的默认路径。
"""

from __future__ import annotations

from . import _dll_utils
from ._camera import (
    CameraDeviceInfo,
    CameraFrame,
    CameraOpenParams,
    HIK_CV_ERR_INVALID_ARG,
    HIK_CV_ERR_LOGIC,
    HIK_CV_ERR_NO_MEMORY,
    HIK_CV_ERR_RUNTIME,
    HIK_CV_ERR_UNKNOWN,
    HIK_CV_FRAME_CLEAR,
    HIK_CV_FRAME_KEEP,
    HIK_CV_FRAME_SET,
    HIK_CV_OK,
    HikCamera,
    HikCvDeviceInfo,
    HikCvFrameInfo,
    HikCvOpenParams,
    HikCvParamValue,
)
from ._reader import (
    BcrCallback,
    HIK_CR_BCR_CLEAR,
    HIK_CR_BCR_KEEP,
    HIK_CR_BCR_SET,
    HIK_CR_ERR_INVALID_ARG,
    HIK_CR_ERR_LOGIC,
    HIK_CR_ERR_NO_MEMORY,
    HIK_CR_ERR_RUNTIME,
    HIK_CR_ERR_UNKNOWN,
    HIK_CR_IPV4_STR_MAX,
    HIK_CR_OK,
    HIK_CR_SERIAL_MAX,
    HikCodeReader,
    HikCrDeviceInfo,
    HikCrOpenParams,
    OpenParams,
)

__all__ = [
    "BcrCallback",
    "CameraDeviceInfo",
    "CameraFrame",
    "CameraOpenParams",
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
    "HIK_CV_ERR_INVALID_ARG",
    "HIK_CV_ERR_LOGIC",
    "HIK_CV_ERR_NO_MEMORY",
    "HIK_CV_ERR_RUNTIME",
    "HIK_CV_ERR_UNKNOWN",
    "HIK_CV_FRAME_CLEAR",
    "HIK_CV_FRAME_KEEP",
    "HIK_CV_FRAME_SET",
    "HIK_CV_OK",
    "HikCamera",
    "HikCodeReader",
    "HikCrDeviceInfo",
    "HikCrOpenParams",
    "HikCvDeviceInfo",
    "HikCvFrameInfo",
    "HikCvOpenParams",
    "HikCvParamValue",
    "OpenParams",
    "diagnose_camera_runtime_context",
    "diagnose_camera_windows_native_load",
    "diagnose_runtime_search_context",
    "diagnose_windows_native_load",
]


def diagnose_runtime_search_context() -> dict[str, object]:
    """读码器运行时上下文诊断（对齐旧 ``hik_code_reader`` 同名函数）。"""
    return _dll_utils.diagnose_runtime_search_context("hik_code_reader.dll", "HIK_CODE_READER_DLL", ("MvCodeReaderCtrl.dll",))


def diagnose_windows_native_load() -> list[dict[str, str | bool]]:
    """读码器原生 DLL 加载诊断（对齐旧 ``hik_code_reader`` 同名函数）。"""
    return _dll_utils.diagnose_windows_native_load("hik_code_reader.dll", "HIK_CODE_READER_DLL", ("MvCodeReaderCtrl.dll",))


def diagnose_camera_runtime_context() -> dict[str, object]:
    """相机运行时上下文诊断（bundled DLL、探测到的 MvCameraControl 目录、PATH 前缀）。"""
    return _dll_utils.diagnose_runtime_search_context("hik_mvcamera.dll", "HIK_MVCAMERA_DLL", ("MvCameraControl.dll",))


def diagnose_camera_windows_native_load() -> list[dict[str, str | bool]]:
    """相机原生 DLL 加载诊断（hik_mvcamera.dll 及其依赖 MvCameraControl.dll）。"""
    return _dll_utils.diagnose_windows_native_load("hik_mvcamera.dll", "HIK_MVCAMERA_DLL", ("MvCameraControl.dll",))
