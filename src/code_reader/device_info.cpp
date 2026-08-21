/** 枚举、按序列号缓存 CodeReader；状态机见 device_control.cpp */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

std::mutex g_device_mutex;
std::unordered_map<std::string, std::shared_ptr<CodeReader>> deviceMap;

namespace {

} // namespace

CodeReader::CodeReader(const std::string &serialNumber)
    : serialNumber(serialNumber),
      handle(nullptr),
      status(CodeReaderStatus::Connected),
      handleStale(false) {
    checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_CreateHandleBySerialNumber(&handle, serialNumber.c_str()),
                               "MV_CODEREADER_CreateHandleBySerialNumber");
}

void CodeReader::recreateHandle() {
    if (handle) {
        MV_CODEREADER_DestroyHandle(handle);
        handle = nullptr;
    }
    checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_CreateHandleBySerialNumber(&handle, serialNumber.c_str()),
                               "MV_CODEREADER_CreateHandleBySerialNumber");
    handleStale = false;
}

CodeReader::~CodeReader() {
    if (!handle) {
        return;
    }
    try {
        close();
    } catch (...) {
    }
    MV_CODEREADER_DestroyHandle(handle);
    handle = nullptr;
}

std::vector<CodeReaderInfo> enumDevice() {
    MV_CODEREADER_DEVICE_INFO_LIST stDevList{};
    checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_EnumCodeReader(&stDevList), "MV_CODEREADER_EnumCodeReader");
    std::vector<CodeReaderInfo> infos;
    for (unsigned i = 0; i < stDevList.nDeviceNum; ++i) {
        MV_CODEREADER_DEVICE_INFO *pinfo = stDevList.pDeviceInfo[i];
        if (!pinfo) {
            continue;
        }
        CodeReaderInfo info;
        if (pinfo->nTLayerType == MV_CODEREADER_GIGE_DEVICE) {
            const auto &gige = pinfo->SpecialInfo.stGigEInfo;
            info.serialNumber = bytesToStr(gige.chSerialNumber, sizeof(gige.chSerialNumber));
            info.netExportIp = intToIp(gige.nNetExport);
        } else if (pinfo->nTLayerType == MV_CODEREADER_USB_DEVICE) {
            const auto &usb = pinfo->SpecialInfo.stUsb3VInfo;
            info.serialNumber = bytesToStr(usb.chSerialNumber, sizeof(usb.chSerialNumber));
            info.netExportIp.clear();  // USB 读码器无 IP
        } else {
            continue;
        }
        infos.push_back(std::move(info));
    }
    return infos;
}

CodeReader *findDevice(const std::string &sn) {
    // 调用方须已持有 g_device_mutex。
    const auto it = deviceMap.find(sn);
    return it == deviceMap.end() ? nullptr : it->second.get();
}

CodeReader *getOrCreateDevice(const std::string &sn) {
    // 调用方须已持有 g_device_mutex。
    const auto it = deviceMap.find(sn);
    if (it != deviceMap.end()) {
        return it->second.get();
    }
    return deviceMap.emplace(sn, std::make_shared<CodeReader>(sn)).first->second.get();
}
