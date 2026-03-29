#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <vector>

enum class CodeReaderStatus {
    Connected,
    Open,
    Grabbing
};

struct CodeReaderInfo {
    std::string serialNumber;
    std::string netExportIp;
};

class CodeReader {
public:
    std::string serialNumber;
    void *handle;
    CodeReaderStatus status;

    CodeReader(std::string sn);
    ~CodeReader();

    void open();
    void grabbing();
    void close();
};

std::vector<CodeReaderInfo> enumDevice();
CodeReader *getDevice(std::string sn, bool createIfNotExist);
void destroyDevice(std::string sn);

void startDevice(std::string sn);
void stopDevice(std::string sn);

void registerImageCallback(std::function<void(std::vector<std::string> codeArr)> callback);
void triggerDevice(std::string sn);

void setIp(std::string sn, std::string ip, std::string mask, std::string gateway);
void setIntValue(std::string sn, std::string key, int value);
void setStringValue(std::string sn, std::string key, std::string value);
void setBoolValue(std::string sn, std::string key, bool value);
void setFloatValue(std::string sn, std::string key, float value);

inline std::string toHexStr(int value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << value;
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

inline unsigned int ipToInt(const std::string &ip) {
    std::vector<int> octets(4);
    char dot;
    std::istringstream iss(ip);
    if (iss >> octets[0] >> dot >> octets[1] >> dot >> octets[2] >> dot >> octets[3]) {
        return (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
    }
    return 0;
}