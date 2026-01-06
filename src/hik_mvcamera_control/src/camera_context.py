from hik_mvcamera_control.exception.camera_exception import CameraError
from hik_mvcamera_control.MvImport.CameraParams_header import MV_CC_DEVICE_INFO
from hik_mvcamera_control.MvImport.MvCameraControl_class import MvCamera
from hik_mvcamera_control.MvImport.MvErrorDefine_const import MV_OK
from hik_mvcamera_control.util.util import decoding_char, to_hex_str


class CameraContext:

    def __init__(self, device_info: MV_CC_DEVICE_INFO):
        self.camera_obj = MvCamera()
        self.device_info = device_info
        self.serial_no = decoding_char(
            device_info.SpecialInfo.stGigEInfo.chSerialNumber
        )

        self.is_open = False
        self.is_grabbing = False

    def destroy_handle(self):
        self.camera_obj.MV_CC_DestroyHandle()

    def create_handle(self):
        result = self.camera_obj.MV_CC_CreateHandle(self.device_info)
        if result != MV_OK:
            self.destroy_handle()
            raise CameraError("创建相机句柄失败", to_hex_str(result))
