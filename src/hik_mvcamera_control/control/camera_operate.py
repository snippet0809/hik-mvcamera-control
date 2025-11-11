from hik_mvcamera_control.control.camera_context import CameraContext
from hik_mvcamera_control.control.camera_thread import CameraThread
from hik_mvcamera_control.exception.camera_exception import CameraError
from hik_mvcamera_control.logger.logger import log
from hik_mvcamera_control.MvImport.CameraParams_const import (
    MV_GIGE_DEVICE,
    MV_ACCESS_Exclusive,
)
from hik_mvcamera_control.MvImport.MvErrorDefine_const import MV_OK
from hik_mvcamera_control.util.util import to_hex_str


class CameraOperate:
    def __init__(self, context: CameraContext, thread: CameraThread):
        self.camera_context = context
        self.camera_thread = thread

    def open_camera(self):
        if self.camera_context.is_open:
            return
        self.camera_context.create_handle()
        result = self.camera_context.camera_obj.MV_CC_OpenDevice(MV_ACCESS_Exclusive, 0)
        if result != MV_OK:
            self.camera_context.camera_obj.MV_CC_DestroyHandle()
            raise CameraError("打开相机失败", to_hex_str(result))
        self.camera_context.is_open = True
        if self.camera_context.device_info.nTLayerType == MV_GIGE_DEVICE:
            nPacketSize = self.camera_context.camera_obj.MV_CC_GetOptimalPacketSize()
            if int(nPacketSize) > 0:
                ret = self.camera_context.camera_obj.MV_CC_SetIntValue(
                    "GevSCPSPacketSize", nPacketSize
                )
                if ret != MV_OK:
                    log.warning("set packet size fail! ret[0x%x]" % ret)
            else:
                log.warning("packet size is invalid[%d]" % nPacketSize)

    def start_grabbing(self, expose_time=None, delay_time=None):
        if not self.camera_context.is_open:
            raise CameraError("开始取流失败，请先打开相机")
        if self.camera_context.is_grabbing:
            log.warning("开始取流指令未发出，因为相机已处于取流状态")
            return
        result = self.camera_context.camera_obj.MV_CC_StartGrabbing()
        if result != MV_OK:
            raise CameraError("开始取流失败", to_hex_str(result))
        self.camera_thread.start_thread()
        self.camera_context.is_grabbing = True

    def stop_grabbing(self):
        self.camera_thread.stop_thread()
        self.camera_context.camera_obj.MV_CC_StopGrabbing()
        self.camera_context.is_grabbing = False

    def close_camera(self):
        self.stop_grabbing()
        self.camera_context.camera_obj.MV_CC_CloseDevice()
        self.camera_context.is_open = False
        self.camera_context.destroy_handle()
