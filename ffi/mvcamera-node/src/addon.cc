/**
 * @file addon.cc
 * @brief N-API 原生插件：封装 hik_mvcamera C ABI（hik_cv_*）。
 *
 * - 链接 `hik_mvcamera.lib`（DLL 导入库），运行时加载同一个 hik_mvcamera.dll。
 * - 图像回调：SDK 抓图线程调用 C ABI 回调 → 帧数据同步拷入 payload → `napi_threadsafe_function`
 *   排到 JS 主线程 → 拷成 `Napi::Buffer` → 调 JS 回调 `(serial, frameInfo, buffer)`。
 *   创建 tsfn 后 `Unref`（不 hold 事件循环，镜像 ffi/node 的读码器修复）。
 * - 错误：`HikCvResult != OK` 时 throw `Napi::Error`，消息取 `hik_cv_last_error_copy`。
 */

#include <napi.h>

#include "hik_mvcamera/c_api.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// 图像回调注册表（JS 侧 tsfn）
// ---------------------------------------------------------------------------

/** 帧载荷：从 SDK 抓图线程同步拷出的帧 + 元数据。 */
struct FramePayload {
    std::string serial;
    HikCvFrameInfo info;
    std::vector<unsigned char> buffer;
};

/** 每个序列号的图像回调条目；tsfn 保活 JS 函数，需显式 Release。 */
struct FrameEntry {
    explicit FrameEntry(Napi::ThreadSafeFunction fn) : tsfn(std::move(fn)) {}
    Napi::ThreadSafeFunction tsfn;
};

std::unordered_map<std::string, std::shared_ptr<FrameEntry>> g_frames;
std::mutex g_framesMutex;

void clearAllFrames() {
    std::unordered_map<std::string, std::shared_ptr<FrameEntry>> taken;
    {
        std::lock_guard<std::mutex> lk(g_framesMutex);
        taken.swap(g_frames);
    }
    for (auto& kv : taken) {
        kv.second->tsfn.Release();
    }
}

void releaseFrame(const std::string& serial) {
    std::shared_ptr<FrameEntry> old;
    {
        std::lock_guard<std::mutex> lk(g_framesMutex);
        auto it = g_frames.find(serial);
        if (it != g_frames.end()) {
            old = it->second;
            g_frames.erase(it);
        }
    }
    if (old) {
        old->tsfn.Release();
    }
}

/** C ABI 图像回调（SDK 抓图线程）；把帧拷入 payload 排到主线程。 */
void frameBridge(const char* serial_utf8, const HikCvFrameInfo* info, const unsigned char* data, size_t len,
                 void* /*user_data*/) {
    if (!serial_utf8 || !info) {
        return;
    }
    const std::string sn(serial_utf8);

    std::shared_ptr<FrameEntry> entry;
    {
        std::lock_guard<std::mutex> lk(g_framesMutex);
        auto it = g_frames.find(sn);
        if (it == g_frames.end()) {
            return;
        }
        entry = it->second;
    }

    auto* payload = new FramePayload();
    payload->serial = sn;
    payload->info = *info;
    if (data && len > 0) {
        payload->buffer.assign(data, data + len);
    }

    const napi_status status = entry->tsfn.NonBlockingCall(
        payload, [](Napi::Env cbEnv, Napi::Function jsCallback, FramePayload* p) {
            Napi::Object infoObj = Napi::Object::New(cbEnv);
            infoObj.Set("width", Napi::Number::New(cbEnv, p->info.width));
            infoObj.Set("height", Napi::Number::New(cbEnv, p->info.height));
            infoObj.Set("pixelType", Napi::Number::New(cbEnv, p->info.pixel_type));
            infoObj.Set("frameLen", Napi::Number::New(cbEnv, p->info.frame_len));
            infoObj.Set("frameNum", Napi::Number::New(cbEnv, p->info.frame_num));
            infoObj.Set("hostTimestamp", Napi::Number::New(cbEnv, static_cast<double>(p->info.host_timestamp)));

            Napi::Buffer<unsigned char> buf = Napi::Buffer<unsigned char>::New(cbEnv, p->buffer.size());
            if (!p->buffer.empty()) {
                std::memcpy(buf.Data(), p->buffer.data(), p->buffer.size());
            }
            jsCallback.Call({Napi::String::New(cbEnv, p->serial), infoObj, buf});
            delete p;
        });
    if (status != napi_ok) {
        delete payload;  // 未入队（如 tsfn 已关闭）时回收
    }
}

