#pragma once

/**
 * @file code_reader_detail.h
 * @brief 本库实现用内部类型与工具，不包含在对外极简 API（code_reader.h）中。
 */

#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

/** 设备在本库中的运行状态（句柄 / 已打开 / 取流）。 */
enum class CodeReaderStatus {
    Connected,
    Open,
    Grabbing
};

/** 单台设备实例：句柄与状态；由实现文件维护，不对外暴露。 */
class CodeReader {
public:
    std::string serialNumber;
    void *handle;
    CodeReaderStatus status;

    explicit CodeReader(const std::string &serialNumber);
    ~CodeReader();

    /** Connected → Open；已为 Open 则返回；Grabbing 下抛 std::logic_error（须先停流）。 */
    void open();
    /** Connected/Open → Grabbing；必要时内部 OpenDevice 并注册图像回调。 */
    void grabbing();
    /** Grabbing → Open → Connected（停流并 CloseDevice）。 */
    void close();
};

CodeReader *getDevice(const std::string &sn, bool createIfNotExist);

void codeReaderInternalBindImageCallbackBeforeGrabbing(CodeReader *device);

/** 按序列号登记 BCR 回调并在取流中刷新 SDK 绑定；仅供实现文件与 C API 调用。 */
void registerImageCallbackForSerial(const std::string &sn,
                                    const std::function<void(std::vector<std::string>)> &callback);

inline std::string toHexStr(int value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << static_cast<std::uint32_t>(value);
    return ss.str();
}

inline std::string intToIp(unsigned int ip) {
    std::stringstream ss;
    ss << ((ip >> 24) & 0xFF) << "."
       << ((ip >> 16) & 0xFF) << "."
       << ((ip >> 8) & 0xFF) << "."
       << (ip & 0xFF);
    return ss.str();
}
