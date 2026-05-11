/**
 * @file device_set_param.cpp
 * @brief GigE 改 IP（setIp）与 GenICam 节点写参（setIntValue 等）；均要求设备处于 Open（未取流）。
 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <stdexcept>

namespace {

void checkSdkOk(int ok, const char *apiName) {
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error(std::string(apiName) + " error: " + toHexStr(ok));
    }
}

/**
 * 参数设置要求设备处于 Open（已 OpenDevice、未取流）。
 * 未缓存 / Connected / Grabbing 均抛 std::logic_error，提示调用方先 openDeviceForParameters 或 stopDevice。
 */
CodeReader *requireOpenForParameter(const std::string &sn, const char *caller) {
    CodeReader *d = getDevice(sn, false);
    if (d == nullptr) {
        throw std::logic_error(std::string(caller) +
                               "：设备未在会话中，请先调用 openDeviceForParameters（需已枚举到该序列号）");
    }
    if (d->status == CodeReaderStatus::Grabbing) {
        throw std::logic_error(std::string(caller) +
                               "：当前为取流状态，无法设置参数；请先 stopDevice，再调用 openDeviceForParameters");
    }
    if (d->status == CodeReaderStatus::Connected) {
        throw std::logic_error(
            std::string(caller) + "：设备尚未打开；请先调用 openDeviceForParameters 进入 Open 状态后再试");
    }
    return d;
}

/** 在 Open 状态下调用 apiName 对应的 SDK Set*。 */
template <typename F>
void setParamOpened(const std::string &sn, const char *apiName, F &&f) {
    CodeReader *device = requireOpenForParameter(sn, apiName);
    checkSdkOk(f(device->handle), apiName);
}

} // namespace

/**
 * 通过 GigE 强制写入设备 IP / 掩码 / 网关。
 *
 * 请注意：
 * 1. 须先 openDeviceForParameters 使设备处于 Open（未取流）；取流中调用将抛异常。
 * 2. 设置成功后设备通常会重启，故成功后 destroyDevice 释放本地句柄。
 *
 * @throws std::invalid_argument IP/掩码/网关格式非法
 * @throws std::logic_error 设备未缓存、仍为 Connected 或正在 Grabbing 等非 Open 状态
 * @throws std::runtime_error MV_CODEREADER_GIGE_ForceIp 失败时抛出
 */
void setIp(const std::string &sn, const std::string &ip, const std::string &mask, const std::string &gateway) {
    unsigned int ipHost = 0;
    unsigned int maskHost = 0;
    unsigned int gwHost = 0;
    if (!tryParseIpv4HostOrder(ip, ipHost) || !tryParseIpv4HostOrder(mask, maskHost) ||
        !tryParseIpv4HostOrder(gateway, gwHost)) {
        throw std::invalid_argument("setIp: invalid IPv4 address, mask, or gateway");
    }
    CodeReader *device = requireOpenForParameter(sn, "setIp");
    checkSdkOk(MV_CODEREADER_GIGE_ForceIp(device->handle, ipHost, maskHost, gwHost), "MV_CODEREADER_GIGE_ForceIp");
    destroyDevice(sn);
}

void setIntValue(const std::string &sn, const std::string &key, int value) {
    setParamOpened(sn, "MV_CODEREADER_SetIntValue", [&](void *h) {
        return MV_CODEREADER_SetIntValue(h, key.c_str(), value);
    });
}

void setFloatValue(const std::string &sn, const std::string &key, float value) {
    setParamOpened(sn, "MV_CODEREADER_SetFloatValue", [&](void *h) {
        return MV_CODEREADER_SetFloatValue(h, key.c_str(), value);
    });
}

void setBoolValue(const std::string &sn, const std::string &key, bool value) {
    setParamOpened(sn, "MV_CODEREADER_SetBoolValue", [&](void *h) {
        return MV_CODEREADER_SetBoolValue(h, key.c_str(), value);
    });
}

void setStringValue(const std::string &sn, const std::string &key, const std::string &value) {
    setParamOpened(sn, "MV_CODEREADER_SetStringValue", [&](void *h) {
        return MV_CODEREADER_SetStringValue(h, key.c_str(), value.c_str());
    });
}

void setEnumValue(const std::string &sn, const std::string &key, unsigned int value) {
    setParamOpened(sn, "MV_CODEREADER_SetEnumValue", [&](void *h) {
        return MV_CODEREADER_SetEnumValue(h, key.c_str(), value);
    });
}

void setEnumValueByString(const std::string &sn, const std::string &key, const std::string &symbolic) {
    setParamOpened(sn, "MV_CODEREADER_SetEnumValueByString", [&](void *h) {
        return MV_CODEREADER_SetEnumValueByString(h, key.c_str(), symbolic.c_str());
    });
}
