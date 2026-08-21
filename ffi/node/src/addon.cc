/**
 * @file addon.cc
 * @brief N-API 原生插件：封装 hik_code_reader C ABI（hik_cr_*）。
 *
 * - 链接 `hik_code_reader.lib`（DLL 导入库），运行时加载同一个 hik_code_reader.dll，
 *   与 Python（ctypes）/ Go（cgo）共享同一份 DLL。
 * - BCR 回调：海康 SDK 在抓图线程调用 C 回调 → `napi_threadsafe_function` 排到 JS 主线程，
 *   避免跨线程调用 JS。per-serial 注册表 + mutex 管理回调生命周期。
 * - 错误：`HikCrResult != OK` 时 throw `Napi::Error`，消息取 `hik_cr_last_error_copy`。
 */

#include <napi.h>

#include "hik_code_reader/c_api.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// BCR 回调注册表（JS 侧 tsfn）
// ---------------------------------------------------------------------------

/** BCR 回调数据：从 C 回调（抓图线程）排到主线程的载荷。 */
struct BcrPayload {
    std::string serial;
    std::vector<std::string> codes;
};

/** 每个序列号的 BCR 回调条目；tsfn 保活 JS 函数，析构时释放。 */
struct BcrEntry {
    explicit BcrEntry(Napi::ThreadSafeFunction fn) : tsfn(std::move(fn)) {}
    Napi::ThreadSafeFunction tsfn;
};

std::unordered_map<std::string, std::shared_ptr<BcrEntry>> g_bcr;
std::mutex g_bcrMutex;

void clearAllBcr() {
    std::unordered_map<std::string, std::shared_ptr<BcrEntry>> taken;
    {
        std::lock_guard<std::mutex> lk(g_bcrMutex);
        taken.swap(g_bcr);
    }
    // ThreadSafeFunction 无析构释放语义，须显式 Release()
    for (auto& kv : taken) {
        kv.second->tsfn.Release();
    }
}

/** 从注册表移除 serial 并显式 Release 其 tsfn（覆盖注册 / 清除用）。 */
void releaseBcr(const std::string& serial) {
    std::shared_ptr<BcrEntry> old;
    {
        std::lock_guard<std::mutex> lk(g_bcrMutex);
        auto it = g_bcr.find(serial);
        if (it != g_bcr.end()) {
            old = it->second;
            g_bcr.erase(it);
        }
    }
    if (old) {
        old->tsfn.Release();
    }
}

/** C 回调（海康抓图线程）；按序列号查注册表并排到主线程。 */
void bcrBridge(const char* serial_utf8, const char* const* codes, int code_count, void* /*user_data*/) {
    if (!serial_utf8) {
        return;
    }
    const std::string sn(serial_utf8);

    std::shared_ptr<BcrEntry> entry;
    {
        std::lock_guard<std::mutex> lk(g_bcrMutex);
        auto it = g_bcr.find(sn);
        if (it == g_bcr.end()) {
            return;
        }
        entry = it->second;  // 拷贝 shared_ptr：回调期间条目即使被移除仍存活
    }

    auto* payload = new BcrPayload();
    payload->serial = sn;
    if (codes && code_count > 0) {
        payload->codes.reserve(static_cast<size_t>(code_count));
        for (int i = 0; i < code_count; ++i) {
            if (codes[i]) {
                payload->codes.emplace_back(codes[i]);
            }
        }
    }
    const napi_status status = entry->tsfn.NonBlockingCall(
        payload, [](Napi::Env cbEnv, Napi::Function jsCallback, BcrPayload* data) {
            Napi::Array arr = Napi::Array::New(cbEnv, data->codes.size());
            for (size_t i = 0; i < data->codes.size(); ++i) {
                arr.Set(i, Napi::String::New(cbEnv, data->codes[i]));
            }
            jsCallback.Call({Napi::String::New(cbEnv, data->serial), arr});
            delete data;
        });
    if (status != napi_ok) {
        delete payload;  // 未入队（如 tsfn 已关闭）时回收，避免泄漏
    }
}

// ---------------------------------------------------------------------------
// 辅助
// ---------------------------------------------------------------------------

std::string lastErrorString() {
    const size_t need = hik_cr_last_error_copy(nullptr, 0);
    if (need <= 1) {
        return "";
    }
    std::vector<char> buf(need);
    hik_cr_last_error_copy(buf.data(), buf.size());
    return std::string(buf.data());
}

void check(Napi::Env env, HikCrResult r) {
    if (r == HIK_CR_OK) {
        return;
    }
    throw Napi::Error::New(env,
                           "hik_cr error " + std::to_string(static_cast<int>(r)) + ": " + lastErrorString());
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

    HikCrDeviceInfo* arr = nullptr;
    int count = 0;
    const HikCrResult r = hik_cr_enum_devices(&arr, &count);
    check(env, r);  // 出错时 c_api 保证 arr == nullptr

    Napi::Array out = Napi::Array::New(env, count > 0 ? static_cast<size_t>(count) : 0);
    if (arr && count > 0) {
        for (int i = 0; i < count; ++i) {
            Napi::Object obj = Napi::Object::New(env);
            obj.Set("serialNumber", Napi::String::New(env, arr[i].serial_number));
            obj.Set("netExportIp", Napi::String::New(env, arr[i].net_export_ip));
            out.Set(i, obj);
        }
    }
    hik_cr_free_device_list(arr);
    return out;
}

