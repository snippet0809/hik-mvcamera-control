#pragma once

/** 实现侧类型；不随 camera.h 对外暴露。 */

#include "camera.h"

#include "../common/sdk_util.h"

#include <cstdint>
#include <string>

enum class CameraStatus { Connected, Open, Grabbing };

class CameraDevice {
public:
    std::string serialNumber;
    void* handle;
    CameraStatus status;

    explicit CameraDevice(const std::string& serialNumber);
    ~CameraDevice();
    void open();
    void grabbing();
    void close();
};

CameraDevice* findCamera(const std::string& sn);
CameraDevice* getOrCreateCamera(const std::string& sn);

/** 起流前绑定图像回调（注册表有回调则绑定 __stdcall 桥，否则解绑）。 */
void cameraInternalBindImageCallbackBeforeGrabbing(CameraDevice* device);
void registerFrameCallbackForSerial(const std::string& sn, const CameraFrameCallback& callback);
