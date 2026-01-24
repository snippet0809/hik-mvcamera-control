#include "camera_ori.h"

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

    async def save_image(
        self, image_path_prefix: str, last_frame_no: int | None = None
    ):
        if self.camera_thread.buf_save_image == None:
            raise CameraError(f"{self.camera_context.serial_no}相机未检测到图片数据")
        # 获取缓存锁
        self.camera_thread.buf_lock.acquire()
        current_frame_no = self.camera_thread.frame_info.nFrameNum
        if last_frame_no == current_frame_no:
            self.camera_thread.buf_lock.release()
            raise CameraError(f"{self.camera_context.serial_no}相机最新帧数据还未更新")
        now = datetime.datetime.now().strftime("%y%m%d%H%M%S")
        image_name = (
            self.camera_context.serial_no + now + "_" + str(current_frame_no) + ".jpeg"
        )
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