/** startDevice(serial, paramsOrNull, bcrAction, callbackOrNull) */
Napi::Value StartDevice(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);

    // 起流参数：未填字段走 C++ 默认（-1 / NULL）
    HikCrOpenParams copen{};
    copen.code128 = -1;
    copen.qrcode = -1;
    HikCrOpenParams* copenPtr = nullptr;

    std::string tmStr;
    std::string tsStr;
    bool hasTm = false;
    bool hasTs = false;

    if (info.Length() >= 2 && info[1].IsObject()) {
        Napi::Object p = info[1].As<Napi::Object>();
        copenPtr = &copen;

        if (p.Has("trigger_mode") && p.Get("trigger_mode").IsString()) {
            tmStr = p.Get("trigger_mode").As<Napi::String>().Utf8Value();
            hasTm = !tmStr.empty();
        }
        if (p.Has("trigger_source") && p.Get("trigger_source").IsString()) {
            tsStr = p.Get("trigger_source").As<Napi::String>().Utf8Value();
            hasTs = !tsStr.empty();
        }
        const auto readTri = [&](const char* key, int& dst) {
            if (!p.Has(key)) {
                return;
            }
            Napi::Value v = p.Get(key);
            if (v.IsBoolean()) {
                dst = v.As<Napi::Boolean>().Value() ? 1 : 0;
            } else if (v.IsNumber()) {
                dst = v.As<Napi::Number>().Int32Value();
            }
            // 其它类型：保持默认
        };
        readTri("code128", copen.code128);
        readTri("qrcode", copen.qrcode);
    }
    // std::string（UTF-8）活到 hik_cr_start_device 返回之后（C 调用期内同步拷贝）
    if (hasTm) {
        copen.trigger_mode = tmStr.c_str();
    }
    if (hasTs) {
        copen.trigger_source = tsStr.c_str();
    }

    // BCR 三态
    int bcrAction = HIK_CR_BCR_KEEP;
    if (info.Length() >= 3 && info[2].IsNumber()) {
        bcrAction = info[2].As<Napi::Number>().Int32Value();
    }
    if (bcrAction != HIK_CR_BCR_KEEP && bcrAction != HIK_CR_BCR_SET && bcrAction != HIK_CR_BCR_CLEAR) {
        throw Napi::TypeError::New(env, "bcrAction must be BCR_KEEP, BCR_SET, or BCR_CLEAR");
    }

    HikCrBcrCallback cCb = nullptr;
    if (bcrAction == HIK_CR_BCR_SET) {
        if (info.Length() < 4 || !info[3].IsFunction()) {
            throw Napi::TypeError::New(env, "bcrAction=BCR_SET requires a callback function");
        }
        Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(
            env, info[3].As<Napi::Function>(), Napi::String::New(env, "hik-bcr"), 0, 1);
        tsfn.Unref(env);  // 不保持事件循环存活（镜像 Python：回调不阻止进程退出）
        std::shared_ptr<BcrEntry> old;
        {
            std::lock_guard<std::mutex> lk(g_bcrMutex);
            auto it = g_bcr.find(serial);
            if (it != g_bcr.end()) {
                old = it->second;  // 覆盖旧条目
                g_bcr.erase(it);
            }
            g_bcr.emplace(serial, std::make_shared<BcrEntry>(std::move(tsfn)));
        }
        if (old) {
            old->tsfn.Release();
        }
        cCb = bcrBridge;
    } else if (bcrAction == HIK_CR_BCR_CLEAR) {
        releaseBcr(serial);
        cCb = nullptr;  // C 侧按 CLEAR 语义清除 C++ 回调表
    }
    // KEEP：cCb 保持 nullptr（C 侧忽略）

    const HikCrResult r =
        hik_cr_start_device(serial.c_str(), copenPtr, bcrAction, cCb, nullptr);
    check(env, r);
    return env.Undefined();
}

Napi::Value StopDevice(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);
    const HikCrResult r = hik_cr_stop_device(serial.c_str());
    check(env, r);
    return env.Undefined();
}

Napi::Value TriggerDevice(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);
    const HikCrResult r = hik_cr_trigger_device(serial.c_str());
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
    exports.Set("lastError", Napi::Function::New(env, LastError));

    exports.Set("HIK_CR_OK", Napi::Number::New(env, HIK_CR_OK));
    exports.Set("HIK_CR_ERR_UNKNOWN", Napi::Number::New(env, HIK_CR_ERR_UNKNOWN));
    exports.Set("HIK_CR_ERR_LOGIC", Napi::Number::New(env, HIK_CR_ERR_LOGIC));
    exports.Set("HIK_CR_ERR_RUNTIME", Napi::Number::New(env, HIK_CR_ERR_RUNTIME));
    exports.Set("HIK_CR_ERR_INVALID_ARG", Napi::Number::New(env, HIK_CR_ERR_INVALID_ARG));
    exports.Set("HIK_CR_ERR_NO_MEMORY", Napi::Number::New(env, HIK_CR_ERR_NO_MEMORY));
    exports.Set("HIK_CR_BCR_KEEP", Napi::Number::New(env, HIK_CR_BCR_KEEP));
    exports.Set("HIK_CR_BCR_SET", Napi::Number::New(env, HIK_CR_BCR_SET));
    exports.Set("HIK_CR_BCR_CLEAR", Napi::Number::New(env, HIK_CR_BCR_CLEAR));

    env.AddCleanupHook([]() { clearAllBcr(); });
    return exports;
}

}  // namespace

NODE_API_MODULE(hik_code_reader, Init)
