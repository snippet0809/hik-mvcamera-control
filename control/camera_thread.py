import threading
from ctypes import byref, c_ubyte, cdll, memset, sizeof
from logging import log

from control.camera_context import CameraContext
from exception.camera_exception import CameraError
from MvImport.CameraParams_header import MV_FRAME_OUT, MV_FRAME_OUT_INFO_EX
from MvImport.MvCameraControl_class import MvCamera
from MvImport.MvErrorDefine_const import MV_OK
from util.util import decoding_char, to_hex_str


class CameraThread:
    def __init__(self, context: CameraContext):
        self.camera_context = context

        self.buf_lock = threading.Lock()
        self.buf_save_image = None
        self.buf_save_image_len = 0
        self.frame_info = MV_FRAME_OUT_INFO_EX()

        self.thread_handle = None
        self.thread_running = False
        self.thread_event = threading.Event()

    def start_thread(self):
        try:
            if not self.thread_running:
                self.thread_handle = threading.Thread(
                    target=CameraThread.work_thread, args=(self, self.thread_event)
                )
                self.thread_handle.start()
                self.thread_running = True
        except TypeError:
            raise CameraError("取图线程开启失败")

    def stop_thread(self):
        if self.thread_running and self.thread_handle != None:
            self.thread_event.set()
            self.thread_handle.join()
            self.thread_running = False

    def work_thread(self, exit_flag):
        stOutFrame = MV_FRAME_OUT()
        memset(byref(stOutFrame), 0, sizeof(stOutFrame))

        while not exit_flag.is_set():
            ret = self.camera_context.camera_obj.MV_CC_GetImageBuffer(stOutFrame, 1000)
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
                self.camera_context.camera_obj.MV_CC_FreeImageBuffer(stOutFrame)
            else:
                serial = decoding_char(
                    self.camera_context.device_info.SpecialInfo.stGigEInfo.chSerialNumber
                )
                log.debug(f"主动取图中，{serial}没有图像数据[{to_hex_str(ret)}]")
