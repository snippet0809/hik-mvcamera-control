/** C ABI：异常 → HikCrResult；线程局部错误串 */

#include "hik_code_reader/c_api.h"
#include "code_reader.h"
#include "code_reader_detail.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace {

thread_local std::string g_err;

void err(std::string s) {
    g_err = std::move(s);
}

bool nonNull(const char *p, const char *name) {
    if (p) {
        return true;
    }
    err(std::string("null: ") + name);
    return false;
}

bool copyTo(char *dst, size_t cap, const std::string &src) {
    if (!cap || src.size() >= cap) {
        err(cap ? "field too long" : "zero cap");
        return false;
    }
    std::memcpy(dst, src.c_str(), src.size() + 1);
    return true;
}

CodeReaderOpenParams cppOpenParams(const HikCrOpenParams *c) {
    CodeReaderOpenParams p{};
    if (!c) {
        return p;
    }
    if (c->trigger_mode && c->trigger_mode[0]) {
        p.triggerMode = c->trigger_mode;
    }
    if (c->trigger_source && c->trigger_source[0]) {
        p.triggerSource = c->trigger_source;
    }
    if (c->code128 >= 0) {
        p.code128 = c->code128 != 0;
    }
    if (c->qrcode >= 0) {
        p.qrcode = c->qrcode != 0;
    }
    return p;
}

// 最近一次 BCR 解码结果（按序列号保留）。修复：回调传出的 codes 指针原先指向 C++ lambda
// 栈上临时 vector，返回后即失效；FFI 若把回调排到其他线程/事件循环延迟消费（如 koffi →
// JS 主线程、ctypes 异步），就会 use-after-free。这里把结果常驻到下次解码前，保证指针
// 在回调期间及其后一段窗口内仍可安全读取。
struct KeptBcr {
    std::vector<std::string> strings;
    std::vector<const char *> ptrs;
};
std::unordered_map<std::string, std::shared_ptr<KeptBcr>> g_last_bcr;

std::optional<CodeReaderBcrCallback> cppBcr(int action, HikCrBcrCallback cb, void *user, const std::string &sn) {
    if (action == HIK_CR_BCR_KEEP) {
        return std::nullopt;
    }
    if (action == HIK_CR_BCR_CLEAR) {
        return CodeReaderBcrCallback{};
    }
    if (action != HIK_CR_BCR_SET) {
        throw std::invalid_argument("invalid bcr_action");
    }
    if (!cb) {
        throw std::invalid_argument("bcr_action=HIK_CR_BCR_SET requires non-null bcr_cb");
    }
    return CodeReaderBcrCallback([cb, user, sn](std::vector<std::string> codeArr) {
        auto kept = std::make_shared<KeptBcr>();
        kept->strings = std::move(codeArr);
        kept->ptrs.reserve(kept->strings.size());
        for (const auto &s : kept->strings) {
            kept->ptrs.push_back(s.c_str());
        }
        {
            std::lock_guard<std::mutex> lock(g_device_mutex);
            g_last_bcr[sn] = kept;
        }
        cb(sn.c_str(), kept->ptrs.data(), static_cast<int>(kept->ptrs.size()), user);
    });
}

template <typename F>
HikCrResult wrap(F &&f) {
    try {
        std::forward<F>(f)();
        return HIK_CR_OK;
    } catch (const std::invalid_argument &e) {
        err(e.what());
        return HIK_CR_ERR_INVALID_ARG;
    } catch (const std::logic_error &e) {
        err(e.what());
        return HIK_CR_ERR_LOGIC;
    } catch (const std::runtime_error &e) {
        err(e.what());
        return HIK_CR_ERR_RUNTIME;
    } catch (const std::bad_alloc &) {
        err("bad_alloc");
        return HIK_CR_ERR_NO_MEMORY;
    } catch (const std::exception &e) {
        err(e.what());
        return HIK_CR_ERR_UNKNOWN;
    } catch (...) {
        err("non-std exception");
        return HIK_CR_ERR_UNKNOWN;
    }
}

} // namespace

