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
    // 海康的时序约束：MV_CC_RegisterImageCallBackEx 必须在 StartGrabbing **之前**调用
    // （MvCameraControl.h 的 @remarks：「先 RegisterImageCallBackEx，再 StartGrabbing」；
    //  在 MV_CC_CreateHandle() 之后即可调用）。取流中更换回调会被 SDK 拒为
    // MV_E_CALLORDER(0x80000003) —— 那是**厂商设计的约束**，不是可以绕过的缺陷：
    // 想换回调必须 StopGrabbing → 重新登记 → 再 StartGrabbing。
    //
    // 这里抛可操作的错误，而不是把裸错误码透给调用方；并且**先判后改**，
    // 避免「注册表里已换成新回调、设备上跑的还是旧回调」这种不一致。
    CameraDevice* d = findCamera(sn);
    if (d && d->status == CameraStatus::Grabbing) {
        throw std::logic_error(
            "registerFrameCallbackForSerial: 设备正在取流，无法登记/更换图像回调"
            "（海康要求回调在 StartGrabbing 之前登记）；请先 stopCamera 再 startCamera: " + sn);
    }
    {
        std::lock_guard<std::mutex> lk(g_framesMutex);
        if (cb) {
            g_frames[sn] = cb;
        } else {
            g_frames.erase(sn);
        }
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
