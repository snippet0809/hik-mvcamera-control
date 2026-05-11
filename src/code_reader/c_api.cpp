/**
 * @file c_api.cpp
 * @brief C ABI 实现：捕获 C++ 异常并映射为 HikCrResult，供 Python / Go 等 FFI。
 */

#include "hik_code_reader/c_api.h"

#include "code_reader.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

    thread_local std::string g_lastError;

    void setLastError(std::string msg) {
        g_lastError = std::move(msg);
    }

    bool requireNonNull(const char *p, const char *what) {
        if (p != nullptr) {
            return true;
        }
        setLastError(std::string("null pointer: ") + what);
        return false;
    }

    bool copyField(char *dest, size_t destCap, const std::string &src) {
        if (destCap == 0) {
            return false;
        }
        if (src.size() >= destCap) {
            setLastError("device field exceeds HIK_CR_*_MAX");
            return false;
        }
        std::memcpy(dest, src.c_str(), src.size() + 1);
        return true;
    }

    template <typename F>
    HikCrResult wrap(F &&f) {
        try {
            std::forward<F>(f)();
            return HIK_CR_OK;
        } catch (const std::invalid_argument &e) {
            setLastError(e.what());
            return HIK_CR_ERR_INVALID_ARG;
        } catch (const std::logic_error &e) {
            setLastError(e.what());
            return HIK_CR_ERR_LOGIC;
        } catch (const std::runtime_error &e) {
            setLastError(e.what());
            return HIK_CR_ERR_RUNTIME;
        } catch (const std::bad_alloc &) {
            setLastError("bad_alloc");
            return HIK_CR_ERR_NO_MEMORY;
        } catch (const std::exception &e) {
            setLastError(e.what());
            return HIK_CR_ERR_UNKNOWN;
        } catch (...) {
            setLastError("unknown non-std exception");
            return HIK_CR_ERR_UNKNOWN;
        }
    }

    std::mutex g_bcrMutex;
    HikCrBcrCallback g_bcrCb = nullptr;
    void *g_bcrUser = nullptr;

    void installBcrForwarderOnce() {
        static std::once_flag once;
        std::call_once(once, [] {
            registerImageCallback([](std::vector<std::string> codeArr) {
                HikCrBcrCallback cb = nullptr;
                void *ud = nullptr;
                {
                    std::lock_guard<std::mutex> lock(g_bcrMutex);
                    cb = g_bcrCb;
                    ud = g_bcrUser;
                }
                if (cb == nullptr) {
                    return;
                }
                std::vector<const char *> ptrs;
                ptrs.reserve(codeArr.size());
                for (const auto &s : codeArr) {
                    ptrs.push_back(s.c_str());
                }
                cb(ptrs.data(), static_cast<int>(ptrs.size()), ud);
            });
        });
    }

} // namespace

extern "C" {

HIK_CR_API HikCrResult hik_cr_enum_devices(HikCrDeviceInfo **out_list, int *out_count) {
    if (out_list == nullptr || out_count == nullptr) {
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
            if (!copyField(arr[i].serial_number, sizeof(arr[i].serial_number), devs[i].serialNumber) ||
                !copyField(arr[i].net_export_ip, sizeof(arr[i].net_export_ip), devs[i].netExportIp)) {
                delete[] arr;
                throw std::runtime_error("hik_cr_enum_devices: field copy failed");
            }
        }
        *out_list = arr;
        *out_count = static_cast<int>(devs.size());
    });
}

HIK_CR_API void hik_cr_free_device_list(HikCrDeviceInfo *list) {
    delete[] list;
}

HIK_CR_API HikCrResult hik_cr_start_device(const char *serial_utf8) {
    if (!requireNonNull(serial_utf8, "serial_utf8")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { startDevice(serial_utf8); });
}

HIK_CR_API HikCrResult hik_cr_stop_device(const char *serial_utf8) {
    if (!requireNonNull(serial_utf8, "serial_utf8")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { stopDevice(serial_utf8); });
}

HIK_CR_API HikCrResult hik_cr_open_device_for_parameters(const char *serial_utf8) {
    if (!requireNonNull(serial_utf8, "serial_utf8")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { openDeviceForParameters(serial_utf8); });
}

HIK_CR_API HikCrResult hik_cr_set_ip(const char *serial_utf8, const char *ip, const char *mask,
                                     const char *gateway) {
    if (!requireNonNull(serial_utf8, "serial_utf8") || !requireNonNull(ip, "ip") ||
        !requireNonNull(mask, "mask") || !requireNonNull(gateway, "gateway")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { setIp(serial_utf8, ip, mask, gateway); });
}

HIK_CR_API HikCrResult hik_cr_register_bcr_callback(HikCrBcrCallback cb, void *user_data) {
    return wrap([&] {
        installBcrForwarderOnce();
        std::lock_guard<std::mutex> lock(g_bcrMutex);
        g_bcrCb = cb;
        g_bcrUser = user_data;
    });
}

HIK_CR_API HikCrResult hik_cr_trigger_device(const char *serial_utf8) {
    if (!requireNonNull(serial_utf8, "serial_utf8")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { triggerDevice(serial_utf8); });
}

HIK_CR_API HikCrResult hik_cr_set_int_value(const char *serial_utf8, const char *key, int32_t value) {
    if (!requireNonNull(serial_utf8, "serial_utf8") || !requireNonNull(key, "key")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { setIntValue(serial_utf8, key, static_cast<int>(value)); });
}

HIK_CR_API HikCrResult hik_cr_set_string_value(const char *serial_utf8, const char *key, const char *value) {
    if (!requireNonNull(serial_utf8, "serial_utf8") || !requireNonNull(key, "key") ||
        !requireNonNull(value, "value")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { setStringValue(serial_utf8, key, value); });
}

HIK_CR_API HikCrResult hik_cr_set_bool_value(const char *serial_utf8, const char *key, int32_t non_zero) {
    if (!requireNonNull(serial_utf8, "serial_utf8") || !requireNonNull(key, "key")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { setBoolValue(serial_utf8, key, non_zero != 0); });
}

HIK_CR_API HikCrResult hik_cr_set_float_value(const char *serial_utf8, const char *key, float value) {
    if (!requireNonNull(serial_utf8, "serial_utf8") || !requireNonNull(key, "key")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { setFloatValue(serial_utf8, key, value); });
}

HIK_CR_API HikCrResult hik_cr_set_enum_value(const char *serial_utf8, const char *key, uint32_t value) {
    if (!requireNonNull(serial_utf8, "serial_utf8") || !requireNonNull(key, "key")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { setEnumValue(serial_utf8, key, value); });
}

HIK_CR_API HikCrResult hik_cr_set_enum_value_by_string(const char *serial_utf8, const char *key,
                                                       const char *symbolic) {
    if (!requireNonNull(serial_utf8, "serial_utf8") || !requireNonNull(key, "key") ||
        !requireNonNull(symbolic, "symbolic")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { setEnumValueByString(serial_utf8, key, symbolic); });
}

HIK_CR_API size_t hik_cr_last_error_copy(char *out_utf8, size_t buf_size) {
    const std::string &e = g_lastError;
    const size_t need = e.size() + 1;
    if (out_utf8 == nullptr || buf_size == 0) {
        return need;
    }
    const size_t n = std::min(e.size(), buf_size - 1);
    std::memcpy(out_utf8, e.data(), n);
    out_utf8[n] = '\0';
    return n;
}

} // extern "C"
