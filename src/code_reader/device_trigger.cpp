/** BCR 回调表、SDK 图像桥、TriggerSoftware */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char *kTriggerSoftware = "TriggerSoftware";
std::unordered_map<std::string, CodeReaderBcrCallback> g_bcr;
std::unordered_map<std::string, CodeReaderFrameCallback> g_frames;
std::unordered_map<std::string, std::shared_ptr<KeptBcrImage>> g_last_image;

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

void __stdcall imageBridge(unsigned char *pData, MV_CODEREADER_IMAGE_OUT_INFO *fi, void *pUser) {
    if (!fi || !pUser) {
        return;
    }
    auto *dev = static_cast<CodeReader *>(pUser);
    // 保留读码帧图，供 hik_cr_get_bcr_image 拉取（pData 在回调外无效，须拷贝）。
    // 仅读码成功 + 条码类型结果才留档；下面的 BCR / 帧回调分发对非 BCR 结果同样要跑。
    if (fi->bIsGetCode && fi->nResultType == CodeReader_ResType_BCR) {
        setLastBcrImage(dev->serialNumber, pData, fi->nFrameLen, fi->nWidth, fi->nHeight, fi->enPixelType);
    }
    CodeReaderBcrCallback cb;
    CodeReaderFrameCallback frameCb;
    {
        std::lock_guard<std::mutex> lock(g_device_mutex);
        auto it = g_bcr.find(dev->serialNumber);
        if (it != g_bcr.end() && it->second) {
            cb = it->second;
        }
        auto fit = g_frames.find(dev->serialNumber);
        if (fit != g_frames.end() && fit->second) {
            frameCb = fit->second;
        }
    }
    // BCR：仅读码成功 + 条码类型结果（保持原有行为）。
    if (fi->bIsGetCode && fi->nResultType == CodeReader_ResType_BCR && cb) {
        cb(bcrStrings(*fi));
    }
    // 读码成功帧图像转发：pData 仅回调期内有效，上层须同步拷贝。
    if (fi->bIsGetCode && frameCb) {
        CodeReaderFrameInfo fr;
        fr.width = fi->nWidth;
        fr.height = fi->nHeight;
        fr.pixelType = static_cast<unsigned int>(fi->enPixelType);
        fr.frameLen = fi->nFrameLen;
        fr.frameNum = fi->nFrameNum;
        frameCb(fr, pData, fi->nFrameLen);
    }
}

} // namespace

std::shared_ptr<KeptBcrImage> getLastBcrImage(const std::string &sn) {
    std::lock_guard<std::mutex> lock(g_device_mutex);
    auto it = g_last_image.find(sn);
    return it == g_last_image.end() ? nullptr : it->second;
}

void setLastBcrImage(const std::string &sn, const unsigned char *data, size_t len,
                     int width, int height, int pixelType) {
    auto img = std::make_shared<KeptBcrImage>();
    img->width = width;
    img->height = height;
    img->pixelType = pixelType;
    if (data && len > 0) {
        img->data.assign(data, data + len);
    }
    {
        std::lock_guard<std::mutex> lock(g_device_mutex);
        g_last_image[sn] = img;
    }
}

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

void registerFrameCallbackForSerial(const std::string &sn, const CodeReaderFrameCallback &cb) {
    // 调用方须已持有 g_device_mutex。
    if (cb) {
        g_frames[sn] = cb;
    } else {
        g_frames.erase(sn);
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
    auto bit = g_bcr.find(device->serialNumber);
    auto fit = g_frames.find(device->serialNumber);
    // BCR 或读码成功帧回调任一登记才绑桥；否则传 nullptr 即解绑。
    const bool on = (bit != g_bcr.end() && bit->second) || (fit != g_frames.end() && fit->second);
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
