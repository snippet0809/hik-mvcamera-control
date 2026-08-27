"""海康工业相机（MvCamera）C API 的 ctypes 封装：``HikCamera``。

对齐 ``include/hik_mvcamera/c_api.h``：
- 枚举（GigE + USB，含型号）、起流/停流/软触发、临时 force_ip；
- 类型化参数读写（Int/Float/Bool/Enum/Command 走 ``hik_cv_set_param``，String 走
  ``hik_cv_set_param_string``）；命令节点经 ``set_command``；
- 图像回调经 ``start_device(on_frame=...)`` 登记，仅回调期内有效，须同步消费/拷贝。

DLL 定位/预置走 ``_dll_utils``（运行时 DLL 为 ``MvCameraControl.dll``；``HIK_MVCAMERA_DLL`` 可覆盖路径）。
"""

from __future__ import annotations

import ctypes
from ctypes import CFUNCTYPE, POINTER, Structure, Union, byref, c_char_p, c_int, c_int64, c_size_t, c_ubyte, c_uint, c_uint32, c_uint64, c_void_p, cast
from collections.abc import Callable
from dataclasses import dataclass

from . import _dll_utils

__all__ = [
    "CameraDeviceInfo",
    "CameraFrame",
    "CameraOpenParams",
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
    "HikCvDeviceInfo",
    "HikCvFrameInfo",
    "HikCvOpenParams",
    "HikCvParamValue",
]

HIK_CV_SERIAL_MAX = 128
HIK_CV_IPV4_STR_MAX = 64
HIK_CV_MODEL_MAX = 64
HIK_CV_STRING_MAX = 256

HIK_CV_OK = 0
HIK_CV_ERR_UNKNOWN = 1
HIK_CV_ERR_LOGIC = 2
HIK_CV_ERR_RUNTIME = 3
HIK_CV_ERR_INVALID_ARG = 4
HIK_CV_ERR_NO_MEMORY = 5

HIK_CV_FRAME_KEEP = 0
HIK_CV_FRAME_SET = 1
HIK_CV_FRAME_CLEAR = 2

HIK_CV_PARAM_INT = 0
HIK_CV_PARAM_FLOAT = 1
HIK_CV_PARAM_BOOL = 2
HIK_CV_PARAM_ENUM = 3
HIK_CV_PARAM_STRING = 4
HIK_CV_PARAM_COMMAND = 5

_HIK_CV_RUNTIME_DLLS = ("MvCameraControl.dll",)
_HIK_CV_ENV_VAR = "HIK_MVCAMERA_DLL"
_HIK_CV_DLL_NAME = "hik_mvcamera.dll"


@dataclass
class CameraOpenParams:
    """起流前 GenICam 项；未填字段走 C++ 默认（TriggerMode/Source 仅在 open 前写入）。"""

    trigger_mode: str | None = None
    trigger_source: str | None = None
    net_trans_mode: int = 0  # 0=不设置（SDK 默认驱动模式）; 1=驱动; 2=socket（免 GigE 过滤驱动）


@dataclass
class CameraDeviceInfo:
    serial_number: str
    net_export_ip: str = ""  # 仅 GigE 有；USB 为空串
    model_name: str = ""


@dataclass
class CameraFrame:
    """单帧元数据（不含图像数据；图像经回调的 data/len 传递）。"""

    width: int
    height: int
    pixel_type: int
    frame_len: int
    frame_num: int
    host_timestamp: int


class HikCvDeviceInfo(Structure):
    _fields_ = [
        ("serial_number", ctypes.c_char * HIK_CV_SERIAL_MAX),
        ("net_export_ip", ctypes.c_char * HIK_CV_IPV4_STR_MAX),
        ("model_name", ctypes.c_char * HIK_CV_MODEL_MAX),
    ]


class HikCvFrameInfo(Structure):
    _fields_ = [
        ("width", c_uint),
        ("height", c_uint),
        ("pixel_type", c_uint),
        ("frame_len", c_uint),
        ("frame_num", c_uint),
        ("host_timestamp", c_uint64),
    ]


class HikCvOpenParams(Structure):
    _fields_ = [
        ("trigger_mode", c_char_p),
        ("trigger_source", c_char_p),
        ("net_trans_mode", c_int),
    ]


class HikCvParamValue(Structure):
    class _U(Union):
        _fields_ = [
            ("i", c_int64),
            ("f", ctypes.c_double),
            ("b", c_int),
            ("e", c_uint32),
        ]

    _anonymous_ = ("_u",)
    _fields_ = [
        ("type", c_int),
        ("_u", _U),
    ]


HikCvFrameCallback = CFUNCTYPE(
    None, c_char_p, POINTER(HikCvFrameInfo), POINTER(c_ubyte), c_size_t, c_void_p
)


