#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include <stdexcept>
#include <unordered_map>

std::unordered_map<std::string, CodeReader> deviceMap;

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

/**
 * 枚举读码器
 */
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

/**
 * 创建句柄
 *
 * @param sn 设备序列号
 * @return 句柄
 */
void *createHandle(std::string sn) {
    void *handle = nullptr;
    int ok = MV_CODEREADER_CreateHandleBySerialNumber(&handle, sn.c_str());
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_CreateHandleBySerialNumber error: " + toHexStr(ok));
    }
    deviceMap[sn] = handle;
    return handle;
}

/**
 * 销毁句柄
 *
 * @param sn 设备序列号
 */
void destroyHandle(std::string sn) {
    auto it = deviceMap.find(sn);
    if (it != deviceMap.end()) {
        MV_CODEREADER_DestroyHandle(it->second);
        deviceMap.erase(it);
    }
}

/**
 * 获取句柄，如果句柄不存在则新建
 *
 * @param sn 设备序列号
 * @return 句柄
 */
void *getHandle(std::string sn, bool createIfNotExist) {
    // 句柄已存在
    auto it = deviceMap.find(sn);
    if (it != deviceMap.end()) {
        return it->second;
    }
    // 句柄不存在
    return createHandle(sn);
}