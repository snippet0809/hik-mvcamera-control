from ctypes import POINTER, cast

from hik_mvcamera_control.control.camera_context import CameraContext
from hik_mvcamera_control.control.camera_image import CameraImage
from hik_mvcamera_control.control.camera_operate import CameraOperate
from hik_mvcamera_control.control.camera_set_param import CameraSetParam
from hik_mvcamera_control.control.camera_thread import CameraThread
from hik_mvcamera_control.exception.camera_exception import CameraError
from hik_mvcamera_control.MvImport.CameraParams_const import (
    MV_GIGE_DEVICE,
    MV_USB_DEVICE,
)
from hik_mvcamera_control.MvImport.CameraParams_header import (
    MV_CC_DEVICE_INFO,
    MV_CC_DEVICE_INFO_LIST,
)
from hik_mvcamera_control.MvImport.MvCameraControl_class import MvCamera
from hik_mvcamera_control.MvImport.MvErrorDefine_const import MV_OK


class CameraControl:

    def __init__(self, device_info: MV_CC_DEVICE_INFO):
        self.camera_context = CameraContext(device_info)
        self.camera_thread = CameraThread(self.camera_context)
        self.camera_set_param = CameraSetParam(self.camera_context)
        self.camera_operate = CameraOperate(self.camera_context, self.camera_thread)
        self.camera_image = CameraImage(self.camera_context, self.camera_thread)


def get_device_list():
    result = MvCamera.MV_CC_Initialize()
    if result != MV_OK:
        raise CameraError("相机初始化失败", result)
    deviceList = MV_CC_DEVICE_INFO_LIST()
    result = MvCamera.MV_CC_EnumDevices((MV_GIGE_DEVICE | MV_USB_DEVICE), deviceList)
    if result != MV_OK:
        raise CameraError("枚举设备失败", result)
    return [
        cast(deviceList.pDeviceInfo[i], POINTER(MV_CC_DEVICE_INFO)).contents
        for i in range(deviceList.nDeviceNum)
    ]
