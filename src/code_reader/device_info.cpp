#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include <stdexcept>
#include <unordered_map>

std::unordered_map<std::string, std::shared_ptr<CodeReader>> deviceMap;

CodeReader::CodeReader(std::string sn) {
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
    int ok = MV_CODEREADER_EnumCodeReader(&stDevList);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_EnumCodeReader error: " + toHexStr(ok));
    }
    std::vector<CodeReaderInfo> infos;
    for (unsigned int i = 0; i < stDevList.nDeviceNum; i++) {
        std::string sn = (char *)stDevList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.chSerialNumber;
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
    deviceMap.erase(sn);
}