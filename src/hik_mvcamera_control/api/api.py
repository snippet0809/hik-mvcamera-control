import asyncio
import os
from datetime import datetime
from time import sleep

from hik_mvcamera_control.control.camera_control import CameraControl, get_device_list
from hik_mvcamera_control.control.camera_image import CameraTriggerResult
from hik_mvcamera_control.exception.camera_exception import CameraError
from hik_mvcamera_control.logger.logger import log
from hik_mvcamera_control.util.util import decoding_char

camera_control_dict: dict[str, CameraControl] = dict()
camera_frame_no_dict: dict[str, int] = dict()


def get_camera_list():
    device_list = get_device_list()
    global camera_control_dict
    for device in device_list:
        serial = decoding_char(device.SpecialInfo.stGigEInfo.chSerialNumber)
        # 不要重复创建相机句柄，可能会出问题("00开头的是USB读码器，只检测GIGE_DEVICE也会返回读码器")
        if camera_control_dict.get(serial) is None and not serial.startswith("00DA"):
            camera_control_dict[serial] = CameraControl(device)
    log.debug(f"枚举相机结果: {camera_control_dict.keys()}")
    return list(camera_control_dict.keys())


def check_camera_serial(serial: str | None) -> list[str]:
    serial_list = []
    if serial is None:
        serial_list = list(camera_control_dict.keys())
    else:
        log.debug(f"当前相机列表: {camera_control_dict.keys()}")
        if camera_control_dict.get(serial) is None:
            raise CameraError(f"未找到序列号为{serial}的设备")
        else:
            serial_list.append(serial)
    return serial_list


def open_camera(serial: str | None = None):
    serial_list = check_camera_serial(serial)
    for serial in serial_list:
        log.debug(f"打开{serial}相机前，准备执行关闭操作")
        camera_control_dict[serial].camera_operate.close_camera()
        log.debug(f"{serial}相机关闭成功，准备执行打开操作")
        camera_control_dict[serial].camera_operate.open_camera()
        log.info(f"{serial}相机打开成功")


def start_grabbing(
    serial: str | None = None,
    expose_time: int = 100 * 1000,
    trigger_mode: int = 1,
    trigger_source: int = 7,
):
    serial_list = check_camera_serial(serial)
    for serial in serial_list:
        camera_control_dict[serial].camera_set_param.setExposeTime(expose_time)
        camera_control_dict[serial].camera_set_param.setTriggerMode(trigger_mode)
        camera_control_dict[serial].camera_set_param.setTriggerSource(trigger_source)
        log.debug(f"{serial}相机开始采集前，准备执行停止采集操作")
        camera_control_dict[serial].camera_operate.stop_grabbing()
        log.debug(f"{serial}相机停止采集成功，准备执行开始采集操作")
        camera_control_dict[serial].camera_operate.start_grabbing()
        log.info(f"{serial}相机开始采集成功")
        camera_frame_no_dict[serial] = 0


async def trigger_camera(
    serial: str | None = None, image_path_prefix: str | None = None
):
    if image_path_prefix is None:
        image_path_prefix = os.path.join(
            os.path.expanduser("~"), "Pictures", "hik-mvcamera-control"
        )
    serial_list = check_camera_serial(serial)
    for serial in serial_list:
        log.debug(f"{serial}相机即将执行软触发")
        camera_control_dict[serial].camera_image.soft_trigger()
        log.info(f"{serial}相机软触发成功")
    await asyncio.sleep(0.5)
    now = datetime.now().timestamp()
    image_dict: dict[str, str] = dict()
    while datetime.now().timestamp() < now + 5:
        task_list: list[asyncio.Task[CameraTriggerResult]] = []
        for serial in serial_list:
            task: asyncio.Task[CameraTriggerResult] = asyncio.create_task(
                camera_control_dict[serial].camera_image.save_image(
                    image_path_prefix, camera_frame_no_dict.get(serial)
                )
            )
            task_list.append(task)
        results = await asyncio.gather(*task_list, return_exceptions=True)
        need_remove_serial: list[str] = []
        for i, result in enumerate(results):
            if isinstance(result, CameraError):
                if result.err_code is None:
                    log.debug(f"{result.err_msg}")
                else:
                    log.debug(f"{result.err_msg}[{result.err_code}]")
            elif isinstance(result, CameraTriggerResult):
                if (
                    camera_frame_no_dict.get(serial_list[i]) is None
                    or camera_frame_no_dict.get(serial_list[i]) != result.frame_no
                ):
                    camera_frame_no_dict[serial_list[i]] = result.frame_no
                    image_dict[serial_list[i]] = result.image_path
                    need_remove_serial.append(serial_list[i])
                    log.info(f"{serial}相机图片保存成功{result.image_path}")
        # 必须统一remove，否则会数组下标越界
        for item in need_remove_serial:
            serial_list.remove(item)
        if len(serial_list) == 0:
            log.info(f"已获取所有图片")
            break
    if len(serial_list) > 0:
        raise CameraError("获取图片超时")
    else:
        return image_dict


def stop_grabbing(serial: str | None = None):
    serial_list = check_camera_serial(serial)
    for serial in serial_list:
        log.debug(f"{serial}相机即将执行停止采集")
        camera_control_dict[serial].camera_operate.stop_grabbing()
        log.info(f"{serial}相机停止采集成功")


def close_camera(serial: str | None = None):
    serial_list = check_camera_serial(serial)
    for serial in serial_list:
        log.debug(f"{serial}相机即将执行关闭")
        camera_control_dict[serial].camera_operate.close_camera()
        log.info(f"{serial}相机关闭成功")


def restart_camera(serial: str | None = None):
    serial_list = check_camera_serial(serial)
    for serial in serial_list:
        log.debug(f"{serial}相机即将执行重启")
        camera_control_dict[serial].camera_operate.close_camera()
        sleep(1)
        camera_control_dict[serial].camera_operate.open_camera()
        log.info(f"{serial}相机重启成功")
