#pragma once

/**
 * @file code_reader.h
 * @brief 海康读码器 SDK 的 C++ 封装：设备枚举、句柄生命周期、取流与参数设置等对外接口。
 * @note 实现分散在 device_info / device_control / device_set_param / device_trigger 等 .cpp 中。
 */

#include <cstdint>
#include <functional>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

/** 读码器在本封装内的运行状态（与 SDK Open / 取流步骤对应）。 */
enum class CodeReaderStatus {
    Connected, ///< 已创建句柄，尚未 OpenDevice
    Open,      ///< 已 OpenDevice，未取流（适合部分参数设置）
    Grabbing   ///< 正在 StartGrabbing 取流
};

/** 枚举单台在线设备时返回的摘要信息（GigE）。 */
struct CodeReaderInfo {
    std::string serialNumber; ///< 设备序列号
    std::string netExportIp;  ///< GigE：SDK 中与通信相关的主机网口 IP 的字符串形式（见实现注释）
};

/**
 * 单台读码器的包装：持有 SDK 句柄，并提供 open / close / startGrabbing 状态迁移。
 * 实例由 getDevice 缓存在全局 map 中；返回的裸指针在 destroyDevice / setIp 等之后可能失效，勿长期保存。
 */
class CodeReader {
public:
    std::string serialNumber;
    void *handle; ///< MV_CODEREADER_* 使用的设备句柄
    CodeReaderStatus status;

    explicit CodeReader(const std::string &serialNumber);
    ~CodeReader();

    void open();         ///< 打开设备（未取流）
    void startGrabbing(); ///< 开始取流
    void close();        ///< 停流（若需要）并关闭设备
};

/** 枚举当前可读码器设备列表（内部调用 MV_CODEREADER_EnumCodeReader）。 */
std::vector<CodeReaderInfo> enumDevice();

/**
 * 按序列号获取或创建读码器实例（缓存在进程内全局 map）。
 *
 * @note 返回的裸指针在 destroyDevice / setIp 等之后可能失效；多线程下由调用方自行同步。
 */
CodeReader *getDevice(const std::string &sn, bool createIfNotExist);

void destroyDevice(const std::string &sn);

void startDevice(const std::string &sn);

void stopDevice(const std::string &sn);

/**
 * 注册读码结果回调（内部对应 MV_CODEREADER_RegisterImageCallBack，在 StartGrabbing 前绑定到设备）。
 *
 * 当一帧中识别到条码且结果类型为 BCR 时，在 SDK 内部线程调用 callback，传入本帧解码得到的字符串列表（按 nCodeNum 顺序）。
 * 传空 std::function（默认构造）表示取消回调，下次 startDevice 时向 SDK 注册 nullptr。
 *
 * @note 通常在 startDevice 之前调用，以保证首次取流即生效；若设备已在取流中修改回调，需 stopDevice 后再次 startDevice。
 * @note 回调在 SDK 线程执行，避免在回调内长时间阻塞或调用可能导致与本 SDK 死锁的接口。
 */
void registerImageCallback(const std::function<void(std::vector<std::string> codeArr)> &callback);

/**
 * 软触发一次（MV_CODEREADER_SetCommandValue，命令节点 TriggerSoftware）。
 *
 * @pre 设备必须已处于本封装内的取流状态（CodeReaderStatus::Grabbing），即已成功 startDevice 且未 stopDevice/close 到非取流。
 * @note 另需在设备上配置软触发相关参数（如 TriggerMode On、TriggerSource Software，以设备 XML 为准）。
 * @throws std::logic_error 非取流状态或设备未缓存时
 * @throws std::runtime_error SDK 调用失败
 */
void triggerDevice(const std::string &sn);

void setIp(const std::string &sn, const std::string &ip, const std::string &mask, const std::string &gateway);

void setIntValue(const std::string &sn, const std::string &key, int value);
void setStringValue(const std::string &sn, const std::string &key, const std::string &value);
void setBoolValue(const std::string &sn, const std::string &key, bool value);
void setFloatValue(const std::string &sn, const std::string &key, float value);

/** 将无符号错误码格式化为 0xXXXXXXXX 十六进制字符串（用于异常信息）。 */
inline std::string toHexStr(std::uint32_t value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << value;
    return ss.str();
}

/** 将 SDK 返回的 int 错误码格式化为十六进制字符串（按无符号位模式显示）。 */
inline std::string toHexStr(int value) {
    return toHexStr(static_cast<std::uint32_t>(value));
}

/** 将 32 位 IPv4（主机字节序）格式化为点分十进制。 */
inline std::string intToIp(unsigned int ip) {
    std::stringstream ss;
    ss << ((ip >> 24) & 0xFF) << "."
       << ((ip >> 16) & 0xFF) << "."
       << ((ip >> 8) & 0xFF) << "."
       << (ip & 0xFF);
    return ss.str();
}

/**
 * 解析点分十进制 IPv4 为主机字节序 32 位整数。
 * @return 成功返回 true 并写入 out；格式非法、段超出 0–255 或存在多余字符时返回 false（out 不修改）。
 */
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

/**
 * @internal
 * @brief 取流前把 registerImageCallback 登记的内容注册到海康 SDK（MV_CODEREADER_RegisterImageCallBack）。
 * @note 由 CodeReader::startGrabbing 在调用 MV_CODEREADER_StartGrabbing 之前调用；勿作为稳定对外 ABI 依赖。
 */
void codeReaderInternalBindImageCallbackBeforeGrabbing(CodeReader *device);
