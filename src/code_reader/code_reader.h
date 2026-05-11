#pragma once

/**
 * @file code_reader.h
 * @brief 海康读码器简易 C++ 封装：按序列号枚举设备、启动/停止取流、注册读码回调、网络与 GenICam 写参、软触发等。
 * @note 典型取流路径由 startDevice/stopDevice 在内部完成打开与起停流。修改 IP 或 GenICam 参数须先调用
 * openDeviceForParameters 使设备处于 Open（未取流），状态不符时抛 std::logic_error，由调用方维护流程。
 */

#include <functional>
#include <string>
#include <vector>

/** 枚举到的单台设备摘要（当前仅 GigE）。 */
struct CodeReaderInfo {
    std::string serialNumber;
    std::string netExportIp;
};

std::vector<CodeReaderInfo> enumDevice();

/** 按序列号开始取流（内部完成打开设备、注册图像回调、StartGrabbing）。 */
void startDevice(const std::string &sn);

/** 按序列号停止取流并关闭设备（句柄仍缓存，除非 setIp 等会 destroy）。 */
void stopDevice(const std::string &sn);

/**
 * 将设备置为「已 OpenDevice、未取流」（Open），以便改 GigE IP（setIp）或写 GenICam 节点（setIntValue 等）。
 * 当前为取流（Grabbing）时抛异常，须先 stopDevice；已为 Open 则无操作。
 *
 * 上述接口均要求设备处于 Open；若正在取流须先 stopDevice 再调用本函数，
 * 否则抛 std::logic_error。未缓存设备时本函数会创建句柄（与 startDevice 相同）。
 */
void openDeviceForParameters(const std::string &sn);

/** GigE 强制改设备 IP / 掩码 / 网关（与下方按类型的 GenICam 参数设置不同类）。成功后常伴随设备重启与本地句柄释放。 */
void setIp(const std::string &sn, const std::string &ip, const std::string &mask, const std::string &gateway);

/**
 * 注册读码结果回调；在识别到条码且类型为 BCR 时，于 SDK 线程调用。
 * 空 std::function 表示取消；建议在 startDevice 之前注册。
 */
void registerImageCallback(const std::function<void(std::vector<std::string>)> &callback);

/**
 * 软触发（TriggerSoftware）。要求：已成功 startDevice，当前处于取流中。
 * @throws std::logic_error 未缓存设备或未在取流状态
 */
void triggerDevice(const std::string &sn);

/** GenICam 节点写参（整型/字符串/布尔/浮点/枚举）。须先 openDeviceForParameters，设备处于 Open（未取流）。 */
void setIntValue(const std::string &sn, const std::string &key, int value);
void setStringValue(const std::string &sn, const std::string &key, const std::string &value);
void setBoolValue(const std::string &sn, const std::string &key, bool value);
void setFloatValue(const std::string &sn, const std::string &key, float value);
void setEnumValue(const std::string &sn, const std::string &key, unsigned int value);
void setEnumValueByString(const std::string &sn, const std::string &key, const std::string &symbolic);
