#pragma once

/** 海康工业相机（MvCamera）：enumCamera / startCamera / stopCamera / triggerCamera + 参数读写。 */

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct CameraInfo {
    std::string serialNumber;
    std::string netExportIp;  // 仅 GigE 有；USB 为空串
    std::string modelName;    // 设备型号（GigE：chModelName；USB：chModelName）
};

/** startCamera 第二参：起流前写入 TriggerMode / TriggerSource；空串表示不修改。 */
struct CameraOpenParams {
    std::string triggerMode{"Off"};      // On / Off
    std::string triggerSource{"Software"};  // 软触发源
    int netTransMode{0};                 // 0=不设置（SDK 默认驱动模式）; 1=驱动; 2=socket（免 GigE 过滤驱动）
};

/** 单帧元数据（不含图像数据；数据经回调的 data/len 传递）。 */
struct FrameInfo {
    unsigned int width{0};
    unsigned int height{0};
    unsigned int pixelType{0};
    unsigned int frameLen{0};
    unsigned int frameNum{0};
    uint64_t hostTimestamp{0};
};

enum class CamParamType { Int, Float, Bool, Enum, String, Command };

/** 参数值：Int / Float / Bool / Enum(字符串 symbolic 值走 SetEnumValueByString)。 */
using CamParamValue = std::variant<int64_t, double, bool, uint32_t, std::string>;

/**
 * 图像回调：在 SDK 抓图线程被调用；data 指向 SDK 缓冲，仅在回调期间有效（勿跨线程持有）。
 */
using CameraFrameCallback = std::function<void(const FrameInfo&, const unsigned char* data, size_t len)>;

std::vector<CameraInfo> enumCamera();

/** 起流；已在 Grabbing 时忽略 params，仅当 onFrame 有值时更新图像回调登记。 */
void startCamera(const std::string& sn, const CameraOpenParams& params = {},
                 const std::optional<CameraFrameCallback>& onFrame = std::nullopt);

void stopCamera(const std::string& sn);

/** TriggerSoftware；须已 startCamera 且处于取流、且 TriggerMode 为 On。 */
void triggerCamera(const std::string& sn);

/** 按 GenICam 节点名读写参数；设备须已 startCamera。 */
void setCameraParam(const std::string& sn, const std::string& name, const CamParamValue& value);
CamParamValue getCameraParam(const std::string& sn, const std::string& name);

/** 执行 GenICam 命令节点（如 "TriggerSoftware"、"UserSetLoad"）；设备须已 startCamera。 */
void runCameraCommand(const std::string& sn, const std::string& name);

/**
 * 临时强制 GigE 相机 IP（MV_GIGE_ForceIpEx；重启后恢复，不改持久配置）。
 * 参数为 "a.b.c.d" 字符串；网关可为 "0.0.0.0"。
 */
void forceCameraIp(const std::string& sn, const std::string& ip, const std::string& subnetMask,
                   const std::string& gateway);
