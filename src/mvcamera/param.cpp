/** 按 GenICam 节点名读写参数：按类型分发到 MV_CC_Set/Get*Value */

#include "MvCameraControl.h"
#include "camera.h"
#include "camera_detail.h"
#include <stdexcept>
#include <type_traits>

namespace {

} // namespace

void setCameraParam(const std::string& sn, const std::string& name, const CamParamValue& value) {
    CameraDevice* d = findCamera(sn);
    if (!d || !d->handle) {
        throw std::logic_error("setCameraParam: 设备未 startCamera");
    }
    void* h = d->handle;

    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                // 部分相机节点（如 ExposureTime/Gain）是 Float 类型：用 Int 接口写会报
                // MV_E_GC_GENERIC(0x80000100)，失败时回退到 Float 接口（两者皆失败时报回 Int 错误）。
                int r = MV_CC_SetIntValueEx(h, name.c_str(), v);
                if (r != MV_OK) {
                    const int r2 = MV_CC_SetFloatValue(h, name.c_str(), static_cast<float>(v));
                    if (r2 != MV_OK) {
                        throw std::runtime_error(std::string("SetInt(" + name + ") error: ") + toHexStr(r));
                    }
                }
            } else if constexpr (std::is_same_v<T, double>) {
                checkSdk<MV_OK>(MV_CC_SetFloatValue(h, name.c_str(), static_cast<float>(v)),
                         ("SetFloat(" + name + ")").c_str());
            } else if constexpr (std::is_same_v<T, bool>) {
                checkSdk<MV_OK>(MV_CC_SetBoolValue(h, name.c_str(), v), ("SetBool(" + name + ")").c_str());
            } else if constexpr (std::is_same_v<T, uint32_t>) {
                checkSdk<MV_OK>(MV_CC_SetEnumValue(h, name.c_str(), v), ("SetEnum(" + name + ")").c_str());
            } else if constexpr (std::is_same_v<T, std::string>) {
                // 字符串值：优先按枚举 symbolic 设置，失败回退为字符串节点
                int r = MV_CC_SetEnumValueByString(h, name.c_str(), v.c_str());
                if (r != MV_OK) {
                    checkSdk<MV_OK>(MV_CC_SetStringValue(h, name.c_str(), v.c_str()),
                             ("SetString(" + name + ")").c_str());
                }
            }
        },
        value);
}

void runCameraCommand(const std::string& sn, const std::string& name) {
    CameraDevice* d = findCamera(sn);
    if (!d || !d->handle) {
        throw std::logic_error("runCameraCommand: 设备未 startCamera");
    }
    checkSdk<MV_OK>(MV_CC_SetCommandValue(d->handle, name.c_str()), ("SetCommand(" + name + ")").c_str());
}

CamParamValue getCameraParam(const std::string& sn, const std::string& name) {
    CameraDevice* d = findCamera(sn);
    if (!d || !d->handle) {
        throw std::logic_error("getCameraParam: 设备未 startCamera");
    }
    void* h = d->handle;

    // 按类型依次尝试（SDK 对类型不匹配的节点返回错误码）
    bool b = false;
    int r = MV_CC_GetBoolValue(h, name.c_str(), &b);
    if (r == MV_OK) {
        return b;
    }
    MVCC_INTVALUE_EX iv{};
    r = MV_CC_GetIntValueEx(h, name.c_str(), &iv);
    if (r == MV_OK) {
        return static_cast<int64_t>(iv.nCurValue);
    }
    MVCC_FLOATVALUE fv{};
    r = MV_CC_GetFloatValue(h, name.c_str(), &fv);
    if (r == MV_OK) {
        return static_cast<double>(fv.fCurValue);
    }
    MVCC_ENUMVALUE ev{};
    r = MV_CC_GetEnumValue(h, name.c_str(), &ev);
    if (r == MV_OK) {
        return static_cast<uint32_t>(ev.nCurValue);
    }
    MVCC_STRINGVALUE sv{};
    r = MV_CC_GetStringValue(h, name.c_str(), &sv);
    if (r == MV_OK) {
        return std::string(sv.chCurValue);
    }
    throw std::runtime_error("getCameraParam: no type handler for node: " + name);
}
