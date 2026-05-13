#pragma once

/** 海康读码器：enumDevice / startDevice / stopDevice / triggerDevice；起流参数见 CodeReaderOpenParams。 */

#include <functional>
#include <optional>
#include <string>
#include <vector>

struct CodeReaderInfo {
    std::string serialNumber;
    std::string netExportIp;
};

/** startDevice 第二参：Open 未取流时写入再起流；默认值见成员初始化。 */
struct CodeReaderOpenParams {
    std::string triggerMode{"On"};
    std::string triggerSource{"Software"};
    bool code128{true};
    bool qrcode{true};
};

using CodeReaderBcrCallback = std::function<void(std::vector<std::string>)>;

std::vector<CodeReaderInfo> enumDevice();

/** 起流；已在 Grabbing 时忽略 params，仅当 onBcrCodes 有值时更新 BCR 登记。 */
void startDevice(const std::string &sn, const CodeReaderOpenParams &params = {},
                 const std::optional<CodeReaderBcrCallback> &onBcrCodes = std::nullopt);

void stopDevice(const std::string &sn);

/** TriggerSoftware；须已 startDevice 且处于取流。 */
void triggerDevice(const std::string &sn);
