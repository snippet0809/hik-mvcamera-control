// 设备枚举与句柄管理：在线设备列表、按序列号创建/缓存/销毁 CodeReader（SDK 句柄）。

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include <stdexcept>
#include <unordered_map>

// 已创建的读码器实例，键为设备序列号；erase 时会析构 CodeReader 并销毁 SDK 句柄。
std::unordered_map<std::string, std::shared_ptr<CodeReader>> deviceMap;

CodeReader::CodeReader(std::string sn) {
    // 仅创建句柄，状态为 Connected；打开设备、取流见 open / grabbing。
    int ok = MV_CODEREADER_CreateHandleBySerialNumber(&this->handle, sn.c_str());
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_CreateHandleBySerialNumber error: " + toHexStr(ok));
    }
    this->serialNumber = sn;
    this->status = CodeReaderStatus::Connected;
}

CodeReader::~CodeReader() {
    MV_CODEREADER_DestroyHandle(this->handle);
}

std::vector<CodeReaderInfo> enumDevice() {
    MV_CODEREADER_DEVICE_INFO_LIST stDevList{};
    // 枚举指定系列读码器（虚拟相机可枚举；私有协议设备见 MV_CODEREADER_EnumIDDevices）。
    int ok = MV_CODEREADER_EnumCodeReader(&stDevList);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_EnumCodeReader error: " + toHexStr(ok));
    }
    std::vector<CodeReaderInfo> infos;
    for (unsigned int i = 0; i < stDevList.nDeviceNum; i++) {
        // 当前仅读取 GigE 分支字段；USB 等设备需按 nTLayerType 解析 SpecialInfo。
        std::string sn = (char *)stDevList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.chSerialNumber;
        // nNetExport：SDK 注释为与设备通信所使用的主机网口 IP（主机字节序由 intToIp 格式化）。
        std::string netExportIp = intToIp(stDevList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.nNetExport);
        infos.push_back({sn, netExportIp});
    }
    return infos;
}

CodeReader *getDevice(std::string sn, bool createIfNotExist) {
    auto it = deviceMap.find(sn);
    if (it != deviceMap.end()) {
        return it->second.get();
    }
    if (createIfNotExist) {
        auto cr = std::make_shared<CodeReader>(sn);
        deviceMap[sn] = cr;
        return cr.get();
    }
    return nullptr;
}

void destroyDevice(std::string sn) {
    // 从缓存移除并析构实例，内部 ~CodeReader 会调用 MV_CODEREADER_DestroyHandle。
    deviceMap.erase(sn);
}