// ---------------------------------------------------------------------------
// 辅助
// ---------------------------------------------------------------------------

std::string lastErrorString() {
    const size_t need = hik_cv_last_error_copy(nullptr, 0);
    if (need <= 1) {
        return "";
    }
    std::vector<char> buf(need);
    hik_cv_last_error_copy(buf.data(), buf.size());
    return std::string(buf.data());
}

void check(Napi::Env env, HikCvResult r) {
    if (r == HIK_CV_OK) {
        return;
    }
    throw Napi::Error::New(env,
                           "hik_cv error " + std::to_string(static_cast<int>(r)) + ": " + lastErrorString());
}

std::string requireSerial(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        throw Napi::TypeError::New(env, "serial must be a string");
    }
    return info[0].As<Napi::String>().Utf8Value();
}

// ---------------------------------------------------------------------------
// N-API 导出函数
// ---------------------------------------------------------------------------

Napi::Value EnumDevices(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    HikCvDeviceInfo* arr = nullptr;
    int count = 0;
    const HikCvResult r = hik_cv_enum_devices(&arr, &count);
    check(env, r);

    Napi::Array out = Napi::Array::New(env, count > 0 ? static_cast<size_t>(count) : 0);
    if (arr && count > 0) {
        for (int i = 0; i < count; ++i) {
            Napi::Object obj = Napi::Object::New(env);
            obj.Set("serialNumber", Napi::String::New(env, arr[i].serial_number));
            obj.Set("netExportIp", Napi::String::New(env, arr[i].net_export_ip));
            obj.Set("modelName", Napi::String::New(env, arr[i].model_name));
            out.Set(i, obj);
        }
    }
    hik_cv_free_device_list(arr);
    return out;
}

/** startDevice(serial, paramsOrNull, frameAction, callbackOrNull) */
Napi::Value StartDevice(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);

    HikCvOpenParams copen{};
    HikCvOpenParams* copenPtr = nullptr;
    std::string tmStr;
    std::string tsStr;
    if (info.Length() >= 2 && info[1].IsObject()) {
        Napi::Object p = info[1].As<Napi::Object>();
        copenPtr = &copen;
        if (p.Has("trigger_mode") && p.Get("trigger_mode").IsString()) {
            tmStr = p.Get("trigger_mode").As<Napi::String>().Utf8Value();
            copen.trigger_mode = tmStr.empty() ? nullptr : tmStr.c_str();
        }
        if (p.Has("trigger_source") && p.Get("trigger_source").IsString()) {
            tsStr = p.Get("trigger_source").As<Napi::String>().Utf8Value();
            copen.trigger_source = tsStr.empty() ? nullptr : tsStr.c_str();
        }
        if (p.Has("net_trans_mode") && p.Get("net_trans_mode").IsNumber()) {
            copen.net_trans_mode = p.Get("net_trans_mode").As<Napi::Number>().Int32Value();
        }
    }

    int frameAction = HIK_CV_FRAME_KEEP;
    if (info.Length() >= 3 && info[2].IsNumber()) {
        frameAction = info[2].As<Napi::Number>().Int32Value();
    }
    if (frameAction != HIK_CV_FRAME_KEEP && frameAction != HIK_CV_FRAME_SET && frameAction != HIK_CV_FRAME_CLEAR) {
        throw Napi::TypeError::New(env, "frameAction must be FRAME_KEEP, FRAME_SET, or FRAME_CLEAR");
    }

    HikCvFrameCallback cCb = nullptr;
    if (frameAction == HIK_CV_FRAME_SET) {
        if (info.Length() < 4 || !info[3].IsFunction()) {
            throw Napi::TypeError::New(env, "frameAction=FRAME_SET requires a callback function");
        }
        Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(
            env, info[3].As<Napi::Function>(), Napi::String::New(env, "hik-cv-frame"), 0, 1);
        tsfn.Unref(env);
        std::shared_ptr<FrameEntry> old;
        {
            std::lock_guard<std::mutex> lk(g_framesMutex);
            auto it = g_frames.find(serial);
            if (it != g_frames.end()) {
                old = it->second;  // 覆盖旧条目
                g_frames.erase(it);
            }
            g_frames.emplace(serial, std::make_shared<FrameEntry>(std::move(tsfn)));
        }
        if (old) {
            old->tsfn.Release();
        }
        cCb = frameBridge;
    } else if (frameAction == HIK_CV_FRAME_CLEAR) {
        releaseFrame(serial);
        cCb = nullptr;  // C 侧按 CLEAR 语义清除 C++ 回调表
    }
    // KEEP：cCb 保持 nullptr（C 侧忽略）

    const HikCvResult r = hik_cv_start_device(serial.c_str(), copenPtr, frameAction, cCb, nullptr);
    check(env, r);
    return env.Undefined();
}

