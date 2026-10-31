"""Linux USB HID 键盘型读码器后端。

背景：海康 MvCodeReader 的 Linux 厂商库（``libMvCodeReaderCtrl.so``）只实现
GigE 传输层，``MV_CODEREADER_EnumDevices(..., MV_CODEREADER_USB_DEVICE)``
返回 ``0x80020004``，所以 Linux 上枚举不到 USB 读码器；而 Windows 的
``MvCodeReaderCtrl.dll`` 带 USB 实现，IDMVS 能枚举。

现场这批 USB 读码器在 Linux 下是标准 HID 键盘（VID ``2bdf``，复合设备同时带
CDC ACM/RNDIS），扫码时向 ``/dev/input/event*`` 发送按键。本模块直接读这些
event 设备，把按键还原成条码，作为“伪读码器”并入 ``HikCodeReader`` 的
``enum_devices`` 结果；没有硬件软触发，读到即回调。

该实现只依赖标准库，不需要 libusb/hidapi，也不需要 root：
``/dev/input/event*`` 通常是 ``root:input 0660``，运行用户加入 ``input`` 组即可。
"""

from __future__ import annotations

import ctypes
import glob
import os
import select
import struct
import sys
import threading
from dataclasses import dataclass, field

__all__ = [
    "HID_READER_MODEL",
    "HidRegistry",
    "HidReader",
    "list_hid_readers",
]

# 让上层 ``camera.is_reader_model()`` 能识别为读码器（MV-ID / MV-HY 前缀）。
# 未知型号时用这个占位，避免和真实 GigE 设备混淆。
HID_READER_MODEL = "MV-IDHID-USB"

# USB iSerial 只是整机 SN 的后 6 位（Linux gadget 字符串），不是铭牌 SN。
# 现场两台已按 IDMVS 日志/铭牌核对：
#   00DA6492165 (MV-ID2013EM-05-RBN-U)  -> USB iSerial 492165
#   00DA7817539 (MV-IDB005EX-05RSU)     -> USB iSerial 817539
# 已知设备对外返回完整 SN 与真实型号；未知设备退回 iSerial + HID_READER_MODEL。
_KNOWN_USB_READERS: dict[str, tuple[str, str]] = {
    "492165": ("00DA6492165", "MV-ID2013EM-05-RBN-U"),
    "817539": ("00DA7817539", "MV-IDB005EX-05RSU"),
}

# Linux input-event-codes.h
EV_SYN = 0
EV_KEY = 1
KEY_ENTER = 28
KEY_KPENTER = 96
KEY_LEFTSHIFT = 42
KEY_RIGHTSHIFT = 54

# US 键盘布局：keycode -> (无 Shift, 有 Shift)。覆盖条码常见的数字/字母/符号。
_KEYMAP: dict[int, tuple[str, str]] = {
    2: ("1", "!"),
    3: ("2", "@"),
    4: ("3", "#"),
    5: ("4", "$"),
    6: ("5", "%"),
    7: ("6", "^"),
    8: ("7", "&"),
    9: ("8", "*"),
    10: ("9", "("),
    11: ("0", ")"),
    12: ("-", "_"),
    13: ("=", "+"),
    16: ("q", "Q"),
    17: ("w", "W"),
    18: ("e", "E"),
    19: ("r", "R"),
    20: ("t", "T"),
    21: ("y", "Y"),
    22: ("u", "U"),
    23: ("i", "I"),
    24: ("o", "O"),
    25: ("p", "P"),
    26: ("[", "{"),
    27: ("]", "}"),
    30: ("a", "A"),
    31: ("s", "S"),
    32: ("d", "D"),
    33: ("f", "F"),
    34: ("g", "G"),
    35: ("h", "H"),
    36: ("j", "J"),
    37: ("k", "K"),
    38: ("l", "L"),
    39: (";", ":"),
    40: ("'", '"'),
    41: ("`", "~"),
    43: ("\\", "|"),
    44: ("z", "Z"),
    45: ("x", "X"),
    46: ("c", "C"),
    47: ("v", "V"),
    48: ("b", "B"),
    49: ("n", "N"),
    50: ("m", "M"),
    51: (",", "<"),
    52: (".", ">"),
    53: ("/", "?"),
    55: ("*", "*"),
    57: (" ", " "),
    # 小键盘
    71: ("7", "7"),
    72: ("8", "8"),
    73: ("9", "9"),
    74: ("-", "-"),
    75: ("4", "4"),
    76: ("5", "5"),
    77: ("6", "6"),
    78: ("+", "+"),
    79: ("1", "1"),
    80: ("2", "2"),
    81: ("3", "3"),
    82: ("0", "0"),
    83: (".", "."),
    98: ("/", "/"),
}

