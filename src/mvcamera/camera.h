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

/** startCamera 第二参：起流前写入 TriggerMode / TriggerSource / Width / Height；空串/0 表示不修改。 */
struct CameraOpenParams {
    std::string triggerMode{};           // 空串=起流不写（保持相机持久化配置）; "On"/"Off" 时起流前写
    std::string triggerSource{};         // 空串=起流不写; 符号名（如 "Software"）时起流前写
    int netTransMode{0};                 // 0=不设置（SDK 默认驱动模式）; 1=驱动; 2=socket（免 GigE 过滤驱动）
    int width{0};                        // >0 时起流前写 Width
    int height{0};                       // >0 时起流前写 Height（线阵相机：每帧行数）
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
 * JPEG 输出缓冲上界：按 RGB24 最坏体积（宽 × 高 × 3）加固定富余量计算，**不做编码**。
 * 调用方可据此预分配缓冲再调 encodeCameraJpeg，避免「先编码一遍只为量长度」的浪费。
 * 宽/高为 0、超过海康 JPEG 上限（65500）或乘积溢出时抛 invalid_argument。
 */
size_t cameraJpegBufferBound(unsigned int width, unsigned int height);

/**
 * 把内存中的一帧原始图像编码为 JPEG 字节（MV_CC_SaveImageEx3；不落盘）。
 * 供上层把 onFrame 拿到的原始帧直接转成可上传/可展示的 JPEG，省掉「落盘再读回」的往返。
 *
 * @param sn      设备序列号；该设备须已 startCamera（内部复用其句柄——SaveImageEx3 需要句柄上下文
 *                来应用 Bayer 插值/gamma/CCM 等设置）。
 * @param info    帧元数据（宽/高/像素格式），取自 onFrame 回调的 frameInfo。
 * @param data    原始帧数据（onFrame 回调的 buffer）。
 * @param len     原始帧数据字节数。**须与 info 描述的一致**：内部按 width×height 校验下界，
 *                不匹配时抛 invalid_argument —— 否则会把小缓冲当大图交给 SDK 越界读。
 * @param quality JPEG 编码质量，有效区间 (50,99]；超出区间按 80 处理。
 * @param method  Bayer 插值方法 0-快速 1-均衡 2-最优 3-最优+；非 0..3 按 1（均衡）处理。
 */
std::vector<unsigned char> encodeCameraJpeg(const std::string& sn, const FrameInfo& info,
                                            const unsigned char* data, size_t len, int quality, int method);

/**
 * 临时强制 GigE 相机 IP（MV_GIGE_ForceIpEx；重启后恢复，不改持久配置）。
 * 参数为 "a.b.c.d" 字符串；网关可为 "0.0.0.0"。
 */
void forceCameraIp(const std::string& sn, const std::string& ip, const std::string& subnetMask,
                   const std::string& gateway);
