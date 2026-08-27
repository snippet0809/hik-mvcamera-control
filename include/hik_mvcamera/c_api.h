/**
 * @file c_api.h
 * @brief 海康工业相机 C ABI（Python ctypes / Go cgo / Node N-API）。与 C++ `camera.h` 对齐。
 *
 * - UTF-8；指针可 NULL 处见各函数说明。
 * - 枚举结果须 `hik_cv_free_device_list` 释放。
 * - 图像回调仅通过 `hik_cv_start_device` 的 `frame_action` / `frame_cb` 登记或清除；
 *   未登记序列号上的帧丢弃。
 * - 参数：数值（Int/Float/Bool/Enum）走 `hik_cv_set_param` / `hik_cv_get_param`；
 *   字符串走 `hik_cv_set_param_string` / `hik_cv_get_param_string`；命令走 `hik_cv_set_param`
 *   （type=HIK_CV_PARAM_COMMAND，`name` 即命令节点名）。
 */

#ifndef HIK_MVCAMERA_C_API_H
#define HIK_MVCAMERA_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(HIK_CV_COMPILE_DLL)
#define HIK_CV_API __declspec(dllexport)
#elif defined(HIK_CV_USE_DLL)
#define HIK_CV_API __declspec(dllimport)
#else
#define HIK_CV_API
#endif
#elif defined(HIK_CV_COMPILE_DLL)
#define HIK_CV_API __attribute__((visibility("default")))
#else
#define HIK_CV_API
#endif

#define HIK_CV_SERIAL_MAX 128
#define HIK_CV_IPV4_STR_MAX 64
#define HIK_CV_MODEL_MAX 64
#define HIK_CV_STRING_MAX 256

typedef enum HikCvResult {
    HIK_CV_OK = 0,
    HIK_CV_ERR_UNKNOWN = 1,
    HIK_CV_ERR_LOGIC = 2,
    HIK_CV_ERR_RUNTIME = 3,
    HIK_CV_ERR_INVALID_ARG = 4,
    HIK_CV_ERR_NO_MEMORY = 5,
} HikCvResult;

typedef struct HikCvDeviceInfo {
    char serial_number[HIK_CV_SERIAL_MAX];
    char net_export_ip[HIK_CV_IPV4_STR_MAX];  // 仅 GigE 有；USB 为空
    char model_name[HIK_CV_MODEL_MAX];
} HikCvDeviceInfo;

typedef struct HikCvFrameInfo {
    unsigned int width;
    unsigned int height;
    unsigned int pixel_type;   // MvGvspPixelType
    unsigned int frame_len;
    unsigned int frame_num;
    uint64_t host_timestamp;
} HikCvFrameInfo;

/** 起流前 GenICam 项；NULL 或空串表示不修改该节点。 */
typedef struct HikCvOpenParams {
    const char* trigger_mode;
    const char* trigger_source;
    int net_trans_mode;  // 0=不设置（SDK 默认驱动模式）; 1=驱动; 2=socket（免 GigE 过滤驱动）
    int width;           // >0 时起流前写 Width
    int height;          // >0 时起流前写 Height（线阵相机：每帧行数）
} HikCvOpenParams;

typedef enum HikCvParamType {
    HIK_CV_PARAM_INT = 0,
    HIK_CV_PARAM_FLOAT = 1,
    HIK_CV_PARAM_BOOL = 2,
    HIK_CV_PARAM_ENUM = 3,
    HIK_CV_PARAM_STRING = 4,
    HIK_CV_PARAM_COMMAND = 5,
} HikCvParamType;

/** 参数值（数值）；字符串经 `hik_cv_set_param_string` / `hik_cv_get_param_string` 传递。 */
typedef struct HikCvParamValue {
    HikCvParamType type;
    union {
        int64_t i;
        double f;
        int b;  // bool：0/1
        uint32_t e;
    };
} HikCvParamValue;

/** `hik_cv_start_device` 的图像回调行为（对应 C++ `std::optional` 第三参）。 */
#define HIK_CV_FRAME_KEEP 0   /**< 不改动已登记的图像回调 */
#define HIK_CV_FRAME_SET 1    /**< 设置 `frame_cb`（须非 NULL） */
#define HIK_CV_FRAME_CLEAR 2  /**< 清除该序列号图像回调 */

/**
 * 图像回调（SDK 抓图线程调用）。
 * `data` 指向 SDK 缓冲，仅回调期内有效（须同步消费/拷贝）。
 */
typedef void (*HikCvFrameCallback)(const char* serial_utf8, const HikCvFrameInfo* info,
                                   const unsigned char* data, size_t len, void* user_data);

HIK_CV_API HikCvResult hik_cv_enum_devices(HikCvDeviceInfo** out_list, int* out_count);
HIK_CV_API void hik_cv_free_device_list(HikCvDeviceInfo* list);

/**
 * 起流：`open_params` 为 NULL 表示不写 TriggerMode/TriggerSource；`frame_action` 见 HIK_CV_FRAME_*。
 * 已在取流时忽略 `open_params`，仅按 `frame_action` 更新图像回调。
 */
HIK_CV_API HikCvResult hik_cv_start_device(const char* serial_utf8, const HikCvOpenParams* open_params,
                                           int frame_action, HikCvFrameCallback frame_cb, void* user_data);

HIK_CV_API HikCvResult hik_cv_stop_device(const char* serial_utf8);
HIK_CV_API HikCvResult hik_cv_trigger_device(const char* serial_utf8);

/** 临时强制 GigE 相机 IP（重启恢复，不改持久配置）；均为 "a.b.c.d" 字符串。 */
HIK_CV_API HikCvResult hik_cv_force_ip(const char* serial_utf8, const char* ip, const char* subnet_mask,
                                       const char* gateway);

/** 数值参数读写（Int/Float/Bool/Enum/Command）。 */
HIK_CV_API HikCvResult hik_cv_set_param(const char* serial_utf8, const char* name,
                                        const HikCvParamValue* value);
HIK_CV_API HikCvResult hik_cv_get_param(const char* serial_utf8, const char* name,
                                        HikCvParamValue* out_value);
/** 字符串参数读写。 */
HIK_CV_API HikCvResult hik_cv_set_param_string(const char* serial_utf8, const char* name,
                                               const char* value);
HIK_CV_API HikCvResult hik_cv_get_param_string(const char* serial_utf8, const char* name,
                                               char* out_utf8, size_t buf_size);

/** 失败信息（线程局部）；返回所需缓冲（含 '\0'）或已写入长度（不含 '\0'）。 */
HIK_CV_API size_t hik_cv_last_error_copy(char* out_utf8, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif
