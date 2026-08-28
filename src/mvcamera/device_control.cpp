/** 状态迁移、startCamera/stopCamera、起流前 TriggerMode/TriggerSource 写参 */

#include "MvCameraControl.h"
#include "camera.h"
#include "camera_detail.h"
#include <stdexcept>

namespace {

void openIfConnected(CameraDevice* d) {
    d->open();
}

void applyOpenParams(CameraDevice* d, const CameraOpenParams& p) {
    if (d->status != CameraStatus::Open) {
        throw std::logic_error("applyOpenParams: expect Open");
    }
    void* h = d->handle;
    if (!p.triggerMode.empty()) {
        checkSdk<MV_OK>(MV_CC_SetEnumValueByString(h, "TriggerMode", p.triggerMode.c_str()), "SetEnum(TriggerMode)");
    }
    if (!p.triggerSource.empty()) {
        checkSdk<MV_OK>(MV_CC_SetEnumValueByString(h, "TriggerSource", p.triggerSource.c_str()),
                 "SetEnum(TriggerSource)");
    }
    if (p.width > 0) {
        checkSdk<MV_OK>(MV_CC_SetIntValue(h, "Width", p.width), "SetInt(Width)");
    }
    if (p.height > 0) {
        checkSdk<MV_OK>(MV_CC_SetIntValue(h, "Height", p.height), "SetInt(Height)");
    }
}

} // namespace

void CameraDevice::open() {
    if (status == CameraStatus::Open) {
        return;
    }
    if (status == CameraStatus::Grabbing) {
        throw std::logic_error("CameraDevice::open: Grabbing 下请先 stopCamera");
    }
    // 关键：枚举 + CreateHandle + OpenDevice 必须在同一作用域。MVS 句柄引用枚举结果缓冲区，
    // 枚举作用域结束后再 OpenDevice 会报 0x80000206 网络错误。
    MV_CC_DEVICE_INFO_LIST list{};
    checkSdk<MV_OK>(MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &list), "MV_CC_EnumDevices");
    const MV_CC_DEVICE_INFO* devInfo = nullptr;
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
        if (sn == serialNumber) {
            devInfo = p;
            break;
        }
    }
    if (!devInfo) {
        throw std::runtime_error("camera not found by serial: " + serialNumber);
    }
    if (handle) {
        MV_CC_DestroyHandle(handle);
        handle = nullptr;
    }
    checkSdk<MV_OK>(MV_CC_CreateHandle(&handle, devInfo), "MV_CC_CreateHandle");
    checkSdk<MV_OK>(MV_CC_OpenDevice(handle, MV_ACCESS_Exclusive, 0), "MV_CC_OpenDevice");
    status = CameraStatus::Open;
}

void CameraDevice::grabbing() {
    if (status == CameraStatus::Grabbing) {
        return;
    }
    openIfConnected(this);
    if (status == CameraStatus::Open) {
        cameraInternalBindImageCallbackBeforeGrabbing(this);
        checkSdk<MV_OK>(MV_CC_StartGrabbing(handle), "MV_CC_StartGrabbing");
        status = CameraStatus::Grabbing;
    }
}

void CameraDevice::close() {
    if (status == CameraStatus::Connected) {
        return;
    }
    if (status == CameraStatus::Grabbing) {
        checkSdk<MV_OK>(MV_CC_StopGrabbing(handle), "MV_CC_StopGrabbing");
        status = CameraStatus::Open;
    }
    if (status == CameraStatus::Open) {
        checkSdk<MV_OK>(MV_CC_CloseDevice(handle), "MV_CC_CloseDevice");
        status = CameraStatus::Connected;
    }
    if (handle) {
        MV_CC_DestroyHandle(handle);
        handle = nullptr;
    }
}

void stopCamera(const std::string& sn) {
    CameraDevice* cam = findCamera(sn);
    if (cam) {
        cam->close();
    }
}

void startCamera(const std::string& sn, const CameraOpenParams& params,
                 const std::optional<CameraFrameCallback>& onFrame) {
    CameraDevice* cam = getOrCreateCamera(sn);
    const auto reg = [&] {
        if (onFrame) {
            registerFrameCallbackForSerial(sn, *onFrame);
        }
    };
    reg();
    if (cam->status == CameraStatus::Grabbing) {
        return;
    }
    if (cam->status == CameraStatus::Connected) {
        cam->open();  // open() 内创建句柄（与枚举同作用域）
        if (params.netTransMode != 0) {
            checkSdk<MV_OK>(MV_GIGE_SetNetTransMode(cam->handle, static_cast<unsigned int>(params.netTransMode)),
                     "MV_GIGE_SetNetTransMode");
        }
    }
    applyOpenParams(cam, params);
    cam->grabbing();
}
