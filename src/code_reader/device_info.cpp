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

std::string gigESerialToString(const unsigned char *buf, std::size_t len) {
    const char *p = reinterpret_cast<const char *>(buf);
    return std::string(p, strnlen(p, len));
}

} // namespace

CodeReader::CodeReader(const std::string &serialNumber)
    : serialNumber(serialNumber),
      handle(nullptr),
      status(CodeReaderStatus::Connected),
      handleStale(false) {
    const int ok = MV_CODEREADER_CreateHandleBySerialNumber(&handle, serialNumber.c_str());
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_CreateHandleBySerialNumber error: " + toHexStr(ok));
    }
}

void CodeReader::recreateHandle() {
    if (handle) {
        MV_CODEREADER_DestroyHandle(handle);
        handle = nullptr;
    }
    const int ok = MV_CODEREADER_CreateHandleBySerialNumber(&handle, serialNumber.c_str());
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_CreateHandleBySerialNumber error: " + toHexStr(ok));
    }
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
    const int ok = MV_CODEREADER_EnumCodeReader(&stDevList);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_EnumCodeReader error: " + toHexStr(ok));
    }
    std::vector<CodeReaderInfo> infos;
    for (unsigned i = 0; i < stDevList.nDeviceNum; ++i) {
        MV_CODEREADER_DEVICE_INFO *pinfo = stDevList.pDeviceInfo[i];
        if (!pinfo || pinfo->nTLayerType != MV_CODEREADER_GIGE_DEVICE) {
            continue;
        }
        const auto &gige = pinfo->SpecialInfo.stGigEInfo;
        infos.push_back({gigESerialToString(gige.chSerialNumber, sizeof(gige.chSerialNumber)),
                         intToIp(gige.nNetExport)});
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