_EVENT_STRUCT = struct.Struct("<llHHi")  # struct input_event（64 位 Linux，24 字节）
_EVENT_SIZE = _EVENT_STRUCT.size


def _read_sysfs(path: str) -> str:
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read().strip()
    except OSError:
        return ""


@dataclass
class HidReader:
    """一个 HID 键盘型读码器；扫码时把条码通过回调抛出。"""

    serial: str
    event_path: str
    model: str = HID_READER_MODEL
    usb_serial: str = ""

    _fd: int | None = field(default=None, init=False, repr=False)
    _thread: threading.Thread | None = field(default=None, init=False, repr=False)
    _stop: threading.Event = field(default_factory=threading.Event, init=False, repr=False)
    _callback: object | None = field(default=None, init=False, repr=False)
    _buf: list[str] = field(default_factory=list, init=False, repr=False)
    _shift: bool = field(default=False, init=False, repr=False)

    @property
    def running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def start(self, callback) -> None:
        """打开 event 设备并启动读线程；已运行则只替换回调。"""
        self._callback = callback
        if self.running:
            return
        try:
            self._fd = os.open(self.event_path, os.O_RDONLY | os.O_NONBLOCK)
        except OSError as e:
            raise OSError(3, f"打开 HID 读码器 {self.serial} 失败（{self.event_path}）: {e}") from e
        self._stop.clear()
        self._buf.clear()
        self._shift = False
        self._thread = threading.Thread(
            target=self._loop, name=f"hid-reader-{self.serial}", daemon=True
        )
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        fd, self._fd = self._fd, None
        if fd is not None:
            try:
                os.close(fd)
            except OSError:
                pass
        thread, self._thread = self._thread, None
        if thread is not None and thread.is_alive() and thread is not threading.current_thread():
            thread.join(timeout=1.0)
        self._callback = None
        self._buf.clear()

    def _emit(self, code: str) -> None:
        cb = self._callback
        if cb is None or not code:
            return
        arr = (ctypes.c_char_p * 1)(code.encode("utf-8"))
        try:
            cb(self.serial.encode("utf-8"), arr, 1, None)
        except Exception:
            pass

    def _loop(self) -> None:
        while not self._stop.is_set():
            fd = self._fd
            if fd is None:
                break
            try:
                ready, _, _ = select.select([fd], [], [], 0.2)
            except (OSError, ValueError):
                break
            if not ready:
                continue
            try:
                data = os.read(fd, _EVENT_SIZE * 64)
            except BlockingIOError:
                continue
            except OSError:
                break
            self.feed_bytes(data)

    def feed_bytes(self, data: bytes) -> None:
        """解析一段 ``struct input_event`` 数据；抽出来便于无设备时做单测。"""
        for off in range(0, len(data) - _EVENT_SIZE + 1, _EVENT_SIZE):
            _, _, etype, code, value = _EVENT_STRUCT.unpack_from(data, off)
            if etype != EV_KEY:
                continue
            if code in (KEY_LEFTSHIFT, KEY_RIGHTSHIFT):
                self._shift = value != 0
                continue
            if value != 1:  # 只处理按下，忽略释放/重复
                continue
            if code in (KEY_ENTER, KEY_KPENTER):
                text = "".join(self._buf)
                self._buf.clear()
                self._emit(text)
                continue
            pair = _KEYMAP.get(code)
            if pair is None:
                continue
            self._buf.append(pair[1] if self._shift else pair[0])