class HikCamera:
    def __init__(self, dll_path: str | None = None) -> None:
        self._lib = _dll_utils.load_native_dll(
            _HIK_CV_DLL_NAME, _HIK_CV_ENV_VAR, _HIK_CV_RUNTIME_DLLS, dll_path
        )
        self._frame_keepalive: dict[str, HikCvFrameCallback] = {}
        self._setup_prototypes()

    def _setup_prototypes(self) -> None:
        L = self._lib
        L.hik_cv_enum_devices.argtypes = [POINTER(POINTER(HikCvDeviceInfo)), POINTER(c_int)]
        L.hik_cv_enum_devices.restype = c_int
        L.hik_cv_free_device_list.argtypes = [POINTER(HikCvDeviceInfo)]
        L.hik_cv_free_device_list.restype = None
        L.hik_cv_start_device.argtypes = [c_char_p, POINTER(HikCvOpenParams), c_int, HikCvFrameCallback, c_void_p]
        L.hik_cv_start_device.restype = c_int
        for name, argtypes in [
            ("hik_cv_stop_device", [c_char_p]),
            ("hik_cv_trigger_device", [c_char_p]),
        ]:
            getattr(L, name).argtypes = argtypes
            getattr(L, name).restype = c_int
        L.hik_cv_force_ip.argtypes = [c_char_p, c_char_p, c_char_p, c_char_p]
        L.hik_cv_force_ip.restype = c_int
        L.hik_cv_set_param.argtypes = [c_char_p, c_char_p, POINTER(HikCvParamValue)]
        L.hik_cv_set_param.restype = c_int
        L.hik_cv_get_param.argtypes = [c_char_p, c_char_p, POINTER(HikCvParamValue)]
        L.hik_cv_get_param.restype = c_int
        L.hik_cv_set_param_string.argtypes = [c_char_p, c_char_p, c_char_p]
        L.hik_cv_set_param_string.restype = c_int
        L.hik_cv_get_param_string.argtypes = [c_char_p, c_char_p, c_char_p, c_size_t]
        L.hik_cv_get_param_string.restype = c_int
        L.hik_cv_last_error_copy.argtypes = [c_char_p, c_size_t]
        L.hik_cv_last_error_copy.restype = c_size_t

    def last_error(self) -> str:
        need = int(self._lib.hik_cv_last_error_copy(None, 0))
        buf = ctypes.create_string_buffer(max(need, 1))
        self._lib.hik_cv_last_error_copy(buf, len(buf))
        return buf.value.decode("utf-8", errors="replace")

    def check(self, code: int) -> None:
        if code != HIK_CV_OK:
            raise OSError(code, self.last_error())

    def enum_devices(self) -> list[CameraDeviceInfo]:
        arr = POINTER(HikCvDeviceInfo)()
        n = c_int(0)
        self.check(self._lib.hik_cv_enum_devices(ctypes.byref(arr), ctypes.byref(n)))
        try:
            out: list[CameraDeviceInfo] = []
            for i in range(n.value):
                d = arr[i]
                out.append(
                    CameraDeviceInfo(
                        serial_number=d.serial_number.split(b"\0", 1)[0].decode("utf-8", errors="replace"),
                        net_export_ip=d.net_export_ip.split(b"\0", 1)[0].decode("utf-8", errors="replace"),
                        model_name=d.model_name.split(b"\0", 1)[0].decode("utf-8", errors="replace"),
                    )
                )
            return out
        finally:
            self._lib.hik_cv_free_device_list(arr)

    def start_device(
        self,
        sn: str,
        *,
        params: CameraOpenParams | None = None,
        on_frame: Callable[..., None] | None = None,
        clear_frame: bool = False,
    ) -> None:
        """起流；``on_frame`` 登记图像回调（SET），``clear_frame`` 清除（CLEAR），均不传则不动（KEEP）。
        已在取流时忽略 ``params``，仅按 frame_action 更新图像回调。"""
        if clear_frame and on_frame is not None:
            raise ValueError("clear_frame 与 on_frame 不可同时指定")
        if clear_frame:
            action = HIK_CV_FRAME_CLEAR
            cb_arg = cast(0, HikCvFrameCallback)
        elif on_frame is not None:
            action = HIK_CV_FRAME_SET
            cb_arg = on_frame if isinstance(on_frame, HikCvFrameCallback) else HikCvFrameCallback(on_frame)
        else:
            action = HIK_CV_FRAME_KEEP
            cb_arg = cast(0, HikCvFrameCallback)

        raw_sn = sn.encode("utf-8")
        c_open: HikCvOpenParams | None = None
        if params is not None:
            keep: list[bytes] = []
            c_open = HikCvOpenParams()
            c_open.net_trans_mode = int(params.net_trans_mode or 0)
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
            # C 在 start 返回前已拷贝字符串；keep 仅保证调用期间有效
            _ = keep

        self.check(
            self._lib.hik_cv_start_device(
                raw_sn,
                byref(c_open) if c_open is not None else None,
                action,
                cb_arg,
                c_void_p(0),
            )
        )
        # keepalive 在 C 登记成功后再更新：失败时不留残余 thunk/闭包；CLEAR 成功后再弹出
        if clear_frame:
            self._frame_keepalive.pop(sn, None)
        elif on_frame is not None:
            self._frame_keepalive[sn] = cb_arg

    def stop_device(self, sn: str) -> None:
        self.check(self._lib.hik_cv_stop_device(sn.encode("utf-8")))

    def trigger_device(self, sn: str) -> None:
        """软触发（TriggerSoftware）；须已 start 且处于取流、且 TriggerMode 为 On。"""
        self.check(self._lib.hik_cv_trigger_device(sn.encode("utf-8")))

    def force_ip(self, sn: str, ip: str, subnet_mask: str, gateway: str) -> None:
        """临时强制 GigE 相机 IP（重启恢复，不改持久配置）；均为 "a.b.c.d" 字符串。"""
        self.check(
            self._lib.hik_cv_force_ip(sn.encode("utf-8"), ip.encode("utf-8"), subnet_mask.encode("utf-8"),
                                      gateway.encode("utf-8"))
        )

    @staticmethod
    def _param_value(type_: int, **fields: int | float | bool) -> HikCvParamValue:
        v = HikCvParamValue()
        v.type = type_
        for k, val in fields.items():
            setattr(v, k, val)
        return v

    def set_int(self, sn: str, name: str, value: int) -> None:
        self.check(
            self._lib.hik_cv_set_param(
                sn.encode("utf-8"), name.encode("utf-8"), byref(self._param_value(HIK_CV_PARAM_INT, i=int(value)))
            )
        )

    def set_float(self, sn: str, name: str, value: float) -> None:
        self.check(
            self._lib.hik_cv_set_param(
                sn.encode("utf-8"), name.encode("utf-8"), byref(self._param_value(HIK_CV_PARAM_FLOAT, f=float(value)))
            )
        )

    def set_bool(self, sn: str, name: str, value: bool) -> None:
        self.check(
            self._lib.hik_cv_set_param(
                sn.encode("utf-8"), name.encode("utf-8"),
                byref(self._param_value(HIK_CV_PARAM_BOOL, b=1 if value else 0)),
            )
        )

    def set_enum(self, sn: str, name: str, value: int) -> None:
        self.check(
            self._lib.hik_cv_set_param(
                sn.encode("utf-8"), name.encode("utf-8"), byref(self._param_value(HIK_CV_PARAM_ENUM, e=int(value)))
            )
        )

    def set_string(self, sn: str, name: str, value: str) -> None:
        self.check(self._lib.hik_cv_set_param_string(sn.encode("utf-8"), name.encode("utf-8"), value.encode("utf-8")))

    def set_command(self, sn: str, name: str) -> None:
        """执行 GenICam 命令节点（如 "TriggerSoftware"、"UserSetLoad"）。"""
        self.check(
            self._lib.hik_cv_set_param(
                sn.encode("utf-8"), name.encode("utf-8"), byref(self._param_value(HIK_CV_PARAM_COMMAND))
            )
        )

    def get_param(self, sn: str, name: str) -> int | float | bool | str:
        """回读参数当前值；数值走 ``hik_cv_get_param``（按返回类型解 union），字符串节点回退
        ``hik_cv_get_param_string``。设备须已 start。"""
        out = HikCvParamValue()
        code = self._lib.hik_cv_get_param(sn.encode("utf-8"), name.encode("utf-8"), byref(out))
        if code == HIK_CV_OK:
            if out.type == HIK_CV_PARAM_INT:
                return int(out.i)
            if out.type == HIK_CV_PARAM_FLOAT:
                return float(out.f)
            if out.type == HIK_CV_PARAM_BOOL:
                return bool(out.b)
            if out.type == HIK_CV_PARAM_ENUM:
                return int(out.e)
            raise OSError(HIK_CV_ERR_LOGIC, f"get_param: 节点 {name} 返回类型未预期 (type={out.type})")
        buf = ctypes.create_string_buffer(HIK_CV_STRING_MAX)
        code2 = self._lib.hik_cv_get_param_string(sn.encode("utf-8"), name.encode("utf-8"), buf, len(buf))
        if code2 == HIK_CV_OK:
            return buf.value.decode("utf-8", errors="replace")
        self.check(code)
        raise AssertionError("unreachable")
