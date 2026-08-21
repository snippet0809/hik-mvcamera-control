/** 按 GenICam 节点名读写参数：按类型分发到 MV_CODEREADER_Set/Get*Value */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace {

// 调用方须已持有 g_device_mutex。
CodeReader *paramDevice(const std::string &sn) {
    CodeReader *d = findDevice(sn);
    if (!d || !d->handle || d->handleStale) {
        throw std::logic_error("param: 设备未 startDevice");
    }
    return d;
}

} // namespace

void setReaderParam(const std::string &sn, const std::string &name, const CodeReaderParamValue &value) {
    std::lock_guard<std::mutex> lock(g_device_mutex);
    CodeReader *d = paramDevice(sn);
    void *h = d->handle;

    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                // 部分读码器节点（如 ExposureTime/Gain）是 Float 类型：用 Int 接口写会报错，
                // 失败时回退到 Float 接口（两者皆失败时报回 Int 错误）。
                int r = MV_CODEREADER_SetIntValue(h, name.c_str(), v);
                if (r != MV_CODEREADER_OK) {
                    const int r2 = MV_CODEREADER_SetFloatValue(h, name.c_str(), static_cast<float>(v));
                    if (r2 != MV_CODEREADER_OK) {
                        throw std::runtime_error(std::string("SetInt(" + name + ") error: ") + toHexStr(r));
                    }
                }
            } else if constexpr (std::is_same_v<T, double>) {
                checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_SetFloatValue(h, name.c_str(), static_cast<float>(v)),
                                           ("SetFloat(" + name + ")").c_str());
            } else if constexpr (std::is_same_v<T, bool>) {
                checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_SetBoolValue(h, name.c_str(), v),
                                           ("SetBool(" + name + ")").c_str());
            } else if constexpr (std::is_same_v<T, uint32_t>) {
                checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_SetEnumValue(h, name.c_str(), v),
                                           ("SetEnum(" + name + ")").c_str());
            } else if constexpr (std::is_same_v<T, std::string>) {
                // 字符串值：优先按枚举 symbolic 设置，失败回退为字符串节点
                int r = MV_CODEREADER_SetEnumValueByString(h, name.c_str(), v.c_str());
                if (r != MV_CODEREADER_OK) {
                    checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_SetStringValue(h, name.c_str(), v.c_str()),
                                               ("SetString(" + name + ")").c_str());
                }
            }
        },
        value);
}

void runReaderCommand(const std::string &sn, const std::string &name) {
    std::lock_guard<std::mutex> lock(g_device_mutex);
    CodeReader *d = paramDevice(sn);
    checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_SetCommandValue(d->handle, name.c_str()),
                               ("SetCommand(" + name + ")").c_str());
}

CodeReaderParamValue getReaderParam(const std::string &sn, const std::string &name) {
    std::lock_guard<std::mutex> lock(g_device_mutex);
    CodeReader *d = paramDevice(sn);
    void *h = d->handle;

    // 按类型依次尝试（SDK 对类型不匹配的节点返回错误码）
    bool b = false;
    int r = MV_CODEREADER_GetBoolValue(h, name.c_str(), &b);
    if (r == MV_CODEREADER_OK) {
        return b;
    }
    MV_CODEREADER_INTVALUE_EX iv{};
    r = MV_CODEREADER_GetIntValue(h, name.c_str(), &iv);
    if (r == MV_CODEREADER_OK) {
        return static_cast<int64_t>(iv.nCurValue);
    }
    MV_CODEREADER_FLOATVALUE fv{};
    r = MV_CODEREADER_GetFloatValue(h, name.c_str(), &fv);
    if (r == MV_CODEREADER_OK) {
        return static_cast<double>(fv.fCurValue);
    }
    MV_CODEREADER_ENUMVALUE ev{};
    r = MV_CODEREADER_GetEnumValue(h, name.c_str(), &ev);
    if (r == MV_CODEREADER_OK) {
        return static_cast<uint32_t>(ev.nCurValue);
    }
    MV_CODEREADER_STRINGVALUE sv{};
    r = MV_CODEREADER_GetStringValue(h, name.c_str(), &sv);
    if (r == MV_CODEREADER_OK) {
        return std::string(sv.chCurValue, strnlen(sv.chCurValue, sizeof(sv.chCurValue)));
    }
    throw std::runtime_error("getReaderParam: no type handler for node: " + name);
}
