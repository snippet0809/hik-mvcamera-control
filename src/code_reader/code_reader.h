#pragma once

/**
 * @file code_reader.h
 * @brief 海康读码器 C++ 封装：对外仅 `enumDevice` / `startDevice` / `stopDevice` / `triggerDevice`；
 *      起流前参数见 `CodeReaderOpenParams`，读码回调在 `startDevice` 第三参（可选）中登记。
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
 * `startDevice` 第二参数：在 Open（未取流）阶段写入后再起流。
 * 成员带默认值；未单独指定的字段即采用默认（触发枚举须与设备 XML 符号名一致）。
 */
struct CodeReaderOpenParams {
    std::string triggerMode{"On"};
    std::string triggerSource{"Software"};
    bool code128{true};
    bool qrcode{true};
};

/** 读码（BCR）结果列表回调；在 SDK 线程触发。 */
using CodeReaderBcrCallback = std::function<void(std::vector<std::string>)>;

std::vector<CodeReaderInfo> enumDevice();

/**
 * 按序列号开始取流（Open 阶段写入 @p params、可选登记 BCR 回调，再进入取流）。
 * 已在取流时忽略 @p params，仅处理 @p onBcrCodes（刷新或注销回调）。
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