Napi::Value StopDevice(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);
    const HikCvResult r = hik_cv_stop_device(serial.c_str());
    check(env, r);
    return env.Undefined();
}

Napi::Value TriggerDevice(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);
    const HikCvResult r = hik_cv_trigger_device(serial.c_str());
    check(env, r);
    return env.Undefined();
}

/** setParam(sn, name, value)：value 支持 number / boolean / string（枚举 symbolic）。 */
Napi::Value SetParam(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);
    if (info.Length() < 2 || !info[1].IsString()) {
        throw Napi::TypeError::New(env, "name must be a string");
    }
    const std::string name = info[1].As<Napi::String>().Utf8Value();
    if (info.Length() < 3) {
        throw Napi::TypeError::New(env, "value required");
    }

    HikCvResult r;
    const Napi::Value v = info[2];
    if (v.IsBoolean()) {
        HikCvParamValue pv{};
        pv.type = HIK_CV_PARAM_BOOL;
        pv.b = v.As<Napi::Boolean>().Value() ? 1 : 0;
        r = hik_cv_set_param(serial.c_str(), name.c_str(), &pv);
    } else if (v.IsString()) {
        const std::string sv = v.As<Napi::String>().Utf8Value();
        r = hik_cv_set_param_string(serial.c_str(), name.c_str(), sv.c_str());
    } else if (v.IsNumber()) {
        const double num = v.As<Napi::Number>().DoubleValue();
        if (num == std::floor(num) && std::abs(num) < 9.2e18) {
            HikCvParamValue pv{};
            pv.type = HIK_CV_PARAM_INT;
            pv.i = static_cast<int64_t>(num);
            r = hik_cv_set_param(serial.c_str(), name.c_str(), &pv);
        } else {
            HikCvParamValue pv{};
            pv.type = HIK_CV_PARAM_FLOAT;
            pv.f = num;
            r = hik_cv_set_param(serial.c_str(), name.c_str(), &pv);
        }
    } else {
        throw Napi::TypeError::New(env, "value must be number, boolean, or string");
    }
    check(env, r);
    return env.Undefined();
}

