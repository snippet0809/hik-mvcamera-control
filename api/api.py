import os
from datetime import datetime
from time import sleep

from control.camera_control import CameraControl, get_device_list
from exception.camera_exception import CameraError
from logger.logger import log
from util.util import decoding_char

camera_control_dict: dict[str, CameraControl] = dict()
camera_frame_no_dict: dict[str, int] = dict()


def get_camara_list():
    device_list = get_device_list()
    global camera_control_dict
    for device in device_list:
        serial = decoding_char(device.SpecialInfo.stGigEInfo.chSerialNumber)
        camera_control_dict[serial] = CameraControl(device)
    return list(camera_control_dict.keys())


def check_camera_serial(serial: str | None) -> list[str]:
    serial_list = []
    if serial is None:
        serial_list = list(camera_control_dict.keys())
    else:
        if camera_control_dict[serial] is None:
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


def trigger_camera(serial: str | None = None, image_path_prefix: str | None = None):
    if image_path_prefix is None:
        image_path_prefix = os.path.join(
            os.path.expanduser("~"),
            ".yspinfo",
            "hik-mvcamera-control",
            "image",
            datetime.now().strftime("%Y-%m"),
        )
    serial_list = check_camera_serial(serial)
    for serial in serial_list:
        log.debug(f"{serial}相机即将执行软触发")
        camera_control_dict[serial].camera_image.soft_trigger()
        log.info(f"{serial}相机软触发成功")
    sleep(0.5)
    now = datetime.now().timestamp()
    image_dict: dict[str, str] = dict()
    while datetime.now().timestamp() < now + 5:
        for serial in serial_list:
            try:
                result = camera_control_dict[serial].camera_image.save_image(
                    image_path_prefix
                )
                if (
                    camera_frame_no_dict.get(serial) is None
                    or camera_frame_no_dict.get(serial) != result.frame_no
                ):
                    camera_frame_no_dict[serial] = result.frame_no
                    image_dict[serial] = result.image_path
                    serial_list.remove(serial)
                    log.info(f"{serial}相机图片保存成功{result.image_path}")
            except CameraError as e:
                log.debug(f"{e.err_msg}[{e.err_code}]")
                pass

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
