/** C ABI：异常 → HikCvResult；线程局部错误串 */

#include "hik_mvcamera/c_api.h"
#include "camera.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

thread_local std::string g_err;

void err(std::string s) {
    g_err = std::move(s);
}

bool nonNull(const char* p, const char* name) {
    if (p) {
        return true;
    }
    err(std::string("null: ") + name);
    return false;
}

bool copyTo(char* dst, size_t cap, const std::string& src) {
    if (!cap || src.size() >= cap) {
        err(cap ? "field too long" : "zero cap");
        return false;
    }
    std::memcpy(dst, src.c_str(), src.size() + 1);
    return true;
}

CameraOpenParams cppCameraOpenParams(const HikCvOpenParams* c) {
    CameraOpenParams p{};
    if (!c) {
        return p;
    }
    if (c->trigger_mode && c->trigger_mode[0]) {
        p.triggerMode = c->trigger_mode;
    }
    if (c->trigger_source && c->trigger_source[0]) {
        p.triggerSource = c->trigger_source;
    }
    p.netTransMode = c->net_trans_mode;
    return p;
}

std::optional<CameraFrameCallback> cppFrame(int action, HikCvFrameCallback cb, void* user,
                                            const std::string& sn) {
    if (action == HIK_CV_FRAME_KEEP) {
        return std::nullopt;
    }
    if (action == HIK_CV_FRAME_CLEAR) {
        return CameraFrameCallback{};
    }
    if (action != HIK_CV_FRAME_SET) {
        throw std::invalid_argument("invalid frame_action");
    }
    if (!cb) {
        throw std::invalid_argument("frame_action=HIK_CV_FRAME_SET requires non-null frame_cb");
    }
    return CameraFrameCallback([cb, user, sn](const FrameInfo& fi, const unsigned char* data, size_t len) {
        HikCvFrameInfo cfi;
        cfi.width = fi.width;
        cfi.height = fi.height;
        cfi.pixel_type = fi.pixelType;
        cfi.frame_len = fi.frameLen;
        cfi.frame_num = fi.frameNum;
        cfi.host_timestamp = fi.hostTimestamp;
        cb(sn.c_str(), &cfi, data, len, user);
    });
}

template <typename F>
HikCvResult wrap(F&& f) {
    try {
        std::forward<F>(f)();
        return HIK_CV_OK;
    } catch (const std::invalid_argument& e) {
        err(e.what());
        return HIK_CV_ERR_INVALID_ARG;
    } catch (const std::logic_error& e) {
        err(e.what());
        return HIK_CV_ERR_LOGIC;
    } catch (const std::runtime_error& e) {
        err(e.what());
        return HIK_CV_ERR_RUNTIME;
    } catch (const std::bad_alloc&) {
        err("bad_alloc");
        return HIK_CV_ERR_NO_MEMORY;
    } catch (const std::exception& e) {
        err(e.what());
        return HIK_CV_ERR_UNKNOWN;
    } catch (...) {
        err("non-std exception");
        return HIK_CV_ERR_UNKNOWN;
    }
}

} // namespace

extern "C" {

HIK_CV_API HikCvResult hik_cv_enum_devices(HikCvDeviceInfo** out_list, int* out_count) {
    if (!out_list || !out_count) {
        return HIK_CV_ERR_INVALID_ARG;
    }
    *out_list = nullptr;
    *out_count = 0;
    return wrap([&] {
        std::vector<CameraInfo> devs = enumCamera();
        if (devs.empty()) {
            return;
        }
        auto* arr = new HikCvDeviceInfo[devs.size()];
        for (size_t i = 0; i < devs.size(); ++i) {
            if (!copyTo(arr[i].serial_number, sizeof(arr[i].serial_number), devs[i].serialNumber) ||
                !copyTo(arr[i].net_export_ip, sizeof(arr[i].net_export_ip), devs[i].netExportIp) ||
                !copyTo(arr[i].model_name, sizeof(arr[i].model_name), devs[i].modelName)) {
                delete[] arr;
                // copyTo 已把具体原因写入 g_err，保留精确错误而非通用文案
                throw std::runtime_error("hik_cv_enum_devices copy: " + g_err);
            }
        }
        *out_list = arr;
        *out_count = static_cast<int>(devs.size());
    });
}

HIK_CV_API void hik_cv_free_device_list(HikCvDeviceInfo* list) {
    delete[] list;
}

HIK_CV_API HikCvResult hik_cv_start_device(const char* serial_utf8, const HikCvOpenParams* open_params,
                                           int frame_action, HikCvFrameCallback frame_cb, void* user_data) {
    if (!nonNull(serial_utf8, "serial_utf8")) {
        return HIK_CV_ERR_INVALID_ARG;
    }
    if (frame_action != HIK_CV_FRAME_KEEP && frame_action != HIK_CV_FRAME_SET &&
        frame_action != HIK_CV_FRAME_CLEAR) {
        err("frame_action must be HIK_CV_FRAME_KEEP, HIK_CV_FRAME_SET, or HIK_CV_FRAME_CLEAR");
        return HIK_CV_ERR_INVALID_ARG;
    }
    if (frame_action == HIK_CV_FRAME_SET && !frame_cb) {
        err("HIK_CV_FRAME_SET requires non-null frame_cb");
        return HIK_CV_ERR_INVALID_ARG;
    }
    return wrap([&] {
        const std::string sn(serial_utf8);
        startCamera(sn, cppCameraOpenParams(open_params), cppFrame(frame_action, frame_cb, user_data, sn));
    });
}

HIK_CV_API HikCvResult hik_cv_stop_device(const char* serial_utf8) {
    if (!nonNull(serial_utf8, "serial_utf8")) {
        return HIK_CV_ERR_INVALID_ARG;
    }
    return wrap([&] { stopCamera(serial_utf8); });
}

HIK_CV_API HikCvResult hik_cv_trigger_device(const char* serial_utf8) {
    if (!nonNull(serial_utf8, "serial_utf8")) {
        return HIK_CV_ERR_INVALID_ARG;
    }
    return wrap([&] { triggerCamera(serial_utf8); });
}

HIK_CV_API HikCvResult hik_cv_force_ip(const char* serial_utf8, const char* ip, const char* subnet_mask,
                                       const char* gateway) {
    if (!nonNull(serial_utf8, "serial_utf8") || !nonNull(ip, "ip") || !nonNull(subnet_mask, "subnet_mask") ||
        !nonNull(gateway, "gateway")) {
        return HIK_CV_ERR_INVALID_ARG;
    }
    return wrap([&] { forceCameraIp(serial_utf8, ip, subnet_mask, gateway); });
}

HIK_CV_API HikCvResult hik_cv_set_param(const char* serial_utf8, const char* name,
                                        const HikCvParamValue* value) {
    if (!nonNull(serial_utf8, "serial_utf8") || !nonNull(name, "name") || !value) {
        return HIK_CV_ERR_INVALID_ARG;
    }
    return wrap([&] {
        const std::string sn(serial_utf8);
        switch (value->type) {
            case HIK_CV_PARAM_INT:
                setCameraParam(sn, name, static_cast<int64_t>(value->i));
                break;
            case HIK_CV_PARAM_FLOAT:
                setCameraParam(sn, name, static_cast<double>(value->f));
                break;
            case HIK_CV_PARAM_BOOL:
                setCameraParam(sn, name, value->b != 0);
                break;
            case HIK_CV_PARAM_ENUM:
                setCameraParam(sn, name, static_cast<uint32_t>(value->e));
                break;
            case HIK_CV_PARAM_COMMAND:
                runCameraCommand(sn, name);
                break;
            default:
                throw std::invalid_argument("invalid param type");
        }
    });
}

HIK_CV_API HikCvResult hik_cv_set_param_string(const char* serial_utf8, const char* name,
                                               const char* value) {
    if (!nonNull(serial_utf8, "serial_utf8") || !nonNull(name, "name") || !nonNull(value, "value")) {
        return HIK_CV_ERR_INVALID_ARG;
    }
    return wrap([&] { setCameraParam(serial_utf8, name, std::string(value)); });
}

HIK_CV_API HikCvResult hik_cv_get_param(const char* serial_utf8, const char* name,
                                        HikCvParamValue* out_value) {
    if (!nonNull(serial_utf8, "serial_utf8") || !nonNull(name, "name") || !out_value) {
        return HIK_CV_ERR_INVALID_ARG;
    }
    return wrap([&] {
        const CamParamValue v = getCameraParam(serial_utf8, name);
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                out_value->type = HIK_CV_PARAM_INT;
                out_value->i = val;
            } else if constexpr (std::is_same_v<T, double>) {
                out_value->type = HIK_CV_PARAM_FLOAT;
                out_value->f = val;
            } else if constexpr (std::is_same_v<T, bool>) {
                out_value->type = HIK_CV_PARAM_BOOL;
                out_value->b = val ? 1 : 0;
            } else if constexpr (std::is_same_v<T, uint32_t>) {
                out_value->type = HIK_CV_PARAM_ENUM;
                out_value->e = val;
            } else {
                throw std::logic_error("get_param: string 节点请用 hik_cv_get_param_string");
            }
        }, v);
    });
}

HIK_CV_API HikCvResult hik_cv_get_param_string(const char* serial_utf8, const char* name, char* out_utf8,
                                               size_t buf_size) {
    if (!nonNull(serial_utf8, "serial_utf8") || !nonNull(name, "name") || !out_utf8 || !buf_size) {
        return HIK_CV_ERR_INVALID_ARG;
    }
    return wrap([&] {
        const CamParamValue v = getCameraParam(serial_utf8, name);
        if (!std::holds_alternative<std::string>(v)) {
            throw std::logic_error("get_param_string: 节点非字符串类型，请用 hik_cv_get_param");
        }
        if (!copyTo(out_utf8, buf_size, std::get<std::string>(v))) {
            throw std::runtime_error("hik_cv_get_param_string copy");
        }
    });
}

HIK_CV_API size_t hik_cv_last_error_copy(char* out_utf8, size_t buf_size) {
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