/** getParam(sn, name) → number | boolean | string。 */
Napi::Value GetParam(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);
    if (info.Length() < 2 || !info[1].IsString()) {
        throw Napi::TypeError::New(env, "name must be a string");
    }
    const std::string name = info[1].As<Napi::String>().Utf8Value();

    HikCvParamValue pv{};
    HikCvResult r = hik_cv_get_param(serial.c_str(), name.c_str(), &pv);
    if (r != HIK_CV_OK) {
        // 字符串节点：转 hik_cv_get_param_string
        char buf[HIK_CV_STRING_MAX];
        const HikCvResult rs = hik_cv_get_param_string(serial.c_str(), name.c_str(), buf, sizeof(buf));
        if (rs == HIK_CV_OK) {
            return Napi::String::New(env, buf);
        }
        check(env, r);
    }
    switch (pv.type) {
        case HIK_CV_PARAM_BOOL:
            return Napi::Boolean::New(env, pv.b != 0);
        case HIK_CV_PARAM_INT:
            return Napi::Number::New(env, static_cast<double>(pv.i));
        case HIK_CV_PARAM_FLOAT:
            return Napi::Number::New(env, pv.f);
        case HIK_CV_PARAM_ENUM:
            return Napi::Number::New(env, static_cast<double>(pv.e));
        default:
            throw Napi::Error::New(env, "get_param: unsupported type");
    }
}

/** forceIp(sn, ip, subnetMask, gateway)：临时强制 GigE 相机 IP（重启恢复）。 */
Napi::Value ForceIp(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);
    if (info.Length() < 4 || !info[1].IsString() || !info[2].IsString() || !info[3].IsString()) {
        throw Napi::TypeError::New(env, "forceIp(sn, ip, subnetMask, gateway)");
    }
    const std::string ip = info[1].As<Napi::String>().Utf8Value();
    const std::string mask = info[2].As<Napi::String>().Utf8Value();
    const std::string gw = info[3].As<Napi::String>().Utf8Value();
    const HikCvResult r = hik_cv_force_ip(serial.c_str(), ip.c_str(), mask.c_str(), gw.c_str());
    check(env, r);
    return env.Undefined();
}

Napi::Value LastError(const Napi::CallbackInfo& info) {
    return Napi::String::New(info.Env(), lastErrorString());
}

// ---------------------------------------------------------------------------
// 模块初始化
// ---------------------------------------------------------------------------

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("enumDevices", Napi::Function::New(env, EnumDevices));
    exports.Set("startDevice", Napi::Function::New(env, StartDevice));
    exports.Set("stopDevice", Napi::Function::New(env, StopDevice));
    exports.Set("triggerDevice", Napi::Function::New(env, TriggerDevice));
    exports.Set("setParam", Napi::Function::New(env, SetParam));
    exports.Set("getParam", Napi::Function::New(env, GetParam));
    exports.Set("forceIp", Napi::Function::New(env, ForceIp));
    exports.Set("lastError", Napi::Function::New(env, LastError));

    exports.Set("HIK_CV_OK", Napi::Number::New(env, HIK_CV_OK));
    exports.Set("HIK_CV_ERR_UNKNOWN", Napi::Number::New(env, HIK_CV_ERR_UNKNOWN));
    exports.Set("HIK_CV_ERR_LOGIC", Napi::Number::New(env, HIK_CV_ERR_LOGIC));
    exports.Set("HIK_CV_ERR_RUNTIME", Napi::Number::New(env, HIK_CV_ERR_RUNTIME));
    exports.Set("HIK_CV_ERR_INVALID_ARG", Napi::Number::New(env, HIK_CV_ERR_INVALID_ARG));
    exports.Set("HIK_CV_ERR_NO_MEMORY", Napi::Number::New(env, HIK_CV_ERR_NO_MEMORY));
    exports.Set("HIK_CV_FRAME_KEEP", Napi::Number::New(env, HIK_CV_FRAME_KEEP));
    exports.Set("HIK_CV_FRAME_SET", Napi::Number::New(env, HIK_CV_FRAME_SET));
    exports.Set("HIK_CV_FRAME_CLEAR", Napi::Number::New(env, HIK_CV_FRAME_CLEAR));

    env.AddCleanupHook([]() { clearAllFrames(); });
    return exports;
}

}  // namespace

NODE_API_MODULE(hik_mvcamera, Init)
