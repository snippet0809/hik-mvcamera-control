/**
 * @file c_api.h
 * @brief 海康读码器 C ABI（Python ctypes / Go cgo）。与 C++ `code_reader.h` 对齐：枚举、起流、停流、触发、参数读写、读码成功帧回调。
 *
 * - UTF-8；指针可 NULL 处见各函数说明。
 * - 枚举结果须 `hik_cr_free_device_list` 释放。
 * - BCR 仅通过 `hik_cr_start_device` 的 `bcr_action` / `bcr_cb` 登记或清除；未登记序列号上的读码结果丢弃。
 * - 读码成功帧回调通过 `hik_cr_set_frame_callback` 独立登记/清除（可热替换，不依赖 `hik_cr_start_device`）。
 * - 参数：数值（Int/Float/Bool/Enum）走 `hik_cr_set_param` / `hik_cr_get_param`；
 *   字符串走 `hik_cr_set_param_string` / `hik_cr_get_param_string`；命令走 `hik_cr_set_param`
 *   （type=HIK_CR_PARAM_COMMAND，`name` 即命令节点名）。设备须已 `hik_cr_start_device`。
 */

#ifndef HIK_CODE_READER_C_API_H
#define HIK_CODE_READER_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(HIK_CR_COMPILE_DLL)
#define HIK_CR_API __declspec(dllexport)
#elif defined(HIK_CR_USE_DLL)
#define HIK_CR_API __declspec(dllimport)
#else
#define HIK_CR_API
#endif
#elif defined(HIK_CR_COMPILE_DLL)
#define HIK_CR_API __attribute__((visibility("default")))
#else
#define HIK_CR_API
#endif

#define HIK_CR_SERIAL_MAX 256
#define HIK_CR_IPV4_STR_MAX 64
#define HIK_CR_MODEL_MAX 64
/** 字符串参数读取的建议缓冲（与相机侧 HIK_CV_STRING_MAX 一致）；超长时读取报 "field too long"。 */
#define HIK_CR_PARAM_STRING_MAX 256

typedef enum HikCrResult {
    HIK_CR_OK = 0,
    HIK_CR_ERR_UNKNOWN = 1,
    HIK_CR_ERR_LOGIC = 2,
    HIK_CR_ERR_RUNTIME = 3,
    HIK_CR_ERR_INVALID_ARG = 4,
    HIK_CR_ERR_NO_MEMORY = 5,
} HikCrResult;

typedef struct HikCrDeviceInfo {
    char serial_number[HIK_CR_SERIAL_MAX];
    char net_export_ip[HIK_CR_IPV4_STR_MAX];
    char model_name[HIK_CR_MODEL_MAX];  // 设备型号（如 MV-IDB005EX=读码器、MV-CU013=相机），用于区分读码器/相机
} HikCrDeviceInfo;

/**
 * 起流前 GenICam 项（与 C++ CodeReaderOpenParams 一致）。
 * @note `trigger_mode` / `trigger_source`：NULL 或空串表示用默认（On / Software）。
 *       `code128` / `qrcode`：负数表示用默认（true），0 为 false，正数为 true。
 */
typedef struct HikCrOpenParams {
    const char *trigger_mode;
    const char *trigger_source;
    int code128;
    int qrcode;
} HikCrOpenParams;

/** 参数类型（对应 C++ `CodeReaderParamValue` 的 variant 分支）。 */
typedef enum HikCrParamType {
    HIK_CR_PARAM_INT = 0,
    HIK_CR_PARAM_FLOAT = 1,
    HIK_CR_PARAM_BOOL = 2,
    HIK_CR_PARAM_ENUM = 3,
    HIK_CR_PARAM_STRING = 4,
    HIK_CR_PARAM_COMMAND = 5,
} HikCrParamType;

/** 参数值（数值）；字符串经 `hik_cr_set_param_string` / `hik_cr_get_param_string` 传递。 */
typedef struct HikCrParamValue {
    HikCrParamType type;
    union {
        int64_t i;
        double f;
        int b;  // bool：0/1
        uint32_t e;
    };
} HikCrParamValue;

/** 最近一次 BCR 成功的读码帧图信息（供 `hik_cr_get_bcr_image` 拉取）。 */
typedef struct HikCrBcrImageInfo {
    int width;
    int height;
    int pixel_type;  // MvCodeReaderGvspPixelType
    int frame_len;   // 图像数据字节数
} HikCrBcrImageInfo;

/** `hik_cr_start_device` 的 BCR 行为（对应 C++ `std::optional` 第三参）。 */
#define HIK_CR_BCR_KEEP 0   /**< 不改动已登记的 BCR */
#define HIK_CR_BCR_SET 1    /**< 设置 `bcr_cb`（须非 NULL） */
#define HIK_CR_BCR_CLEAR 2  /**< 清除该序列号 BCR */

