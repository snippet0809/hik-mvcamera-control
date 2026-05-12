/**
 * @file device_info.cpp
 * @brief 设备枚举与句柄管理：在线列表、按序列号创建/缓存/销毁 CodeReader（SDK 句柄）。
 * @note 状态迁移（open/close/startGrabbing）见 device_control.cpp。
 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <memory>
#include <unordered_map>

std::unordered_map<std::string, std::shared_ptr<CodeReader>> deviceMap;

namespace {

    std::string gigESerialToString(const unsigned char *buf, std::size_t len) {
        std::size_t n = 0;
        while (n < len && buf[n] != '\0') {
            ++n;
        }
        return std::string(reinterpret_cast<const char *>(buf), n);
    }

} // namespace

/**
 * 按序列号创建 SDK 句柄，状态为 Connected（尚未 OpenDevice）。
 *
 * @param serialNumber 设备序列号
 * @throws std::runtime_error MV_CODEREADER_CreateHandleBySerialNumber 失败时抛出
 */
CodeReader::CodeReader(const std::string &serialNumber)
    : serialNumber(serialNumber), handle(nullptr), status(CodeReaderStatus::Connected) {
    int ok = MV_CODEREADER_CreateHandleBySerialNumber(&this->handle, this->serialNumber.c_str());
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_CreateHandleBySerialNumber error: " + toHexStr(ok));
    }
}

// 析构时须先尽量停流并 CloseDevice，再 DestroyHandle；否则部分固件上会出现下次 MV_CODEREADER_OpenDevice 报 0x80020000。
CodeReader::~CodeReader() {
    if (handle == nullptr) {
        return;
    }
    try {
        close();
    } catch (...) {
        // 析构阶段不再向外抛异常；尽力释放后再销毁句柄
    }
    MV_CODEREADER_DestroyHandle(handle);
    handle = nullptr;
}

/**
 * 枚举在线读码器，填充序列号与 GigE 相关摘要。
 *
 * @return 设备信息列表（仅传输层为 GigE 的项；其余类型跳过）。
 * @throws std::runtime_error MV_CODEREADER_EnumCodeReader 失败时抛出
 */
std::vector<CodeReaderInfo> enumDevice() {
    MV_CODEREADER_DEVICE_INFO_LIST stDevList{};
    int ok = MV_CODEREADER_EnumCodeReader(&stDevList);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_EnumCodeReader error: " + toHexStr(ok));
    }
    std::vector<CodeReaderInfo> infos;
    for (unsigned int i = 0; i < stDevList.nDeviceNum; i++) {
        MV_CODEREADER_DEVICE_INFO *pinfo = stDevList.pDeviceInfo[i];
        if (pinfo == nullptr) {
            continue;
        }
        if (pinfo->nTLayerType != MV_CODEREADER_GIGE_DEVICE) {
            continue;
        }
        const MV_CODEREADER_GIGE_DEVICE_INFO &gige = pinfo->SpecialInfo.stGigEInfo;
        std::string sn = gigESerialToString(gige.chSerialNumber, sizeof(gige.chSerialNumber));
        std::string netExportIp = intToIp(gige.nNetExport);
        infos.push_back({sn, netExportIp});
    }
    return infos;
}

/**
 * 按序列号查找或创建实例。
 * @return createIfNotExist 为 false 且不存在时返回 nullptr
 * @throws std::runtime_error createIfNotExist 为 true 且 MV_CODEREADER_CreateHandleBySerialNumber 失败时抛出
 */
CodeReader *getDevice(const std::string &sn, bool createIfNotExist) {
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

/** 从缓存移除该序列号实例（如 setIp 成功后设备重启，本地句柄失效）。 */
void destroyDevice(const std::string &sn) {
    deviceMap.erase(sn);
}
