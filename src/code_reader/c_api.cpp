/** C ABI：异常 → HikCrResult；线程局部错误串 */

#include "hik_code_reader/c_api.h"
#include "code_reader.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
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
        std::vector<const char *> ptrs;
        ptrs.reserve(codeArr.size());
        for (const auto &s : codeArr) {
            ptrs.push_back(s.c_str());
        }
        cb(sn.c_str(), ptrs.data(), static_cast<int>(ptrs.size()), user);
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
                !copyTo(arr[i].net_export_ip, sizeof(arr[i].net_export_ip), devs[i].netExportIp)) {
                delete[] arr;
                throw std::runtime_error("hik_cr_enum_devices copy");
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
