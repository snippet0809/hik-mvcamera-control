/** 图像回调表、SDK __stdcall 图像桥、TriggerSoftware */

#include "MvCameraControl.h"
#include "camera.h"
#include "camera_detail.h"
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

std::unordered_map<std::string, CameraFrameCallback> g_frames;
std::mutex g_framesMutex;

constexpr const char* kTriggerSoftware = "TriggerSoftware";

/**
 * SDK 图像桥（__stdcall，SDK 抓图线程调用）。
 * 把帧元数据 + 数据指针透传给登记回调；不拷贝——数据仅回调期内有效，上层须同步消费/拷贝。
 */
void __stdcall imageBridge(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo, void* pUser) {
    if (!pFrameInfo || !pUser) {
        return;
    }
    auto* dev = static_cast<CameraDevice*>(pUser);

    CameraFrameCallback cb;
    {
        std::lock_guard<std::mutex> lk(g_framesMutex);
        auto it = g_frames.find(dev->serialNumber);
        if (it == g_frames.end() || !it->second) {
            return;
        }
        cb = it->second;
    }

    FrameInfo fi;
    fi.width = pFrameInfo->nWidth;
    fi.height = pFrameInfo->nHeight;
    fi.pixelType = static_cast<unsigned int>(pFrameInfo->enPixelType);
    fi.frameLen = pFrameInfo->nFrameLen;
    fi.frameNum = pFrameInfo->nFrameNum;
    fi.hostTimestamp = static_cast<std::uint64_t>(pFrameInfo->nHostTimeStamp);

    cb(fi, pData, fi.frameLen);
}

} // namespace

void registerFrameCallbackForSerial(const std::string& sn, const CameraFrameCallback& cb) {
    {
        std::lock_guard<std::mutex> lk(g_framesMutex);
        if (cb) {
            g_frames[sn] = cb;
        } else {
            g_frames.erase(sn);
        }
    }
    CameraDevice* d = findCamera(sn);
    if (d && d->status == CameraStatus::Grabbing) {
        cameraInternalBindImageCallbackBeforeGrabbing(d);
    }
}

void cameraInternalBindImageCallbackBeforeGrabbing(CameraDevice* device) {
    if (!device || !device->handle) {
        return;
    }
    CameraFrameCallback cb;
    {
        std::lock_guard<std::mutex> lk(g_framesMutex);
        auto it = g_frames.find(device->serialNumber);
        if (it != g_frames.end() && it->second) {
            cb = it->second;
        }
    }
    checkSdk<MV_OK>(MV_CC_RegisterImageCallBackEx(device->handle, cb ? imageBridge : nullptr,
                                                  cb ? static_cast<void*>(device) : nullptr),
                    "MV_CC_RegisterImageCallBackEx");
}

void triggerCamera(const std::string& sn) {
    CameraDevice* d = findCamera(sn);
    if (!d || d->status != CameraStatus::Grabbing) {
        throw std::logic_error("triggerCamera: 须已 startCamera 且处于取流（且 TriggerMode 为 On）");
    }
    checkSdk<MV_OK>(MV_CC_SetCommandValue(d->handle, kTriggerSoftware), "MV_CC_SetCommandValue");
}
