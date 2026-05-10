/**
 * @file device_set_param.cpp
 * @brief 读码器网络与 GenICam 风格参数设置（GigE 改 IP、SetInt/Float/Bool/String 等）。
 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include <stdexcept>
#include <string>

namespace {

void checkSdkOk(int ok, const char *apiName) {
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error(std::string(apiName) + " error: " + toHexStr(ok));
    }
}

template <typename F>
void setParamOpened(const std::string &sn, const char *apiName, F &&f) {
    CodeReader *device = getDevice(sn, true);
    device->open();
    checkSdkOk(f(device->handle), apiName);
}

} // namespace

/**
 * 通过 GigE 强制写入设备 IP / 掩码 / 网关。
 *
 * 请注意：
 * 1. 设置 IP 时设备须处于可接受配置的状态（实现中会先 open）。
 * 2. 设置成功后设备通常会重启，故成功后 destroyDevice 释放本地句柄。
 *
 * @throws std::invalid_argument IP/掩码/网关格式非法
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
    CodeReader *device = getDevice(sn, true);
    device->open();
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
