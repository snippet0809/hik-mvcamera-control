import ctypes
import datetime
from ctypes import POINTER, c_ubyte, cast

from control.camera_context import CameraContext
from control.camera_thread import CameraThread
from exception.camera_exception import CameraError
from MvImport.CameraParams_header import MV_SAVE_IMAGE_TO_FILE_PARAM_EX, MV_Image_Jpeg
from MvImport.MvErrorDefine_const import MV_OK
from util.util import to_hex_str


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

    def save_image(self, image_path_prefix: str):
        if self.camera_thread.buf_save_image == None:
            raise CameraError("保存图片失败，未检测到图片数据")
        # 获取缓存锁
        self.camera_thread.buf_lock.acquire()
        now = datetime.datetime.now().strftime("%y%m%d%H%M%S")
        file_path = (
            image_path_prefix
            + now
            + "_"
            + str(self.camera_thread.frame_info.nFrameNum)
            + ".jpeg"
        )
        c_file_path = file_path.encode("ascii")
        stSaveParam = MV_SAVE_IMAGE_TO_FILE_PARAM_EX()
        # ch:相机对应的像素格式 | en:Camera pixel type
        stSaveParam.enPixelType = self.camera_thread.frame_info.enPixelType
        stSaveParam.nWidth = self.camera_thread.frame_info.nWidth
        stSaveParam.nHeight = self.camera_thread.frame_info.nHeight
        stSaveParam.nDataLen = self.camera_thread.frame_info.nFrameLen
        stSaveParam.pData = cast(self.camera_thread.buf_save_image, POINTER(c_ubyte))
        stSaveParam.enImageType = MV_Image_Jpeg
        stSaveParam.pcImagePath = ctypes.create_string_buffer(c_file_path)
        stSaveParam.iMethodValue = 1
        ret = self.camera_context.camera_obj.MV_CC_SaveImageToFileEx(stSaveParam)
        if ret != MV_OK:
            raise CameraError("图片保存失败", to_hex_str(ret))
        self.camera_thread.buf_lock.release()
        return {
            "imagePath": file_path,
            "frameNum": self.camera_thread.frame_info.nFrameNum,
        }
