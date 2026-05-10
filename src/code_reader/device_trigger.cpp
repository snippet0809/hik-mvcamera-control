/**
 * @file device_trigger.cpp
 * @brief 图像回调注册与软触发：对接 MV_CODEREADER_RegisterImageCallBack、SetCommandValue(TriggerSoftware)。
 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

/** 软触发命令节点名（GenICam 常见命名，具体以设备 XML 为准）。 */
constexpr const char kTriggerSoftware[] = "TriggerSoftware";

/** 保护 g_imageCallback，避免与 SDK 回调线程并发读写。 */
std::mutex g_imageCallbackMutex;

/** 集成方通过 registerImageCallback 注册的读码结果回调；空表示不注册 SDK 回调。 */
std::function<void(std::vector<std::string>)> g_imageCallback;

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
 * pData 为图像缓冲（此处只关心读码结果，不解析像素）；pstFrameInfo 含 BCR 等元数据。
 */
void __stdcall sdkImageCallbackBridge(unsigned char *pData, MV_CODEREADER_IMAGE_OUT_INFO *pstFrameInfo, void *pUser) {
    (void)pData;
    (void)pUser;
    if (pstFrameInfo == nullptr) {
        return;
    }
    // 仅在有读码结果且类型为 BCR 时向上层派发（与头文件约定一致）
    if (!pstFrameInfo->bIsGetCode || pstFrameInfo->nResultType != CodeReader_ResType_BCR) {
        return;
    }
    std::vector<std::string> codes = extractBcrStrings(*pstFrameInfo);
    // 拷贝回调后在锁外调用，避免用户回调里再次 register 导致死锁
    std::function<void(std::vector<std::string>)> userCb;
    {
        std::lock_guard<std::mutex> lock(g_imageCallbackMutex);
        userCb = g_imageCallback;
    }
    if (userCb) {
        userCb(std::move(codes));
    }
}

} // namespace

/**
 * 注册/更新/清空读码结果回调（仅更新内存；真正写入 SDK 在每次 startGrabbing 前完成）。
 */
void registerImageCallback(const std::function<void(std::vector<std::string> codeArr)> &callback) {
    std::lock_guard<std::mutex> lock(g_imageCallbackMutex);
    g_imageCallback = callback;
}

/**
 * 在 MV_CODEREADER_StartGrabbing 之前，把当前全局回调注册到指定设备句柄。
 * 无用户回调时向 SDK 传 nullptr，等价于取消图像回调。
 */
void codeReaderInternalBindImageCallbackBeforeGrabbing(CodeReader *device) {
    if (device == nullptr || device->handle == nullptr) {
        return;
    }
    std::function<void(std::vector<std::string>)> userCb;
    {
        std::lock_guard<std::mutex> lock(g_imageCallbackMutex);
        userCb = g_imageCallback;
    }
    void(__stdcall *thunk)(unsigned char *, MV_CODEREADER_IMAGE_OUT_INFO *, void *) =
        userCb ? sdkImageCallbackBridge : nullptr;
    int ok = MV_CODEREADER_RegisterImageCallBack(device->handle, thunk, nullptr);
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
            "triggerDevice：设备未缓存，请先创建句柄并 startDevice 进入取流状态后再触发");
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
