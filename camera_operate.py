import ctypes
import datetime
import threading
from ctypes import POINTER, byref, c_ubyte, cast, cdll, memset, sizeof

from camera_exception import CameraError
from camera_set_param import CameraSetParam
from logger import log
from MvImport.CameraParams_const import MV_GIGE_DEVICE, MV_ACCESS_Exclusive
from MvImport.CameraParams_header import (
    MV_CC_DEVICE_INFO,
    MV_FRAME_OUT,
    MV_FRAME_OUT_INFO_EX,
    MV_SAVE_IMAGE_TO_FILE_PARAM_EX,
    MV_Image_Jpeg,
)
from MvImport.MvCameraControl_class import MvCamera
from MvImport.MvErrorDefine_const import MV_OK
from util import decoding_char, to_hex_str


class CameraOperate:
    def __init__(self, camera_info: MV_CC_DEVICE_INFO):
        self.camera_info = camera_info
        self.camera_obj = MvCamera()
        self.is_open = False
        self.is_grabbing = False

        self.buf_lock = threading.Lock()
        self.buf_save_image = None
        self.buf_save_image_len = 0
        self.frame_info = MV_FRAME_OUT_INFO_EX()

        self.thread_handle = None
        self.is_thread_running = False
        self.exit_flag = threading.Event()

        self.camera_set_param = CameraSetParam(self.camera_obj)

    def open_camera(self):
        if self.is_open:
            return
        result = self.camera_obj.MV_CC_CreateHandle(self.camera_info)
        if result != MV_OK:
            self.camera_obj.MV_CC_DestroyHandle()
            raise CameraError("创建相机句柄失败", to_hex_str(result))
        result = self.camera_obj.MV_CC_OpenDevice(MV_ACCESS_Exclusive, 0)
        if result != MV_OK:
            self.camera_obj.MV_CC_DestroyHandle()
            raise CameraError("打开相机失败", to_hex_str(result))
        self.is_open = True
        if self.camera_info.nTLayerType == MV_GIGE_DEVICE:
            nPacketSize = self.camera_obj.MV_CC_GetOptimalPacketSize()
            if int(nPacketSize) > 0:
                ret = self.camera_obj.MV_CC_SetIntValue(
                    "GevSCPSPacketSize", nPacketSize
                )
                if ret != MV_OK:
                    log.warning("set packet size fail! ret[0x%x]" % ret)
            else:
                log.warning("packet size is invalid[%d]" % nPacketSize)

    def start_grabbing(self, expose_time=None, delay_time=None):
        if not self.is_open:
            raise CameraError("开始取流失败，请先打开相机")
        if self.is_grabbing:
            logging.warning("开始取流指令未发出，因为相机已处于取流状态")
            return
        result = self.camera_obj.MV_CC_StartGrabbing()
        if result != MV_OK:
            raise CameraError("开始取流失败", to_hex_str(result))
        try:
            if not self.is_thread_running:
                self.thread_handle = threading.Thread(
                    target=CameraOperate.work_thread, args=(self, self.exit_flag)
                )
                self.thread_handle.start()
                self.is_thread_running = True
        except TypeError:
            raise CameraError("开始取流失败，取图线程开始失败")
        self.is_grabbing = True

    def work_thread(self, exit_flag):
        stOutFrame = MV_FRAME_OUT()
        memset(byref(stOutFrame), 0, sizeof(stOutFrame))

        while not exit_flag.is_set():
            ret = self.camera_obj.MV_CC_GetImageBuffer(stOutFrame, 1000)
            if ret == MV_OK:
                self.buf_lock.acquire()
                if self.buf_save_image_len < stOutFrame.stFrameInfo.nFrameLen:
                    if self.buf_save_image is not None:
                        del self.buf_save_image
                        self.buf_save_image = None
                    self.buf_save_image = (c_ubyte * stOutFrame.stFrameInfo.nFrameLen)()
                    self.buf_save_image_len = stOutFrame.stFrameInfo.nFrameLen

                cdll.msvcrt.memcpy(
                    byref(self.frame_info),
                    byref(stOutFrame.stFrameInfo),
                    sizeof(MV_FRAME_OUT_INFO_EX),
                )
                assert self.buf_save_image is not None
                cdll.msvcrt.memcpy(
                    byref(self.buf_save_image),
                    stOutFrame.pBufAddr,
                    self.frame_info.nFrameLen,
                )
                self.buf_lock.release()
                self.camera_obj.MV_CC_FreeImageBuffer(stOutFrame)
            else:
                serial = decoding_char(
                    self.camera_info.SpecialInfo.stGigEInfo.chSerialNumber
                )
                log.debug(f"主动取图中，{serial}没有图像数据[{to_hex_str(ret)}]")

    def save_image(self, image_path_prefix: str):
        if self.buf_save_image == None:
            raise CameraError("保存图片失败，未检测到图片数据")
        # 获取缓存锁
        self.buf_lock.acquire()
        now = datetime.datetime.now().strftime("%y%m%d%H%M%S")
        file_path = (
            image_path_prefix + now + "_" + str(self.frame_info.nFrameNum) + ".jpeg"
        )
        c_file_path = file_path.encode("ascii")
        stSaveParam = MV_SAVE_IMAGE_TO_FILE_PARAM_EX()
        # ch:相机对应的像素格式 | en:Camera pixel type
        stSaveParam.enPixelType = self.frame_info.enPixelType
        stSaveParam.nWidth = self.frame_info.nWidth  # ch:相机对应的宽 | en:Width
        stSaveParam.nHeight = self.frame_info.nHeight  # ch:相机对应的高 | en:Height
        stSaveParam.nDataLen = self.frame_info.nFrameLen
        stSaveParam.pData = cast(self.buf_save_image, POINTER(c_ubyte))
        stSaveParam.enImageType = MV_Image_Jpeg
        stSaveParam.pcImagePath = ctypes.create_string_buffer(c_file_path)
        stSaveParam.iMethodValue = 1
        ret = self.camera_obj.MV_CC_SaveImageToFileEx(stSaveParam)
        if ret != MV_OK:
            raise CameraError("图片保存失败", to_hex_str(ret))
        self.buf_lock.release()
        return {"imgPath": file_path, "frameNum": self.frame_info.nFrameNum}

    def soft_trigger(self):
        if not self.is_open or not self.is_grabbing or not self.is_thread_running:
            raise CameraError("请先打开相机并开始采集")
        ret = self.camera_obj.MV_CC_SetCommandValue("TriggerSoftware")
        if ret != MV_OK:
            raise CameraError("软触发失败", to_hex_str(ret))

    def stop_grabbing(self):
        if self.is_thread_running and self.thread_handle != None:
            self.exit_flag.set()
            self.thread_handle.join()
            self.is_thread_running = False
        self.camera_obj.MV_CC_StopGrabbing()
        self.is_grabbing = False

    def close_camera(self):
        self.stop_grabbing()
        self.camera_obj.MV_CC_CloseDevice()
        self.is_open = False
        self.camera_obj.MV_CC_DestroyHandle()
