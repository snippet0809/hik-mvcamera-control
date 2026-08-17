#pragma once

/** 实现侧类型；不随 code_reader.h 对外暴露。 */

#include "code_reader.h"

#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

enum class CodeReaderStatus { Connected, Open, Grabbing };

/**
 * 全局互斥锁：保护 deviceMap / g_bcr 及句柄状态迁移。
 * startDevice / stopDevice / triggerDevice / imageBridge 在各自入口加锁；
 * findDevice / getOrCreateDevice / CodeReader::open/grabbing/close / recreateHandle /
 * registerImageCallbackForSerial 等内部函数约定「调用方已持有本锁」。
 */
extern std::mutex g_device_mutex;

class CodeReader {
public:
    std::string serialNumber;
    void *handle;
    CodeReaderStatus status;
    /** true 表示句柄已被 CloseDevice，SDK 不允许拿它再次 OpenDevice，须先重建。 */
    bool handleStale;

    explicit CodeReader(const std::string &serialNumber);
    ~CodeReader();
    void open();
    void grabbing();
    void close();
    /** 销毁旧句柄并重新 CreateHandleBySerialNumber，得到可再次 Open 的新句柄。 */
    void recreateHandle();
};

CodeReader *findDevice(const std::string &sn);
CodeReader *getOrCreateDevice(const std::string &sn);

void codeReaderInternalBindImageCallbackBeforeGrabbing(CodeReader *device);
void registerImageCallbackForSerial(const std::string &sn, const CodeReaderBcrCallback &callback);

inline std::string toHexStr(int value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << static_cast<std::uint32_t>(value);
    return ss.str();
}

inline std::string intToIp(unsigned int ip) {
    std::stringstream ss;
    ss << ((ip >> 24) & 0xFF) << "." << ((ip >> 16) & 0xFF) << "." << ((ip >> 8) & 0xFF) << "." << (ip & 0xFF);
    return ss.str();
}
