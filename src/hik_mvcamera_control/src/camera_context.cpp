#include "camera_ori.h"

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
