"""HID 后端优先级：网络设备优先，HID 只兜底。

不起任何硬件：用 ``__new__`` 绕过 ``HikCodeReader.__init__``（它要加载厂商库），
只验证 ``_hid_preferred()`` 的判定和 ``HIK_CODE_READER_DISABLE_HID`` 解析。
"""

from __future__ import annotations

import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hik_mvcamera_control._reader import HikCodeReader, _hid_enabled_from_env  # noqa: E402


class _FakeHid:
    def __init__(self, devices=("HID-1",)) -> None:
        self.devices = set(devices)
        self.started: list[str] = []
        self.stopped: list[str] = []
        self.triggered: list[str] = []

    def has(self, sn: str) -> bool:
        return sn in self.devices

    def start(self, sn: str, callback, *, clear: bool = False) -> None:
        self.started.append(sn)

    def stop(self, sn: str) -> bool:
        self.stopped.append(sn)
        return sn in self.devices

    def trigger(self, sn: str) -> bool:
        self.triggered.append(sn)
        return sn in self.devices


def _reader(*, net=(), hid_started=(), network_started=(), enabled=True) -> HikCodeReader:
    r = HikCodeReader.__new__(HikCodeReader)
    r._hid = _FakeHid()  # type: ignore[assignment]
    r._hid_enabled = enabled
    r._net_serials = set(net)
    r._hid_started = set(hid_started)
    r._network_started = set(network_started)
    return r


class TestHidPreference(unittest.TestCase):
    def test_network_serial_never_uses_hid(self) -> None:
        r = _reader(net={"NET-1"})
        self.assertFalse(r._hid_preferred("NET-1"))
        self.assertTrue(r._hid_preferred("HID-1"))

    def test_unknown_serial_is_not_forced_to_hid(self) -> None:
        r = _reader()
        # 判定为“可走 HID”，但 start_device 还要求 _hid.has()，未知 SN 最终仍走 C API。
        self.assertTrue(r._hid_preferred("UNKNOWN"))

    def test_hid_started_keeps_hid_even_if_network_appears(self) -> None:
        r = _reader(net={"DEV-1"}, hid_started={"DEV-1"})
        self.assertTrue(r._hid_preferred("DEV-1"))

    def test_network_started_is_not_stolen_by_hid(self) -> None:
        r = _reader(network_started={"DEV-1"})
        self.assertFalse(r._hid_preferred("DEV-1"))

    def test_disable_switch_turns_hid_off(self) -> None:
        r = _reader(enabled=False)
        self.assertFalse(r._hid_preferred("HID-1"))

    def test_env_switch(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("HIK_CODE_READER_DISABLE_HID", None)
            self.assertTrue(_hid_enabled_from_env())
        for value in ("1", "true", "TRUE", "yes", "on"):
            with mock.patch.dict(os.environ, {"HIK_CODE_READER_DISABLE_HID": value}):
                self.assertFalse(_hid_enabled_from_env(), value)
        for value in ("0", "false", "no", "off", ""):
            with mock.patch.dict(os.environ, {"HIK_CODE_READER_DISABLE_HID": value}):
                self.assertTrue(_hid_enabled_from_env(), value)


if __name__ == "__main__":
    unittest.main(verbosity=2)
