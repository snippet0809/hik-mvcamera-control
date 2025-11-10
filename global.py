from camera_exception import CameraError
from camera_operate import CameraOperate, decoding_char
from MvImport.MvCameraControl_class import *

camera_dict: dict[str, MvCamera] = dict()
camera_operate_list = []
camera_operate: CameraOperate


def get_device_list() -> list[str]:
    result = MvCamera.MV_CC_Initialize()
    if result != MV_OK:
        raise CameraError("相机初始化失败", result)
    deviceList = MV_CC_DEVICE_INFO_LIST()
    result = MvCamera.MV_CC_EnumDevices((MV_GIGE_DEVICE | MV_USB_DEVICE), deviceList)
    if result != MV_OK:
        raise CameraError("枚举设备失败", result)
    for i in range(deviceList.nDeviceNum):
        mvcc_dev_info = cast(
            deviceList.pDeviceInfo[i], POINTER(MV_CC_DEVICE_INFO)
        ).contents
        serial = decoding_char(mvcc_dev_info.SpecialInfo.stGigEInfo.chSerialNumber)
        global serial_index
        camera_dict.clear()
        camera_dict[serial] = MvCamera()
        global camera_operate
        camera_operate = CameraOperate(mvcc_dev_info)
        try:
            camera_operate.open_camera()
            camera_operate.start_grabbing()
        except CameraError as e:
            print(e.err_msg)
            print(e.err_code)
    return list(camera_dict.keys())


if __name__ == "__main__":
    camera_list = get_device_list()
    print("相机序列号列表：", camera_list)
    while True:
        continue
