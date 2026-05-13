/**
 * @file c_api.cpp
 * @brief C ABI 实现：捕获 C++ 异常并映射为 HikCrResult，供 Python / Go 等 FFI。
 */

#include "hik_code_reader/c_api.h"

#include "code_reader.h"
#include "code_reader_detail.h"

#include <algorithm>
#include <cstring>
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

HIK_CR_API HikCrResult hik_cr_register_bcr_callback_for_serial(const char *serial_utf8, HikCrBcrCallback cb,
                                                               void *user_data) {
    if (!requireNonNull(serial_utf8, "serial_utf8")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] {
        const std::string sn(serial_utf8);
        if (cb == nullptr) {
            registerImageCallbackForSerial(sn, {});
            return;
        }
        registerImageCallbackForSerial(sn, [cb, user_data, sn](std::vector<std::string> codeArr) {
            std::vector<const char *> ptrs;
            ptrs.reserve(codeArr.size());
            for (const auto &s : codeArr) {
                ptrs.push_back(s.c_str());
            }
            cb(sn.c_str(), ptrs.data(), static_cast<int>(ptrs.size()), user_data);
        });
    });
}

HIK_CR_API HikCrResult hik_cr_trigger_device(const char *serial_utf8) {
    if (!requireNonNull(serial_utf8, "serial_utf8")) {
        return HIK_CR_ERR_INVALID_ARG;
    }
    return wrap([&] { triggerDevice(serial_utf8); });
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
