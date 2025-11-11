import ctypes
import datetime
import os
from ctypes import POINTER, c_ubyte, cast

from control.camera_context import CameraContext
from control.camera_thread import CameraThread
from exception.camera_exception import CameraError
from MvImport.CameraParams_header import MV_SAVE_IMAGE_TO_FILE_PARAM_EX, MV_Image_Jpeg
from MvImport.MvErrorDefine_const import MV_OK
from util.util import to_hex_str


class CameraTriggerResult:
    def __init__(self, image_path: str, frame_no: int):
        self.image_path = image_path
        self.frame_no = frame_no


class CameraImage:

    def __init__(self, context: CameraContext, thread: CameraThread):
        self.camera_context = context
        self.camera_thread = thread

    def soft_trigger(self):
        if not self.camera_context.is_open or not self.camera_context.is_grabbing:
            raise CameraError("请先打开相机并开始采集")
        ret = self.camera_context.camera_obj.MV_CC_SetCommandValue("TriggerSoftware")
        if ret != MV_OK:
            raise CameraError("软触发失败", to_hex_str(ret))

    async def save_image(self, image_path_prefix: str):
        if self.camera_thread.buf_save_image == None:
            raise CameraError(f"{self.camera_context.serial_no}相机未检测到图片数据")
        # 获取缓存锁
        self.camera_thread.buf_lock.acquire()
        now = datetime.datetime.now().strftime("%y%m%d%H%M%S")
        image_name = now + "_" + str(self.camera_thread.frame_info.nFrameNum) + ".jpeg"
        file_path = os.path.join(image_path_prefix, image_name)
        c_file_path = file_path.encode("ascii")
        stSaveParam = MV_SAVE_IMAGE_TO_FILE_PARAM_EX()
        # ch:相机对应的像素格式 | en:Camera pixel type
        stSaveParam.enPixelType = self.camera_thread.frame_info.enPixelType
        stSaveParam.nWidth = self.camera_thread.frame_info.nWidth
        stSaveParam.nHeight = self.camera_thread.frame_info.nHeight
        stSaveParam.nDataLen = self.camera_thread.frame_info.nFrameLen
        stSaveParam.pData = cast(self.camera_thread.buf_save_image, POINTER(c_ubyte))
        stSaveParam.enImageType = MV_Image_Jpeg
        stSaveParam.nQuality = 80  # (50, 99]
        stSaveParam.pcImagePath = ctypes.create_string_buffer(c_file_path)
        stSaveParam.iMethodValue = 1
        ret = self.camera_context.camera_obj.MV_CC_SaveImageToFileEx(stSaveParam)
        self.camera_thread.buf_lock.release()
        if ret != MV_OK:
            raise CameraError(
                f"{self.camera_context.serial_no}相机保存图片失败", to_hex_str(ret)
            )
        return CameraTriggerResult(file_path, self.camera_thread.frame_info.nFrameNum)
