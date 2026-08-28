/** 枚举（GigE+USB）、按序列号缓存 CameraDevice；状态机见 device_control.cpp */

#include "MvCameraControl.h"
#include "camera.h"
#include "camera_detail.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unordered_map>

std::unordered_map<std::string, std::shared_ptr<CameraDevice>> cameraMap;

namespace {

unsigned int ipToUint(const std::string& s) {
    unsigned int a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(s.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        throw std::invalid_argument("invalid IP: " + s);
    }
    return (a << 24) | (b << 16) | (c << 8) | d;
}

const MV_CC_DEVICE_INFO* findDeviceInfoBySerial(const std::string& serial) {
    MV_CC_DEVICE_INFO_LIST list{};
    checkSdk<MV_OK>(MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &list), "MV_CC_EnumDevices");
    for (unsigned i = 0; i < list.nDeviceNum; ++i) {
        const MV_CC_DEVICE_INFO* p = list.pDeviceInfo[i];
        if (!p) {
            continue;
        }
        std::string sn;
        if (p->nTLayerType == MV_GIGE_DEVICE) {
            sn = bytesToStr(p->SpecialInfo.stGigEInfo.chSerialNumber,
                            sizeof(p->SpecialInfo.stGigEInfo.chSerialNumber));
        } else if (p->nTLayerType == MV_USB_DEVICE) {
            sn = bytesToStr(p->SpecialInfo.stUsb3VInfo.chSerialNumber,
                            sizeof(p->SpecialInfo.stUsb3VInfo.chSerialNumber));
        } else {
            continue;
        }
        if (sn == serial) {
            return p;
        }
    }
    return nullptr;
}

} // namespace

CameraDevice::CameraDevice(const std::string& serialNumber)
    : serialNumber(serialNumber), handle(nullptr), status(CameraStatus::Connected) {
    // 仅校验设备在线；句柄在 open() 内与枚举同作用域创建（避免 MVS 悬垂引用导致 OpenDevice 0x80000206）
    if (!findDeviceInfoBySerial(serialNumber)) {
        throw std::runtime_error("camera not found by serial: " + serialNumber);
    }
}

CameraDevice::~CameraDevice() {
    if (!handle) {
        return;
    }
    try {
        close();
    } catch (...) {
    }
    if (handle) {
        MV_CC_DestroyHandle(handle);
        handle = nullptr;
    }
}

std::vector<CameraInfo> enumCamera() {
    MV_CC_DEVICE_INFO_LIST list{};
    checkSdk<MV_OK>(MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &list), "MV_CC_EnumDevices");
    std::vector<CameraInfo> infos;
    for (unsigned i = 0; i < list.nDeviceNum; ++i) {
        const MV_CC_DEVICE_INFO* p = list.pDeviceInfo[i];
        if (!p) {
            continue;
        }
        CameraInfo info;
        if (p->nTLayerType == MV_GIGE_DEVICE) {
            info.serialNumber = bytesToStr(p->SpecialInfo.stGigEInfo.chSerialNumber,
                                           sizeof(p->SpecialInfo.stGigEInfo.chSerialNumber));
            info.netExportIp = intToIp(p->SpecialInfo.stGigEInfo.nCurrentIp);
            info.modelName = bytesToStr(p->SpecialInfo.stGigEInfo.chModelName,
                                        sizeof(p->SpecialInfo.stGigEInfo.chModelName));
        } else if (p->nTLayerType == MV_USB_DEVICE) {
            info.serialNumber = bytesToStr(p->SpecialInfo.stUsb3VInfo.chSerialNumber,
                                           sizeof(p->SpecialInfo.stUsb3VInfo.chSerialNumber));
            info.netExportIp.clear();  // USB 无 IP
            info.modelName = bytesToStr(p->SpecialInfo.stUsb3VInfo.chModelName,
                                        sizeof(p->SpecialInfo.stUsb3VInfo.chModelName));
        } else {
            continue;
        }
        infos.push_back(std::move(info));
    }
    return infos;
}

void forceCameraIp(const std::string& sn, const std::string& ip, const std::string& subnetMask,
                   const std::string& gateway) {
    // 枚举 + CreateHandle 同作用域（findDeviceInfoBySerial 返回的指针随局部 list 失效，不可跨作用域 CreateHandle）
    MV_CC_DEVICE_INFO_LIST list{};
    checkSdk<MV_OK>(MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &list), "MV_CC_EnumDevices");
    const MV_CC_DEVICE_INFO* devInfo = nullptr;
    for (unsigned i = 0; i < list.nDeviceNum; ++i) {
        const MV_CC_DEVICE_INFO* p = list.pDeviceInfo[i];
        if (!p) {
            continue;
        }
        std::string s;
        if (p->nTLayerType == MV_GIGE_DEVICE) {
            s = bytesToStr(p->SpecialInfo.stGigEInfo.chSerialNumber,
                           sizeof(p->SpecialInfo.stGigEInfo.chSerialNumber));
        } else if (p->nTLayerType == MV_USB_DEVICE) {
            s = bytesToStr(p->SpecialInfo.stUsb3VInfo.chSerialNumber,
                           sizeof(p->SpecialInfo.stUsb3VInfo.chSerialNumber));
        } else {
            continue;
        }
        if (s == sn) {
            devInfo = p;
            break;
        }
    }
    if (!devInfo) {
        throw std::runtime_error("camera not found by serial: " + sn);
    }
    void* h = nullptr;
    checkSdk<MV_OK>(MV_CC_CreateHandle(&h, devInfo), "MV_CC_CreateHandle");
    try {
        checkSdk<MV_OK>(MV_GIGE_ForceIpEx(h, ipToUint(ip), ipToUint(subnetMask), ipToUint(gateway)),
                 "MV_GIGE_ForceIpEx");
    } catch (...) {
        MV_CC_DestroyHandle(h);
        throw;
    }
    MV_CC_DestroyHandle(h);
}

CameraDevice* findCamera(const std::string& sn) {
    const auto it = cameraMap.find(sn);
    return it == cameraMap.end() ? nullptr : it->second.get();
}

CameraDevice* getOrCreateCamera(const std::string& sn) {
    const auto it = cameraMap.find(sn);
    if (it != cameraMap.end()) {
        return it->second.get();
    }
    return cameraMap.emplace(sn, std::make_shared<CameraDevice>(sn)).first->second.get();
}
