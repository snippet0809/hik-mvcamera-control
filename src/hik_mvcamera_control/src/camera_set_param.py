from hik_mvcamera_control.control.camera_context import CameraContext
from hik_mvcamera_control.exception.camera_exception import CameraError
from hik_mvcamera_control.MvImport.MvErrorDefine_const import MV_OK
from hik_mvcamera_control.util.util import to_hex_str


class CameraSetParam:

    def __init__(self, context: CameraContext):
        self.context = context

    def check_is_open(self):
        if not self.context.is_open:
            raise CameraError(f"请先打开{self.context.serial_no}相机")

    def setExposeTime(self, expose_time: int):
        self.check_is_open()
        ret = self.context.camera_obj.MV_CC_SetFloatValue("ExposureTime", expose_time)
        if ret != MV_OK:
            raise CameraError(
                f"{self.context.serial_no}相机设置曝光时间失败", to_hex_str(ret)
            )

    # 0-关闭 1-开启
    def setTriggerMode(self, mode: int):
        self.check_is_open()
        ret = self.context.camera_obj.MV_CC_SetEnumValue("TriggerMode", mode)
        if ret != MV_OK:
            raise CameraError(
                f"{self.context.serial_no}相机设置触发模式失败", to_hex_str(ret)
            )

    # 7-软触发
    def setTriggerSource(self, source: int):
        self.check_is_open()
        ret = self.context.camera_obj.MV_CC_SetEnumValue("TriggerSource", source)
        if ret != MV_OK:
            raise CameraError(
                f"{self.context.serial_no}相机设置触发源失败", to_hex_str(ret)
            )

    def setDelayTime(self, delay_time: float):
        self.check_is_open()
        ret = self.context.camera_obj.MV_CC_SetFloatValue("TriggerDelay", delay_time)
        if ret != MV_OK:
            raise CameraError(
                f"{self.context.serial_no}相机设置触发延时失败", to_hex_str(ret)
            )
