/**
 * @file device_set_param.cpp
 * @brief GenICam 节点写参（setIntValue 等）；要求非取流；Connected 时会先 OpenDevice。
 */

#include "MvCodeReaderCtrl.h"
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
     * Grabbing 时抛 std::logic_error；Connected 时在本函数内调用 open()；无句柄时创建实例（需已掌握合法序列号）。
     */
    CodeReader *requireOpenForParameter(const std::string &sn, const char *caller) {
        CodeReader *d = getDevice(sn, true);
        if (d->status == CodeReaderStatus::Grabbing) {
            throw std::logic_error(std::string(caller) +
                                   "：当前为取流状态，无法设置参数；请先 stopDevice");
        }
        if (d->status == CodeReaderStatus::Connected) {
            d->open();
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
