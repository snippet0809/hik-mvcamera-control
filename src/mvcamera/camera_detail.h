#pragma once

/** 实现侧类型；不随 camera.h 对外暴露。 */

#include "camera.h"

#include <cstdint>
#include <sstream>
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

inline std::string toHexStr(int value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << static_cast<std::uint32_t>(value);
    return ss.str();
}
