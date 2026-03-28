#include "code_reader.h"
#include "MvCodeReaderCtrl.h"
#include "MvCodeReaderParams.h"
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

std::unordered_map<std::string, void *> deviceMap;

inline std::string toHexStr(int value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << value;
    return ss.str();
}

std::vector<std::string> enumCodeReader() {
    MV_CODEREADER_DEVICE_INFO_LIST g_stDevList{};
    int ok = MV_CODEREADER_EnumCodeReader(&g_stDevList);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_EnumCodeReader error: " + toHexStr(ok));
    }
    std::vector<std::string> sns;
    for (unsigned int i = 0; i < g_stDevList.nDeviceNum; i++) {
        sns.push_back((char *)g_stDevList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.chSerialNumber);
    }
    return sns;
}

/**
 * 停止设备，会执行以下操作：
 * 1.停止取流
 * 2.关闭设备
 * 3.销毁句柄
 */
void stopDevice(std::string sn) {
    auto it = deviceMap.find(sn);
    if (it != deviceMap.end()) {
        void *handle = it->second;
        MV_CODEREADER_StopGrabbing(handle);
        MV_CODEREADER_CloseDevice(handle);
        MV_CODEREADER_DestroyHandle(handle);
        deviceMap.erase(it);
    }
}

/**
 * 启动设备，会执行以下操作：
 * 1.创建句柄
 * 2.打开设备
 * 3.启动取流
 */
void startDevice(std::string sn) {
    // 停止设备
    stopDevice(sn);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // 创建句柄
    void *handle = nullptr;
    int ok = MV_CODEREADER_CreateHandleBySerialNumber(&handle, sn.c_str());
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_CreateHandleBySerialNumber error: " + toHexStr(ok));
    }
    deviceMap[sn] = handle;
    // 打开设备
    ok = MV_CODEREADER_OpenDevice(handle);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_OpenDevice error: " + toHexStr(ok));
    }
    // 启动取流
    ok = MV_CODEREADER_StartGrabbing(handle);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_StartGrabbing error: " + toHexStr(ok));
    }
}
