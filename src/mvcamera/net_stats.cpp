/** 取流网络统计（丢包 / 丢帧 / 重传）：MV_CC_GetAllMatchInfo + MV_MATCH_TYPE_NET_DETECT */

#include "MvCameraControl.h"
#include "camera.h"
#include "camera_detail.h"
#include <stdexcept>

NetStats getCameraNetStats(const std::string& sn) {
    CameraDevice* d = findCamera(sn);
    if (!d || !d->handle) {
        throw std::logic_error("getCameraNetStats: 设备未 startCamera");
    }
    // 海康的约束：MV_MATCH_TYPE_NET_DETECT 只能在 MV_CC_StartGrabbing **之后**取，
    // 且统计窗口就是「本次 Start 到 Stop 之间」——中途重启取流会从零重新计。
    // 这里显式判状态并给可操作的提示，而不是把 SDK 的内部错误码甩给调用方。
    if (d->status != CameraStatus::Grabbing) {
        throw std::logic_error("getCameraNetStats: 网络统计仅在取流中有效（先 startCamera，统计窗口为本次起流到停流）");
    }

    MV_MATCH_INFO_NET_DETECT detect{};
    MV_ALL_MATCH_INFO info{};
    info.nType = MV_MATCH_TYPE_NET_DETECT;
    info.pInfo = &detect;
    info.nInfoSize = static_cast<unsigned int>(sizeof(detect));
    const int r = MV_CC_GetAllMatchInfo(d->handle, &info);
    if (r != MV_OK) {
        // USB3 相机与部分老固件没有 NET_DETECT 统计；GigE 线阵/面阵都支持。
        throw std::runtime_error(std::string("MV_CC_GetAllMatchInfo(NET_DETECT) error: ") + toHexStr(r) +
                                 "（该接口仅 GigE 相机支持：USB3 相机没有丢包统计）");
    }

    NetStats s;
    s.receiveDataSize = detect.nReceiveDataSize;
    s.lostPacketCount = detect.nLostPacketCount;
    s.lostFrameCount = detect.nLostFrameCount;
    s.receivedFrameCount = detect.nNetRecvFrameCount;
    s.requestResendPacketCount = detect.nRequestResendPacketCount;
    s.resendPacketCount = detect.nResendPacketCount;
    return s;
}
