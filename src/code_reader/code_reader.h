#pragma once

/**
 * @file code_reader.h
 * @brief 海康读码器简易 C++ 封装：按序列号枚举设备、启动/停止取流、注册读码回调、软触发等。
 * @note 典型取流路径由 startDevice/stopDevice 在内部完成打开与起停流。常用 GenICam 项可通过 `startDevice` 的 `CodeReaderOpenParams` 在起流前写入。
 */

#include <functional>
#include <optional>
#include <string>
#include <vector>

/** 枚举到的单台设备摘要（当前仅 GigE）。 */
struct CodeReaderInfo {
    std::string serialNumber;
    std::string netExportIp;
};

/**
 * 设备处于「已 OpenDevice、未取流」（Open）时可写入的可选参数集合；亦作为 `startDevice` 第二参数，在起流前写入已设字段。
 * 未设置的 `std::optional` 表示不修改该项，沿用设备当前值。
 */
struct CodeReaderOpenParams {
    std::optional<std::string> triggerMode;
    std::optional<std::string> triggerSource;
};

/** 读码（BCR）结果列表回调；在 SDK 线程触发。 */
using CodeReaderBcrCallback = std::function<void(std::vector<std::string>)>;

std::vector<CodeReaderInfo> enumDevice();

/**
 * 按序列号开始取流（内部完成打开设备、在 Open 阶段应用 @p params 中已设字段、可选注册 BCR 回调、StartGrabbing）。
 * @param params 未设置的 `std::optional` 表示不修改该项。已在取流（Grabbing）且 @p params 含任意已设字段时抛 `std::logic_error`（须先 `stopDevice`）。
 * @param onBcrCodes `std::nullopt` 表示不修改该序列号上已登记的回调；传入非空函数则注册/覆盖；传入已 engaged 的空 `std::function` 则取消该序列号回调。已在取流时仅更新回调表并刷新 SDK 绑定，不重复起流。
 */
void startDevice(const std::string &sn, const CodeReaderOpenParams &params = {},
                 const std::optional<CodeReaderBcrCallback> &onBcrCodes = std::nullopt);

/** 按序列号停止取流并关闭设备（句柄仍缓存在本库映射中）。 */
void stopDevice(const std::string &sn);

/**
 * 软触发（TriggerSoftware）。要求：已成功 startDevice，当前处于取流中。
 * @throws std::logic_error 未缓存设备或未在取流状态
 */
void triggerDevice(const std::string &sn);
