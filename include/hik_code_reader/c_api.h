/**
 * @file c_api.h
 * @brief 海康读码器封装的 **C 语言 ABI**，供 Python（ctypes / cffi）、Go（cgo）等调用。
 *
 * 约定：
 * - 所有函数线程安全与否与底层 C++ 一致；`hik_cr_last_error_copy` 使用线程局部存储。
 * - `const char *` 入参须为 UTF-8 且非 NULL（除非函数说明可 NULL）。
 * - 枚举设备返回的列表须用 `hik_cr_free_device_list` 释放。
 * - BCR 回调按序列号注册（`hik_cr_register_bcr_callback_for_serial`），在 SDK 线程触发，回调内勿长时间阻塞或再次调用本库（除非文档允许）；未注册序列号上的读码结果静默丢弃。
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

/** 与实现侧保持一致的字段上限（含结尾 '\\0'）。 */
#define HIK_CR_SERIAL_MAX 256
#define HIK_CR_IPV4_STR_MAX 64

typedef enum HikCrResult {
    HIK_CR_OK = 0,
    HIK_CR_ERR_UNKNOWN = 1,
    HIK_CR_ERR_LOGIC = 2,       /**< 状态/前置条件不符（如未取流却 trigger） */
    HIK_CR_ERR_RUNTIME = 3,     /**< SDK 或其它运行时错误 */
    HIK_CR_ERR_INVALID_ARG = 4, /**< 非法参数（如 NULL、IP 格式错误） */
    HIK_CR_ERR_NO_MEMORY = 5,
} HikCrResult;

typedef struct HikCrDeviceInfo {
    char serial_number[HIK_CR_SERIAL_MAX];
    char net_export_ip[HIK_CR_IPV4_STR_MAX];
} HikCrDeviceInfo;

/**
 * BCR 结果回调（按序列号注册，每台设备独立）。
 * `serial_utf8` 与 `codes` 在回调返回前有效；`code_count` 为 0 表示本轮无条码字符串。
 */
typedef void (*HikCrBcrCallback)(const char *serial_utf8, const char *const *codes, int code_count,
                                 void *user_data);

/**
 * 枚举在线读码器（当前实现仅 GigE）。
 * @param out_list 输出：成功时指向堆数组，须 `hik_cr_free_device_list`；失败时为 NULL。
 * @param out_count 输出元素个数
 */
HIK_CR_API HikCrResult hik_cr_enum_devices(HikCrDeviceInfo **out_list, int *out_count);

HIK_CR_API void hik_cr_free_device_list(HikCrDeviceInfo *list);

HIK_CR_API HikCrResult hik_cr_start_device(const char *serial_utf8);
HIK_CR_API HikCrResult hik_cr_stop_device(const char *serial_utf8);
HIK_CR_API HikCrResult hik_cr_open_device_for_parameters(const char *serial_utf8);

HIK_CR_API HikCrResult hik_cr_set_ip(const char *serial_utf8, const char *ip, const char *mask,
                                     const char *gateway);

/**
 * 为指定序列号注册/更新/移除 BCR 回调。同一序列号再次注册时新回调覆盖旧回调；cb==NULL 表示移除该序列号的回调。
 * 未注册回调的设备在读到码时静默丢弃（不向用户派发）。若设备已在取流，会尝试刷新 SDK 侧绑定。
 */
HIK_CR_API HikCrResult hik_cr_register_bcr_callback_for_serial(const char *serial_utf8, HikCrBcrCallback cb,
                                                               void *user_data);

HIK_CR_API HikCrResult hik_cr_trigger_device(const char *serial_utf8);

HIK_CR_API HikCrResult hik_cr_set_int_value(const char *serial_utf8, const char *key, int32_t value);
HIK_CR_API HikCrResult hik_cr_set_string_value(const char *serial_utf8, const char *key, const char *value);
HIK_CR_API HikCrResult hik_cr_set_bool_value(const char *serial_utf8, const char *key, int32_t non_zero);
HIK_CR_API HikCrResult hik_cr_set_float_value(const char *serial_utf8, const char *key, float value);
HIK_CR_API HikCrResult hik_cr_set_enum_value(const char *serial_utf8, const char *key, uint32_t value);
HIK_CR_API HikCrResult hik_cr_set_enum_value_by_string(const char *serial_utf8, const char *key,
                                                       const char *symbolic);

/**
 * 拷贝本线程最近一次失败的人类可读信息（UTF-8），不含结尾则截断。
 * @return 实际写入字节数（不含 '\\0'）；若 out==NULL 或 buf_size==0，返回所需缓冲区大小（含 '\\0'）。
 */
HIK_CR_API size_t hik_cr_last_error_copy(char *out_utf8, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif
