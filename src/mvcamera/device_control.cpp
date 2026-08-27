/** 状态迁移、startCamera/stopCamera、起流前 TriggerMode/TriggerSource 写参 */

#include "MvCameraControl.h"
#include "camera.h"
#include "camera_detail.h"
#include <stdexcept>

namespace {

void openIfConnected(CameraDevice* d) {
    if (d->status != CameraStatus::Connected) {
        return;
    }
    checkSdk<MV_OK>(MV_CC_OpenDevice(d->handle, MV_ACCESS_Exclusive, 0), "MV_CC_OpenDevice");
    d->status = CameraStatus::Open;
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
    openIfConnected(this);
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
        if (params.netTransMode != 0) {
            checkSdk<MV_OK>(MV_GIGE_SetNetTransMode(cam->handle, static_cast<unsigned int>(params.netTransMode)),
                     "MV_GIGE_SetNetTransMode");
        }
        cam->open();
    }
    applyOpenParams(cam, params);
    cam->grabbing();
}
