#pragma once

/** 实现侧类型；不随 code_reader.h 对外暴露。 */

#include "code_reader.h"

#include <cstdint>
#include <sstream>
#include <string>

enum class CodeReaderStatus { Connected, Open, Grabbing };

class CodeReader {
public:
    std::string serialNumber;
    void *handle;
    CodeReaderStatus status;

    explicit CodeReader(const std::string &serialNumber);
    ~CodeReader();
    void open();
    void grabbing();
    void close();
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