extern "C" {

HIK_CR_API HikCrResult hik_cr_enum_devices(HikCrDeviceInfo **out_list, int *out_count) {
    if (!out_list || !out_count) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    *out_list = nullptr;
    *out_count = 0;
    return wrap([&] {
        std::vector<CodeReaderInfo> devs = enumDevice();
        if (devs.empty()) {
            return;
        }
        auto *arr = new HikCrDeviceInfo[devs.size()];
        for (size_t i = 0; i < devs.size(); ++i) {
            if (!copyTo(arr[i].serial_number, sizeof(arr[i].serial_number), devs[i].serialNumber) ||
                !copyTo(arr[i].net_export_ip, sizeof(arr[i].net_export_ip), devs[i].netExportIp) ||
                !copyTo(arr[i].model_name, sizeof(arr[i].model_name), devs[i].modelName)) {
                delete[] arr;
                // copyTo 已把具体原因写入 g_err，保留精确错误而非通用文案
                throw std::runtime_error("hik_cr_enum_devices copy: " + g_err);
            }
        }
        *out_list = arr;
        *out_count = static_cast<int>(devs.size());
    });
}

HIK_CR_API void hik_cr_free_device_list(HikCrDeviceInfo *list) {
    delete[] list;
}

HIK_CR_API HikCrResult hik_cr_start_device(const char *serial_utf8, const HikCrOpenParams *open_params,
                                           int bcr_action, HikCrBcrCallback bcr_cb, void *bcr_user_data) {
    if (!nonNull(serial_utf8, "serial_utf8")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    if (bcr_action != HIK_CR_BCR_KEEP && bcr_action != HIK_CR_BCR_SET && bcr_action != HIK_CR_BCR_CLEAR) {
        err("bcr_action must be HIK_CR_BCR_KEEP, HIK_CR_BCR_SET, or HIK_CR_BCR_CLEAR");
        return HIK_CR_ERR_INVALID_ARG;
    }
    if (bcr_action == HIK_CR_BCR_SET && !bcr_cb) {
        err("HIK_CR_BCR_SET requires non-null bcr_cb");
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] {
        const std::string sn(serial_utf8);
        startDevice(sn, cppOpenParams(open_params), cppBcr(bcr_action, bcr_cb, bcr_user_data, sn));
    });
}

HIK_CR_API HikCrResult hik_cr_stop_device(const char *serial_utf8) {
    if (!nonNull(serial_utf8, "serial_utf8")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { stopDevice(serial_utf8); });
}

HIK_CR_API HikCrResult hik_cr_trigger_device(const char *serial_utf8) {
    if (!nonNull(serial_utf8, "serial_utf8")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { triggerDevice(serial_utf8); });
}

HIK_CR_API HikCrResult hik_cr_set_param(const char *serial_utf8, const char *name,
                                        const HikCrParamValue *value) {
    if (!nonNull(serial_utf8, "serial_utf8") || !nonNull(name, "name") || !value) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] {
        const std::string sn(serial_utf8);
        switch (value->type) {
            case HIK_CR_PARAM_INT:
                setReaderParam(sn, name, static_cast<int64_t>(value->i));
                break;
            case HIK_CR_PARAM_FLOAT:
                setReaderParam(sn, name, static_cast<double>(value->f));
                break;
            case HIK_CR_PARAM_BOOL:
                setReaderParam(sn, name, value->b != 0);
                break;
            case HIK_CR_PARAM_ENUM:
                setReaderParam(sn, name, static_cast<uint32_t>(value->e));
                break;
            case HIK_CR_PARAM_COMMAND:
                runReaderCommand(sn, name);
                break;
            default:
                throw std::invalid_argument("invalid param type");
        }
    });
}

HIK_CR_API HikCrResult hik_cr_set_param_string(const char *serial_utf8, const char *name,
                                               const char *value) {
    if (!nonNull(serial_utf8, "serial_utf8") || !nonNull(name, "name") || !nonNull(value, "value")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { setReaderParam(serial_utf8, name, std::string(value)); });
}

HIK_CR_API HikCrResult hik_cr_get_param(const char *serial_utf8, const char *name,
                                        HikCrParamValue *out_value) {
    if (!nonNull(serial_utf8, "serial_utf8") || !nonNull(name, "name") || !out_value) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] {
        const CodeReaderParamValue v = getReaderParam(serial_utf8, name);
        std::visit([&](const auto &val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                out_value->type = HIK_CR_PARAM_INT;
                out_value->i = val;
            } else if constexpr (std::is_same_v<T, double>) {
                out_value->type = HIK_CR_PARAM_FLOAT;
                out_value->f = val;
            } else if constexpr (std::is_same_v<T, bool>) {
                out_value->type = HIK_CR_PARAM_BOOL;
                out_value->b = val ? 1 : 0;
            } else if constexpr (std::is_same_v<T, uint32_t>) {
                out_value->type = HIK_CR_PARAM_ENUM;
                out_value->e = val;
            } else {
                throw std::logic_error("get_param: string 节点请用 hik_cr_get_param_string");
            }
        }, v);
    });
}

HIK_CR_API HikCrResult hik_cr_get_param_string(const char *serial_utf8, const char *name, char *out_utf8,
                                               size_t buf_size) {
    if (!nonNull(serial_utf8, "serial_utf8") || !nonNull(name, "name") || !out_utf8 || !buf_size) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] {
        const CodeReaderParamValue v = getReaderParam(serial_utf8, name);
        if (!std::holds_alternative<std::string>(v)) {
            throw std::logic_error("get_param_string: 节点非字符串类型，请用 hik_cr_get_param");
        }
        if (!copyTo(out_utf8, buf_size, std::get<std::string>(v))) {
            throw std::runtime_error("hik_cr_get_param_string copy");
        }
    });
}

HIK_CR_API size_t hik_cr_last_error_copy(char *out_utf8, size_t buf_size) {
    const size_t need = g_err.size() + 1;
    if (!out_utf8 || !buf_size) {
        return need;
    }
    const size_t n = std::min(g_err.size(), buf_size - 1);
    std::memcpy(out_utf8, g_err.data(), n);
    out_utf8[n] = '\0';
    return n;
}

} // extern "C"
