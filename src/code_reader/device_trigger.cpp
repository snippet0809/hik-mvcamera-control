/**
 * @file device_trigger.cpp
 * @brief 图像回调注册与软触发：对接 MV_CODEREADER_RegisterImageCallBack、SetCommandValue(TriggerSoftware)。
 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

    /** 软触发命令节点名（GenICam 常见命名，具体以设备 XML 为准）。 */
    constexpr const char *kTriggerSoftware = "TriggerSoftware";

    using BcrCodes = std::vector<std::string>;
    using BcrUserCb = std::function<void(BcrCodes)>;

    /** 序列号与回调分两列存，避免部分 Clang 对 ``pair<string, function<...>>`` 的 constexpr 诊断。 */
    std::vector<std::string> g_callbackSerials;
    std::vector<BcrUserCb> g_callbackFuncs;

    int findSerialIndex(const std::string &sn) {
        const auto it = std::find(g_callbackSerials.begin(), g_callbackSerials.end(), sn);
        if (it == g_callbackSerials.end()) {
            return -1;
        }
        return static_cast<int>(it - g_callbackSerials.begin());
    }

    /**
     * 从 MV_CODEREADER_IMAGE_OUT_INFO 中解析 BCR 结果，得到条码字符串列表。
     * 假定 nResultType 已为 BCR；chResult 前 sizeof(MV_CODEREADER_RESULT_BCR) 字节为有效载荷。
     */
    std::vector<std::string> extractBcrStrings(const MV_CODEREADER_IMAGE_OUT_INFO &info) {
        std::vector<std::string> out;
        if (info.nResultType != CodeReader_ResType_BCR) {
            return out;
        }
        MV_CODEREADER_RESULT_BCR bcr{};
        constexpr std::size_t kBcrSize = sizeof(MV_CODEREADER_RESULT_BCR);
        static_assert(kBcrSize <= MV_CODEREADER_MAX_RESULT_SIZE, "RESULT_BCR must fit in chResult");
        std::memcpy(&bcr, info.chResult, kBcrSize);
        unsigned int n = bcr.nCodeNum;
        if (n > MAX_CODEREADER_BCR_COUNT) {
            n = MAX_CODEREADER_BCR_COUNT;
        }
        out.reserve(n);
        for (unsigned int i = 0; i < n; ++i) {
            const MV_CODEREADER_BCR_INFO &bi = bcr.stBcrInfo[i];
            const std::size_t cap = sizeof(bi.chCode);
            // nLen 与缓冲区取较小有效长度，避免越界或未终止字符串
            std::size_t len = bi.nLen;
            if (len >= cap) {
                len = cap - 1;
            }
            const std::size_t z = strnlen(bi.chCode, cap);
            if (len > z) {
                len = z;
            }
            out.emplace_back(bi.chCode, len);
        }
        return out;
    }

    /**
     * SDK 要求的 __stdcall 图像回调桥接函数。
     * pUser 为 CodeReader*，用于按序列号查找用户回调；未注册则静默返回。
     */
    void __stdcall sdkImageCallbackBridge([[maybe_unused]] unsigned char *pData, MV_CODEREADER_IMAGE_OUT_INFO *pstFrameInfo,
                                          void *pUser) {
        if (pstFrameInfo == nullptr || pUser == nullptr) {
            return;
        }
        if (!pstFrameInfo->bIsGetCode || pstFrameInfo->nResultType != CodeReader_ResType_BCR) {
            return;
        }
        auto *device = static_cast<CodeReader *>(pUser);
        std::vector<std::string> codes = extractBcrStrings(*pstFrameInfo);
        const int idx = findSerialIndex(device->serialNumber);
        if (idx < 0 || !g_callbackFuncs[static_cast<std::size_t>(idx)]) {
            return;
        }
        BcrUserCb userCb = g_callbackFuncs[static_cast<std::size_t>(idx)];
        userCb(std::move(codes));
    }

} // namespace

void registerImageCallbackForSerial(const std::string &sn,
                                    const std::function<void(std::vector<std::string>)> &callback) {
    const int idx = findSerialIndex(sn);
    if (callback) {
        if (idx >= 0) {
            g_callbackFuncs[static_cast<std::size_t>(idx)] = callback;
        } else {
            g_callbackSerials.push_back(sn);
            g_callbackFuncs.push_back(callback);
        }
    } else if (idx >= 0) {
        const auto i = static_cast<std::size_t>(idx);
        g_callbackSerials.erase(g_callbackSerials.begin() + static_cast<std::ptrdiff_t>(i));
        g_callbackFuncs.erase(g_callbackFuncs.begin() + static_cast<std::ptrdiff_t>(i));
    }
    CodeReader *d = getDevice(sn, false);
    if (d != nullptr && d->status == CodeReaderStatus::Grabbing) {
        codeReaderInternalBindImageCallbackBeforeGrabbing(d);
    }
}

/**
 * 在 MV_CODEREADER_StartGrabbing 之前（或取流中刷新），按序列号把 SDK 图像回调绑定到桥接函数。
 * 无该序列号用户回调时向 SDK 传 nullptr，等价于不向用户派发读码结果。
 */
void codeReaderInternalBindImageCallbackBeforeGrabbing(CodeReader *device) {
    if (device == nullptr || device->handle == nullptr) {
        return;
    }
    BcrUserCb userCb;
    const int idx = findSerialIndex(device->serialNumber);
    if (idx >= 0) {
        userCb = g_callbackFuncs[static_cast<std::size_t>(idx)];
    }
    void(__stdcall * thunk)(unsigned char *, MV_CODEREADER_IMAGE_OUT_INFO *, void *) =
        userCb ? sdkImageCallbackBridge : nullptr;
    void *pUser = userCb ? static_cast<void *>(device) : nullptr;
    int ok = MV_CODEREADER_RegisterImageCallBack(device->handle, thunk, pUser);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_RegisterImageCallBack error: " + toHexStr(ok));
    }
}

/**
 * 软触发：要求设备已在取流（Grabbing），否则抛 logic_error。
 */
void triggerDevice(const std::string &sn) {
    CodeReader *d = getDevice(sn, false);
    if (d == nullptr) {
        throw std::logic_error(
            "triggerDevice：设备未在会话中，请先 startDevice（或曾缓存该序列号）进入取流状态后再触发");
    }
    if (d->status != CodeReaderStatus::Grabbing) {
        throw std::logic_error(
            "triggerDevice：仅可在取流状态（Grabbing）下调用，请先 startDevice，且勿在 stopDevice 之后未重新取流时触发");
    }
    int triggerOk = MV_CODEREADER_SetCommandValue(d->handle, kTriggerSoftware);
    if (triggerOk != MV_CODEREADER_OK) {
        throw std::runtime_error(std::string("MV_CODEREADER_SetCommandValue(") + kTriggerSoftware +
                                 ") error: " + toHexStr(triggerOk));
    }
}
