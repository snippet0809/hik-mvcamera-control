#pragma once

/**
 * @file code_reader_detail.h
 * @brief 本库实现用内部类型与工具，不包含在对外极简 API（code_reader.h）中。
 */

#include <cstdint>
#include <istream>
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
    void startGrabbing();
    /** Grabbing → Open → Connected（停流并 CloseDevice）。 */
    void close();
};

CodeReader *getDevice(const std::string &sn, bool createIfNotExist);
void destroyDevice(const std::string &sn);

void codeReaderInternalBindImageCallbackBeforeGrabbing(CodeReader *device);

inline std::string toHexStr(std::uint32_t value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << value;
    return ss.str();
}

inline std::string toHexStr(int value) {
    return toHexStr(static_cast<std::uint32_t>(value));
}

inline std::string intToIp(unsigned int ip) {
    std::stringstream ss;
    ss << ((ip >> 24) & 0xFF) << "."
       << ((ip >> 16) & 0xFF) << "."
       << ((ip >> 8) & 0xFF) << "."
       << (ip & 0xFF);
    return ss.str();
}

inline bool tryParseIpv4HostOrder(const std::string &ip, unsigned int &out) {
    int octets[4] = {};
    char dot = 0;
    std::istringstream iss(ip);
    if (!(iss >> octets[0] >> dot) || dot != '.') {
        return false;
    }
    if (!(iss >> octets[1] >> dot) || dot != '.') {
        return false;
    }
    if (!(iss >> octets[2] >> dot) || dot != '.') {
        return false;
    }
    if (!(iss >> octets[3])) {
        return false;
    }
    iss >> std::ws;
    if (!iss.eof()) {
        return false;
    }
    for (int o : octets) {
        if (o < 0 || o > 255) {
            return false;
        }
    }
    out = (static_cast<unsigned int>(octets[0]) << 24) | (static_cast<unsigned int>(octets[1]) << 16) |
          (static_cast<unsigned int>(octets[2]) << 8) | static_cast<unsigned int>(octets[3]);
    return true;
}
