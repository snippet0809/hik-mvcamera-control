/**
 * @file device_set_param.cpp
 * @brief GenICam 写参；取流中禁止。`applyCodeReaderOpenParams` 供起流路径在已 Open 句柄上批量写入。
 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <stdexcept>

static void checkSdkOk(int ok, const char *apiName) {
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error(std::string(apiName) + " error: " + toHexStr(ok));
    }
}

namespace {

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

void applyCodeReaderOpenParams(CodeReader *d, const CodeReaderOpenParams &params) {
    if (d == nullptr) {
        throw std::logic_error("applyCodeReaderOpenParams: null device");
    }
    if (d->status != CodeReaderStatus::Open) {
        throw std::logic_error("applyCodeReaderOpenParams: device must be Open");
    }
    void *h = d->handle;
    checkSdkOk(MV_CODEREADER_SetEnumValueByString(h, "TriggerMode", params.triggerMode.c_str()),
               "MV_CODEREADER_SetEnumValueByString(TriggerMode)");
    checkSdkOk(MV_CODEREADER_SetEnumValueByString(h, "TriggerSource", params.triggerSource.c_str()),
               "MV_CODEREADER_SetEnumValueByString(TriggerSource)");
    checkSdkOk(MV_CODEREADER_SetBoolValue(h, "CODE128", params.code128), "MV_CODEREADER_SetBoolValue(CODE128)");
    checkSdkOk(MV_CODEREADER_SetBoolValue(h, "QRCode", params.qrcode), "MV_CODEREADER_SetBoolValue(QRCode)");
}
