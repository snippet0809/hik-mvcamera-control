from camera_exception import CameraError
from MvImport.MvCameraControl_class import MvCamera
from MvImport.MvErrorDefine_const import MV_OK
from util import to_hex_str


class CameraSetParam:
    def __init__(self, camera_obj: MvCamera):
        self.camera_obj = camera_obj

    def setExposeTime(self, expose_time: int):
        ret = self.camera_obj.MV_CC_SetFloatValue("ExposureTime", expose_time)
        if ret != MV_OK:
            raise CameraError("设置曝光时间失败", to_hex_str(ret))

    # 0-关闭 1-开启
    def setTriggerMode(self, mode: int):
        ret = self.camera_obj.MV_CC_SetEnumValue("TriggerMode", mode)
        if ret != MV_OK:
            raise CameraError("设置触发模式失败", to_hex_str(ret))

    # 7-软触发
    def setTriggerSource(self, source: int):
        ret = self.camera_obj.MV_CC_SetEnumValue("TriggerSource", source)
        if ret != MV_OK:
            raise CameraError("设置触发源失败", to_hex_str(ret))

    def setDelayTime(self, delay_time: float):
        ret = self.camera_obj.MV_CC_SetFloatValue("TriggerDelay", delay_time)
        if ret != MV_OK:
            raise CameraError("设置触发延时失败", to_hex_str(ret))