def list_hid_readers(vendor_id: str = "2bdf") -> list[HidReader]:
    """扫描 Linux 上的 HID 读码器（默认海康 USB VID ``2bdf``），按序列号去重。"""
    if not sys.platform.startswith("linux"):
        return []
    found: dict[str, HidReader] = {}
    # udev 的 by-id 链接名带着 iSerial，例如
    # usb-..._Multifunction_Composite_Gadget_817539-if04-event-kbd -> ../../event7
    for link in glob.glob("/dev/input/by-id/*-event-kbd"):
        try:
            event_path = os.path.realpath(link)
            event_name = os.path.basename(event_path)
            sysdir = f"/sys/class/input/{event_name}/device"
            if _read_sysfs(f"{sysdir}/id/vendor").lower() != vendor_id.lower():
                continue
            usb_serial = _read_sysfs(f"{sysdir}/uniq")
            if not usb_serial:
                continue
            serial, model = _KNOWN_USB_READERS.get(
                usb_serial, (usb_serial, HID_READER_MODEL)
            )
            found.setdefault(
                serial,
                HidReader(serial=serial, event_path=event_path, model=model, usb_serial=usb_serial),
            )
        except OSError:
            continue
    # 兜底：没有 by-id 链接时直接扫 sysfs
    for sysdir in glob.glob("/sys/class/input/event*/device"):
        if _read_sysfs(f"{sysdir}/id/vendor").lower() != vendor_id.lower():
            continue
        event_name = os.path.basename(os.path.dirname(sysdir))
        usb_serial = _read_sysfs(f"{sysdir}/uniq")
        if not usb_serial:
            continue
        serial, model = _KNOWN_USB_READERS.get(usb_serial, (usb_serial, HID_READER_MODEL))
        if serial not in found:
            found[serial] = HidReader(
                serial=serial,
                event_path=f"/dev/input/{event_name}",
                model=model,
                usb_serial=usb_serial,
            )
    return list(found.values())


class HidRegistry:
    """管理 HID 伪读码器：枚举、起停、回调。"""

    def __init__(self) -> None:
        self._devices: dict[str, HidReader] = {}
        self._by_usb: dict[str, str] = {}
        self._started: dict[str, HidReader] = {}
        self._callbacks: dict[str, object] = {}

    def refresh(self) -> list[tuple[str, str, str]]:
        devices = list_hid_readers()
        self._devices = {d.serial: d for d in devices}
        self._by_usb = {d.usb_serial: d.serial for d in devices if d.usb_serial}
        return [(d.serial, "", d.model) for d in devices]

    def _resolve(self, serial: str) -> HidReader | None:
        if serial in self._devices:
            return self._devices[serial]
        real = self._by_usb.get(serial)
        if real is not None:
            return self._devices.get(real)
        return self._started.get(serial)

    def has(self, serial: str) -> bool:
        if not self._devices:
            self.refresh()
        return self._resolve(serial) is not None

    def start(self, serial: str, callback, *, clear: bool = False) -> None:
        device = self._resolve(serial)
        if device is None:
            if not self._devices:
                self.refresh()
                device = self._resolve(serial)
        if device is None:
            raise OSError(3, f"HID 读码器 {serial} 不在线")
        if clear:
            self._callbacks.pop(device.serial, None)
        if callback is not None:
            self._callbacks[device.serial] = callback
        device.start(self._callbacks.get(device.serial))
        self._started[device.serial] = device

    def stop(self, serial: str) -> bool:
        device = self._resolve(serial)
        if device is None:
            return False
        self._started.pop(device.serial, None)
        device.stop()
        self._callbacks.pop(device.serial, None)
        return True

    def trigger(self, serial: str) -> bool:
        """HID 读码器没有软触发；返回 True 表示由本后端接管。"""
        return self._resolve(serial) is not None

    def close(self) -> None:
        for serial in list(self._started):
            self.stop(serial)
