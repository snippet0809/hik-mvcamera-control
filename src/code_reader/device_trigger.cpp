/**
 * @file device_trigger.cpp
 * @brief 图像回调登记、SDK 桥接与软触发（TriggerSoftware）。
 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

    constexpr const char *kTriggerSoftware = "TriggerSoftware";

    using BcrCodes = std::vector<std::string>;
    using BcrUserCb = CodeReaderBcrCallback;

    std::unordered_map<std::string, BcrUserCb> g_bcrBySerial;

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

    void __stdcall sdkImageCallbackBridge([[maybe_unused]] unsigned char *pData, MV_CODEREADER_IMAGE_OUT_INFO *pstFrameInfo,
                                          void *pUser) {
        if (pstFrameInfo == nullptr || pUser == nullptr) {
            return;
        }
        if (!pstFrameInfo->bIsGetCode || pstFrameInfo->nResultType != CodeReader_ResType_BCR) {
            return;
        }
        auto *device = static_cast<CodeReader *>(pUser);
        BcrCodes codes = extractBcrStrings(*pstFrameInfo);
        const auto it = g_bcrBySerial.find(device->serialNumber);
        if (it == g_bcrBySerial.end() || !it->second) {
            return;
        }
        it->second(std::move(codes));
    }

} // namespace

void registerImageCallbackForSerial(const std::string &sn, const CodeReaderBcrCallback &callback) {
    if (callback) {
        g_bcrBySerial[sn] = callback;
    } else {
        g_bcrBySerial.erase(sn);
    }
    CodeReader *d = getDevice(sn, false);
    if (d != nullptr && d->status == CodeReaderStatus::Grabbing) {
        codeReaderInternalBindImageCallbackBeforeGrabbing(d);
    }
}

void codeReaderInternalBindImageCallbackBeforeGrabbing(CodeReader *device) {
    if (device == nullptr || device->handle == nullptr) {
        return;
    }
    BcrUserCb userCb;
    const auto it = g_bcrBySerial.find(device->serialNumber);
    if (it != g_bcrBySerial.end()) {
        userCb = it->second;
    }
    void(__stdcall * thunk)(unsigned char *, MV_CODEREADER_IMAGE_OUT_INFO *, void *) =
        userCb ? sdkImageCallbackBridge : nullptr;
    void *pUser = userCb ? static_cast<void *>(device) : nullptr;
    int ok = MV_CODEREADER_RegisterImageCallBack(device->handle, thunk, pUser);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_RegisterImageCallBack error: " + toHexStr(ok));
    }
}

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
