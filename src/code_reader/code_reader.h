#pragma once

/** 海康读码器：enumDevice / startDevice / stopDevice / triggerDevice + 参数读写；起流参数见 CodeReaderOpenParams。 */

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct CodeReaderInfo {
    std::string serialNumber;
    std::string netExportIp;
    std::string modelName;  // 设备型号，用于区分读码器（如 MV-IDB*）与相机（如 MV-CU*）
};

/** startDevice 第二参：Open 未取流时写入再起流；默认值见成员初始化。 */
struct CodeReaderOpenParams {
    std::string triggerMode{"On"};
    std::string triggerSource{"Software"};
    bool code128{true};
    bool qrcode{true};
};

using CodeReaderBcrCallback = std::function<void(std::vector<std::string>)>;

/** 参数值：Int / Float / Bool / Enum(字符串 symbolic 值走 SetEnumValueByString)。 */
using CodeReaderParamValue = std::variant<int64_t, double, bool, uint32_t, std::string>;

std::vector<CodeReaderInfo> enumDevice();

/** 起流；已在 Grabbing 时忽略 params，仅当 onBcrCodes 有值时更新 BCR 登记。 */
void startDevice(const std::string &sn, const CodeReaderOpenParams &params = {},
                 const std::optional<CodeReaderBcrCallback> &onBcrCodes = std::nullopt);

void stopDevice(const std::string &sn);

/** TriggerSoftware；须已 startDevice 且处于取流。 */
void triggerDevice(const std::string &sn);

/** 按 GenICam 节点名读写参数；设备须已 startDevice。 */
void setReaderParam(const std::string &sn, const std::string &name, const CodeReaderParamValue &value);
CodeReaderParamValue getReaderParam(const std::string &sn, const std::string &name);

/** 执行 GenICam 命令节点（如 "TriggerSoftware"、"UserSetLoad"）；设备须已 startDevice。 */
void runReaderCommand(const std::string &sn, const std::string &name);