typedef void (*HikCrBcrCallback)(const char *serial_utf8, const char *const *codes, int code_count,
                                 void *user_data);

/** 读码成功帧的元数据（对齐 C++ 读码器 IMAGE_OUT_INFO）。`pixel_type` 为读码器 GVSP 像素值（数值与相机 MvGvspPixelType 一致）。 */
typedef struct HikCrFrameInfo {
    unsigned int width;
    unsigned int height;
    unsigned int pixel_type;
    unsigned int frame_len;
    unsigned int frame_num;
} HikCrFrameInfo;

/** `hik_cr_set_frame_callback` 的帧回调行为（独立于 BCR，可热替换）。 */
#define HIK_CR_FRAME_KEEP 0   /**< 不改动已登记的读码成功帧回调 */
#define HIK_CR_FRAME_SET 1    /**< 设置 `frame_cb`（须非 NULL） */
#define HIK_CR_FRAME_CLEAR 2  /**< 清除该序列号读码成功帧回调 */

/**
 * 读码成功帧回调（SDK 图像回调线程调用，仅 `bIsGetCode` 帧触发）。
 * `data` 指向 SDK 缓冲，仅回调期内有效（须同步消费/拷贝）。
 */
typedef void (*HikCrFrameCallback)(const char *serial_utf8, const HikCrFrameInfo *info,
                                   const unsigned char *data, size_t len, void *user_data);

HIK_CR_API HikCrResult hik_cr_enum_devices(HikCrDeviceInfo **out_list, int *out_count);
HIK_CR_API void hik_cr_free_device_list(HikCrDeviceInfo *list);

/**
 * 起流：`open_params` 为 NULL 表示全默认；`bcr_action` 见 HIK_CR_BCR_*。
 * 已在取流时忽略 `open_params`，仅按 `bcr_action` 更新 BCR。
 */
HIK_CR_API HikCrResult hik_cr_start_device(const char *serial_utf8, const HikCrOpenParams *open_params,
                                           int bcr_action, HikCrBcrCallback bcr_cb, void *bcr_user_data);

HIK_CR_API HikCrResult hik_cr_stop_device(const char *serial_utf8);

/**
 * 停流但保留连接（不 CloseDevice）；未开流时为空操作。
 * 与 `hik_cr_stop_device` 的区别：设备仍被本进程占用，下次 `hik_cr_start_device` 只重写
 * `open_params` 并重新 StartGrabbing，省掉重建句柄 + OpenDevice 的开销。
 * 需要把设备让给外部软件（如 IDMVS）时仍用 `hik_cr_stop_device` 彻底释放。
 */
HIK_CR_API HikCrResult hik_cr_stop_grabbing(const char *serial_utf8);

HIK_CR_API HikCrResult hik_cr_trigger_device(const char *serial_utf8);

/**
 * 登记/清除读码成功帧回调（`frame_action` 见 HIK_CR_FRAME_*）。独立于 BCR、可在已取流时热替换；
 * 设备未起流时登记，起流后自动生效。未登记时读码成功帧仅走 BCR，图像不转发。
 */
HIK_CR_API HikCrResult hik_cr_set_frame_callback(const char *serial_utf8, int frame_action,
                                                 HikCrFrameCallback frame_cb, void *frame_user_data);

/** 数值参数读写（Int/Float/Bool/Enum/Command）；设备须已 startDevice。 */
HIK_CR_API HikCrResult hik_cr_set_param(const char *serial_utf8, const char *name,
                                        const HikCrParamValue *value);
HIK_CR_API HikCrResult hik_cr_get_param(const char *serial_utf8, const char *name,
                                        HikCrParamValue *out_value);
/** 字符串参数读写（含枚举 symbolic 值）。 */
HIK_CR_API HikCrResult hik_cr_set_param_string(const char *serial_utf8, const char *name,
                                               const char *value);
HIK_CR_API HikCrResult hik_cr_get_param_string(const char *serial_utf8, const char *name,
                                               char *out_utf8, size_t buf_size);

/**
 * 取最近一次 BCR 成功的读码帧图（返回拷贝，避免回调缓冲失效）。
 * `out_data` 为 NULL 时仅回填 `out_info`（先查长度再分配缓冲）；
 * `out_cap` 不足或该序列号未读到过条码时报错。
 */
HIK_CR_API HikCrResult hik_cr_get_bcr_image(const char *serial_utf8, HikCrBcrImageInfo *out_info,
                                            unsigned char *out_data, size_t out_cap);

/** 失败信息（线程局部）；返回所需缓冲（含 '\\0'）或已写入长度（不含 '\\0'）。 */
HIK_CR_API size_t hik_cr_last_error_copy(char *out_utf8, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif
