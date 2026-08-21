/** BCR 回调表、SDK 图像桥、TriggerSoftware */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char *kTriggerSoftware = "TriggerSoftware";
std::unordered_map<std::string, CodeReaderBcrCallback> g_bcr;

std::vector<std::string> bcrStrings(const MV_CODEREADER_IMAGE_OUT_INFO &info) {
    std::vector<std::string> out;
    if (info.nResultType != CodeReader_ResType_BCR) {
        return out;
    }
    MV_CODEREADER_RESULT_BCR bcr{};
    constexpr std::size_t kBcr = sizeof(MV_CODEREADER_RESULT_BCR);
    static_assert(kBcr <= MV_CODEREADER_MAX_RESULT_SIZE, "BCR payload");
    std::memcpy(&bcr, info.chResult, kBcr);
    unsigned n = std::min(bcr.nCodeNum, static_cast<unsigned>(MAX_CODEREADER_BCR_COUNT));
    out.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        const MV_CODEREADER_BCR_INFO &bi = bcr.stBcrInfo[i];
        const std::size_t cap = sizeof(bi.chCode);
        std::size_t len = std::min<std::size_t>({bi.nLen, cap - 1, strnlen(bi.chCode, cap)});
        out.emplace_back(bi.chCode, len);
    }
    return out;
}

void __stdcall imageBridge(unsigned char *, MV_CODEREADER_IMAGE_OUT_INFO *fi, void *pUser) {
    if (!fi || !pUser || !fi->bIsGetCode || fi->nResultType != CodeReader_ResType_BCR) {
        return;
    }
    auto *dev = static_cast<CodeReader *>(pUser);
    CodeReaderBcrCallback cb;
    {
        std::lock_guard<std::mutex> lock(g_device_mutex);
        auto it = g_bcr.find(dev->serialNumber);
        if (it == g_bcr.end() || !it->second) {
            return;
        }
        cb = it->second;
    }
    cb(bcrStrings(*fi));
}

} // namespace

void registerImageCallbackForSerial(const std::string &sn, const CodeReaderBcrCallback &cb) {
    // 调用方须已持有 g_device_mutex。
    if (cb) {
        g_bcr[sn] = cb;
    } else {
        g_bcr.erase(sn);
    }
    CodeReader *d = findDevice(sn);
    if (d && d->status == CodeReaderStatus::Grabbing) {
        codeReaderInternalBindImageCallbackBeforeGrabbing(d);
    }
}

void codeReaderInternalBindImageCallbackBeforeGrabbing(CodeReader *device) {
    // 调用方须已持有 g_device_mutex。
    if (!device || !device->handle) {
        return;
    }
    auto it = g_bcr.find(device->serialNumber);
    const bool on = it != g_bcr.end() && it->second;
    checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_RegisterImageCallBack(device->handle, on ? imageBridge : nullptr,
                                                                   on ? static_cast<void *>(device) : nullptr),
                               "MV_CODEREADER_RegisterImageCallBack");
}

void triggerDevice(const std::string &sn) {
    std::lock_guard<std::mutex> lock(g_device_mutex);
    CodeReader *d = findDevice(sn);
    if (!d || d->status != CodeReaderStatus::Grabbing) {
        throw std::logic_error("triggerDevice: 须已 startDevice 且处于取流");
    }
    checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_SetCommandValue(d->handle, kTriggerSoftware),
                               "MV_CODEREADER_SetCommandValue");
}
