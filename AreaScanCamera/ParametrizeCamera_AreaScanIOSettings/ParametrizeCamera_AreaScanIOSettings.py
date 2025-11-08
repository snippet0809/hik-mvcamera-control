# -- coding: utf-8 --

import sys
import msvcrt
import os
sys.path.append(os.getenv('MVCAM_COMMON_RUNENV') + "/Samples/Python/MvImport")
from MvCameraControl_class import *


winfun_ctype = WINFUNCTYPE

stFrameInfo = POINTER(MV_FRAME_OUT_INFO_EX)
pData = POINTER(c_ubyte)
FrameInfoCallBack = winfun_ctype(None, pData, stFrameInfo, c_void_p)

def image_callback(pData, pFrameInfo, pUser):
        stFrameInfo = cast(pFrameInfo, POINTER(MV_FRAME_OUT_INFO_EX)).contents
        if stFrameInfo:
            print("get one frame: Width[%d], Height[%d], nFrameNum[%d]" % (stFrameInfo.nWidth, stFrameInfo.nHeight, stFrameInfo.nFrameNum))

CALL_BACK_FUN = FrameInfoCallBack(image_callback)

if __name__ == "__main__":

    try:
        # ch:初始化SDK | en: initialize SDK
        MvCamera.MV_CC_Initialize()

        deviceList = MV_CC_DEVICE_INFO_LIST()
        tlayerType = (MV_GIGE_DEVICE | MV_USB_DEVICE | MV_GENTL_CAMERALINK_DEVICE
                      | MV_GENTL_CXP_DEVICE | MV_GENTL_XOF_DEVICE)

        # ch:枚举设备 | en:Enum device
        ret = MvCamera.MV_CC_EnumDevices(tlayerType, deviceList)
        if ret != 0:
            print("enum devices fail! ret[0x%x]" % ret)
            sys.exit()

        if deviceList.nDeviceNum == 0:
            print("find no device!")
            sys.exit()

        print("Find %d devices!" % deviceList.nDeviceNum)

        for i in range(0, deviceList.nDeviceNum):
            mvcc_dev_info = cast(deviceList.pDeviceInfo[i], POINTER(MV_CC_DEVICE_INFO)).contents
            if mvcc_dev_info.nTLayerType == MV_GIGE_DEVICE or mvcc_dev_info.nTLayerType == MV_GENTL_GIGE_DEVICE:
                print("\ngige device: [%d]" % i)
                strModeName = ""
                for per in mvcc_dev_info.SpecialInfo.stGigEInfo.chModelName:
                    if per == 0:
                        break
                    strModeName = strModeName + chr(per)
                print("device model name: %s" % strModeName)

                nip1 = ((mvcc_dev_info.SpecialInfo.stGigEInfo.nCurrentIp & 0xff000000) >> 24)
                nip2 = ((mvcc_dev_info.SpecialInfo.stGigEInfo.nCurrentIp & 0x00ff0000) >> 16)
                nip3 = ((mvcc_dev_info.SpecialInfo.stGigEInfo.nCurrentIp & 0x0000ff00) >> 8)
                nip4 = (mvcc_dev_info.SpecialInfo.stGigEInfo.nCurrentIp & 0x000000ff)
                print("current ip: %d.%d.%d.%d\n" % (nip1, nip2, nip3, nip4))
            elif mvcc_dev_info.nTLayerType == MV_USB_DEVICE:
                print("\nu3v device: [%d]" % i)
                strModeName = ""
                for per in mvcc_dev_info.SpecialInfo.stUsb3VInfo.chModelName:
                    if per == 0:
                        break
                    strModeName = strModeName + chr(per)
                print("device model name: %s" % strModeName)

                strSerialNumber = ""
                for per in mvcc_dev_info.SpecialInfo.stUsb3VInfo.chSerialNumber:
                    if per == 0:
                        break
                    strSerialNumber = strSerialNumber + chr(per)
                print("user serial number: %s" % strSerialNumber)
            elif mvcc_dev_info.nTLayerType == MV_GENTL_CAMERALINK_DEVICE:
                print("\nCML device: [%d]" % i)
                strModeName = ""
                for per in mvcc_dev_info.SpecialInfo.stCMLInfo.chModelName:
                    if per == 0:
                        break
                    strModeName = strModeName + chr(per)
                print("device model name: %s" % strModeName)

                strSerialNumber = ""
                for per in mvcc_dev_info.SpecialInfo.stCMLInfo.chSerialNumber:
                    if per == 0:
                        break
                    strSerialNumber = strSerialNumber + chr(per)
                print("user serial number: %s" % strSerialNumber)
            elif mvcc_dev_info.nTLayerType == MV_GENTL_CXP_DEVICE:
                print("\nCXP device: [%d]" % i)
                strModeName = ""
                for per in mvcc_dev_info.SpecialInfo.stCXPInfo.chModelName:
                    if per == 0:
                        break
                    strModeName = strModeName + chr(per)
                print("device model name: %s" % strModeName)

                strSerialNumber = ""
                for per in mvcc_dev_info.SpecialInfo.stCXPInfo.chSerialNumber:
                    if per == 0:
                        break
                    strSerialNumber = strSerialNumber + chr(per)
                print("user serial number: %s" % strSerialNumber)
            elif mvcc_dev_info.nTLayerType == MV_GENTL_XOF_DEVICE:
                print("\nXoF device: [%d]" % i)
                strModeName = ""
                for per in mvcc_dev_info.SpecialInfo.stXoFInfo.chModelName:
                    if per == 0:
                        break
                    strModeName = strModeName + chr(per)
                print("device model name: %s" % strModeName)

                strSerialNumber = ""
                for per in mvcc_dev_info.SpecialInfo.stXoFInfo.chSerialNumber:
                    if per == 0:
                        break
                    strSerialNumber = strSerialNumber + chr(per)
                print("user serial number: %s" % strSerialNumber)

        nConnectionNum = input("please input the number of the device to connect:")

        if int(nConnectionNum) >= deviceList.nDeviceNum:
            print("input error!")
            sys.exit()

        # ch:创建相机实例 | en:Create Camera Object
        cam = MvCamera()

        # ch:选择设备并创建句柄 | en:Select device and create handle
        stDeviceList = cast(deviceList.pDeviceInfo[int(nConnectionNum)], POINTER(MV_CC_DEVICE_INFO)).contents

        ret = cam.MV_CC_CreateHandle(stDeviceList)
        if ret != 0:
            raise Exception("create handle fail! ret[0x%x]" % ret)

        # ch:打开设备 | en:Open device
        ret = cam.MV_CC_OpenDevice(MV_ACCESS_Exclusive, 0)
        if ret != 0:
            raise Exception("open device fail! ret[0x%x]" % ret)

        # ch:探测网络最佳包大小(只对GigE相机有效) | en:Detection network optimal package size(It only works for the GigE camera)
        if stDeviceList.nTLayerType == MV_GIGE_DEVICE or stDeviceList.nTLayerType == MV_GENTL_GIGE_DEVICE:
            nPacketSize = cam.MV_CC_GetOptimalPacketSize()
            if int(nPacketSize) > 0:
                ret = cam.MV_CC_SetIntValue("GevSCPSPacketSize", nPacketSize)
                if ret != 0:
                    print("Warning: Set Packet Size fail! ret[0x%x]" % ret)
            else:
                print("Warning: Get Packet Size fail! ret[0x%x]" % nPacketSize)

        # 以下设置Line0 IO输入, 也可以选择Line2作为输入, Line2延迟更低
        print("Now set IO input...")
        # ch:设置触发模式为on | en:Set trigger mode as on
        ret = cam.MV_CC_SetEnumValueByString("TriggerMode", "On")
        if ret != 0:
            raise Exception("set trigger mode on fail! ret[0x%x]" % ret)

        # ch:设置触发源为Line0 | en:Set trigger source as Line0
        ret = cam.MV_CC_SetEnumValueByString("TriggerSource", "Line0")
        if ret != 0:
            raise Exception("set trigger source fail! ret[0x%x]" % ret)

        # ch:设置触发沿为RisingEdge | en: Set TriggerActivation as RisingEdge
        ret = cam.MV_CC_SetEnumValueByString("TriggerActivation", "RisingEdge")
        if ret != 0:
            raise Exception("set trigger activation fail! ret[0x%x]" % ret)

        # ch:设置触发延迟 | en: Set TriggerDelay
        ret = cam.MV_CC_SetFloatValue("TriggerDelay", 0)
        if ret != 0:
            raise Exception("set trigger delay fail! ret[0x%x]" % ret)

        # ch:关闭触发缓存、如需要可开启 | en: Turn off TriggerCacheEnable
        ret = cam.MV_CC_SetBoolValue("TriggerCacheEnable", False)
        if ret != 0:
            raise Exception("set trigger cache enable fail! ret[0x%x]" % ret)

        # ch:切换LineSelector 为Line0 | en: Set LineSelector as Line0
        ret = cam.MV_CC_SetEnumValueByString("LineSelector", "Line0")
        if ret != 0:
            raise Exception("set line selector fail! ret[0x%x]" % ret)

        # ch:设置Line0 滤波时间(us)、误触发可适当加大 | Set Line0 LineDebouncerTime(us)
        ret = cam.MV_CC_SetIntValueEx("LineDebouncerTime", 50)
        if ret != 0:
            raise Exception("set line debouncer time fail! ret[0x%x]" % ret)

        # 以下设置Line1 IO输出、也可选择Line2作为输出、Line2 延迟更低；用于控制外部光源等设备
        print("Now set IO output...")
        # ch:切换LineSelector 为Line1 | en:Set LineSelector as Line1
        ret = cam.MV_CC_SetEnumValueByString("LineSelector", "Line1")
        if ret != 0:
            raise Exception("set line selector fail! ret[0x%x]" % ret)

        # ch:输出源选择曝光开始 |en:Set LineSource as ExposureStartActive
        ret = cam.MV_CC_SetEnumValueByString("LineSource", "ExposureStartActive")
        if ret != 0:
            raise Exception("set line source fail! ret[0x%x]" % ret)

        # ch:开启输出使能| en: Turn on StrobeEnable
        ret = cam.MV_CC_SetBoolValue("StrobeEnable", True)
        if ret != 0:
            raise Exception("set strobe enable fail! ret[0x%x]" % ret)

        # ch:设置输出线路持续时间（us） | en: Set StrobeLineDuration(us)
        ret = cam.MV_CC_SetIntValueEx("StrobeLineDuration", 0)
        if ret != 0:
            raise Exception("set strobe line duration fail! ret[0x%x]" % ret)

        # ch:设置输出线路延迟（us） | en: Set StrobeLineDelay(us)
        ret = cam.MV_CC_SetIntValueEx("StrobeLineDelay", 0)
        if ret != 0:
            raise Exception("set strobe line delay fail! ret[0x%x]" % ret)

        # ch:输出线路预延迟（us） | en: Set StrobeLinePreDelay(us)
        ret = cam.MV_CC_SetIntValueEx("StrobeLinePreDelay", 0)
        if ret != 0:
            raise Exception("set strobe line pre-delay fail! ret[0x%x]" % ret)

        # ch:注册抓图回调 | en:Register image callback
        ret = cam.MV_CC_RegisterImageCallBackEx(CALL_BACK_FUN, None)
        if ret != 0:
            raise Exception("register image callback fail! ret[0x%x]" % ret)

        # ch:开始取流 | en:Start grab image
        ret = cam.MV_CC_StartGrabbing()
        if ret != 0:
            raise Exception("start grabbing fail! ret[0x%x]" % ret)
        else:
            print("start grabbing success")

        print("press a key to exit.")
        msvcrt.getch()

        # ch:停止取流 | en:Stop grab image
        ret = cam.MV_CC_StopGrabbing()
        if ret != 0:
            raise Exception("stop grabbing fail! ret[0x%x]" % ret)

        # ch:关闭设备 | Close device
        ret = cam.MV_CC_CloseDevice()
        if ret != 0:
            raise Exception("close device fail! ret[0x%x]" % ret)

        # ch:销毁句柄 | Destroy handle0
        cam.MV_CC_DestroyHandle()

    except Exception as e:
        print(e)
        cam.MV_CC_CloseDevice()
        cam.MV_CC_DestroyHandle()
    finally:
        # ch:反初始化SDK | en: finalize SDK
        MvCamera.MV_CC_Finalize()
