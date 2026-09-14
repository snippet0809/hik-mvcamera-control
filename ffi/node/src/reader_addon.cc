/**
 * @file reader_addon.cc
 * @brief 统一插件（hik-mvcamera-control）的读码器部分：封装 hik_code_reader C ABI（hik_cr_*）。
 *
 * - 链接 `hik_code_reader.lib`（DLL 导入库），运行时加载同一个 hik_code_reader.dll，
 *   与 Python（ctypes）/ Go（cgo）共享同一份 DLL。
 * - BCR 回调：海康 SDK 在抓图线程调用 C 回调 → `napi_threadsafe_function` 排到 JS 主线程，
 *   避免跨线程调用 JS。per-serial 注册表 + mutex 管理回调生命周期。
 * - 读码成功帧回调：同上桥接，payload 内同步拷出帧数据后再排主线程（SDK 缓冲仅回调期内有效）。
 * - 错误：`HikCrResult != OK` 时 throw `Napi::Error`，消息取 `hik_cr_last_error_copy`。
 *
 * 本文件只含读码器部分（`RegisterReader` 供 addon.cc 的统一 Init 调用）；
 * 相机部分见 camera_addon.cc。
 */

#include <napi.h>

#include "hik_code_reader/c_api.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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
// 读码成功帧回调注册表（JS 侧 tsfn）
// ---------------------------------------------------------------------------

/** 帧载荷：从 SDK 抓图线程同步拷出的帧 + 元数据。 */
struct FramePayload {
    std::string serial;
    HikCrFrameInfo info;
    std::vector<unsigned char> buffer;
};

/** 每个序列号的帧回调条目；tsfn 保活 JS 函数，需显式 Release。 */
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

/** 从注册表移除 serial 并显式 Release 其 tsfn（覆盖注册 / 清除用）。 */
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

/** C 回调（海康抓图线程）；把帧拷入 payload 排到主线程。 */
void frameBridge(const char* serial_utf8, const HikCrFrameInfo* info, const unsigned char* data, size_t len,
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
        entry = it->second;  // 拷贝 shared_ptr：回调期间条目即使被移除仍存活
    }

    auto* payload = new FramePayload();
    payload->serial = sn;
    payload->info = *info;
    if (data && len > 0) {
        payload->buffer.assign(data, data + len);  // SDK 缓冲仅回调期内有效，须同步拷贝
    }

    const napi_status status = entry->tsfn.NonBlockingCall(
        payload, [](Napi::Env cbEnv, Napi::Function jsCallback, FramePayload* p) {
            Napi::Object infoObj = Napi::Object::New(cbEnv);
            infoObj.Set("width", Napi::Number::New(cbEnv, p->info.width));
            infoObj.Set("height", Napi::Number::New(cbEnv, p->info.height));
            infoObj.Set("pixelType", Napi::Number::New(cbEnv, p->info.pixel_type));
            infoObj.Set("frameLen", Napi::Number::New(cbEnv, p->info.frame_len));
            infoObj.Set("frameNum", Napi::Number::New(cbEnv, p->info.frame_num));

            Napi::Buffer<unsigned char> buf = Napi::Buffer<unsigned char>::New(cbEnv, p->buffer.size());
            if (!p->buffer.empty()) {
                std::memcpy(buf.Data(), p->buffer.data(), p->buffer.size());
            }
            jsCallback.Call({Napi::String::New(cbEnv, p->serial), infoObj, buf});
            delete p;
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

/** stopGrabbing(sn)：停流但保留连接（下次 startDevice 省掉重建句柄 + OpenDevice）。 */
Napi::Value StopGrabbing(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);
    const HikCrResult r = hik_cr_stop_grabbing(serial.c_str());
    check(env, r);
    return env.Undefined();
}

/** setParam(sn, name, value)：value 支持 number / boolean / string（枚举 symbolic 或命令节点名）。 */
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

    HikCrResult r;
    const Napi::Value v = info[2];
    if (v.IsBoolean()) {
        HikCrParamValue pv{};
        pv.type = HIK_CR_PARAM_BOOL;
        pv.b = v.As<Napi::Boolean>().Value() ? 1 : 0;
        r = hik_cr_set_param(serial.c_str(), name.c_str(), &pv);
    } else if (v.IsString()) {
        const std::string sv = v.As<Napi::String>().Utf8Value();
        r = hik_cr_set_param_string(serial.c_str(), name.c_str(), sv.c_str());
    } else if (v.IsNumber()) {
        const double num = v.As<Napi::Number>().DoubleValue();
        // 整数值走 Int、非整数走 Float（与 Go 侧显式区分 ParamInt/ParamFloat 的语义一致）
        if (num == std::floor(num) && std::abs(num) < 9.2e18) {
            HikCrParamValue pv{};
            pv.type = HIK_CR_PARAM_INT;
            pv.i = static_cast<int64_t>(num);
            r = hik_cr_set_param(serial.c_str(), name.c_str(), &pv);
        } else {
            HikCrParamValue pv{};
            pv.type = HIK_CR_PARAM_FLOAT;
            pv.f = num;
            r = hik_cr_set_param(serial.c_str(), name.c_str(), &pv);
        }
    } else {
        throw Napi::TypeError::New(env, "value must be number, boolean, or string");
    }
    check(env, r);
    return env.Undefined();
}

/**
 * runCommand(sn, name)：执行 GenICam 命令节点（如 "TriggerSoftware"、"UserSetLoad"）。
 * 命令节点无值，故不走 setParam 的取值分支——与 Go 的 ParamCommand / Python 的 set_command 对齐。
 */
Napi::Value RunCommand(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);
    if (info.Length() < 2 || !info[1].IsString()) {
        throw Napi::TypeError::New(env, "name must be a string");
    }
    const std::string name = info[1].As<Napi::String>().Utf8Value();

    HikCrParamValue pv{};
    pv.type = HIK_CR_PARAM_COMMAND;
    check(env, hik_cr_set_param(serial.c_str(), name.c_str(), &pv));
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

    HikCrParamValue pv{};
    HikCrResult r = hik_cr_get_param(serial.c_str(), name.c_str(), &pv);
    if (r != HIK_CR_OK) {
        // 数值接口失败有两种可能：① 节点本就是字符串类型（这里能救回来）；
        // ② 节点不存在/设备未起流（字符串接口同样会失败，其错误信息更贴近真实原因，故用 rs）。
        std::vector<char> buf(HIK_CR_PARAM_STRING_MAX);
        const HikCrResult rs =
            hik_cr_get_param_string(serial.c_str(), name.c_str(), buf.data(), buf.size());
        if (rs == HIK_CR_OK) {
            return Napi::String::New(env, buf.data());
        }
        check(env, rs);
    }
    switch (pv.type) {
        case HIK_CR_PARAM_BOOL:
            return Napi::Boolean::New(env, pv.b != 0);
        case HIK_CR_PARAM_INT:
            return Napi::Number::New(env, static_cast<double>(pv.i));
        case HIK_CR_PARAM_FLOAT:
            return Napi::Number::New(env, pv.f);
        case HIK_CR_PARAM_ENUM:
            return Napi::Number::New(env, static_cast<double>(pv.e));
        default:
            throw Napi::Error::New(env, "get_param: unsupported type");
    }
}

/**
 * getBcrImage(sn) → { width, height, pixelType, buffer } | null。
 * 取最近一次 BCR 成功帧的拷贝；该序列号尚未读到过条码时返回 null（而非抛错，便于调用方轮询）。
 */
Napi::Value GetBcrImage(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);

    // 两段式：out_data 为 NULL 时仅回填 info（含 frame_len）。
    // 查询路径（out_data=NULL）下 C 侧唯一会抛的业务错误就是「尚未读到条码」，其余
    //（入参非法 / bad_alloc）是真故障，不能一并吞成 null，否则调用方永远查不出原因。
    HikCrBcrImageInfo imgInfo{};
    HikCrResult r = hik_cr_get_bcr_image(serial.c_str(), &imgInfo, nullptr, 0);
    if (r == HIK_CR_ERR_RUNTIME) {
        return env.Null();  // 「尚未读到条码」属正常状态，交给调用方判断而非抛异常
    }
    check(env, r);
    if (imgInfo.frame_len <= 0) {
        return env.Null();
    }

    std::vector<unsigned char> data(static_cast<size_t>(imgInfo.frame_len));
    r = hik_cr_get_bcr_image(serial.c_str(), &imgInfo, data.data(), data.size());
    check(env, r);

    // 第二次调用会把 frame_len 更新为本次实际写入长度。抓图线程可能在两次调用之间解码出新的一帧，
    // 若新帧更短，向量尾部会留下未写入的零字节——按实际长度出 Buffer，避免把零填充当成 JPEG 数据。
    const size_t actual = std::min(static_cast<size_t>(imgInfo.frame_len), data.size());
    Napi::Object out = Napi::Object::New(env);
    out.Set("width", Napi::Number::New(env, imgInfo.width));
    out.Set("height", Napi::Number::New(env, imgInfo.height));
    out.Set("pixelType", Napi::Number::New(env, imgInfo.pixel_type));
    Napi::Buffer<unsigned char> buf = Napi::Buffer<unsigned char>::New(env, actual);
    if (actual > 0) {
        std::memcpy(buf.Data(), data.data(), actual);
    }
    out.Set("buffer", buf);
    return out;
}

/**
 * setFrameCallback(sn, frameAction, callbackOrNull)：登记/清除读码成功帧回调。
 * 独立于 BCR，可在已取流时热替换；未登记时读码成功帧仅走 BCR、图像不转发。
 */
Napi::Value SetFrameCallback(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    const std::string serial = requireSerial(info);

    int frameAction = HIK_CR_FRAME_KEEP;
    if (info.Length() >= 2 && info[1].IsNumber()) {
        frameAction = info[1].As<Napi::Number>().Int32Value();
    }
    if (frameAction != HIK_CR_FRAME_KEEP && frameAction != HIK_CR_FRAME_SET &&
        frameAction != HIK_CR_FRAME_CLEAR) {
        throw Napi::TypeError::New(env, "frameAction must be FRAME_KEEP, FRAME_SET, or FRAME_CLEAR");
    }

    HikCrFrameCallback cCb = nullptr;
    if (frameAction == HIK_CR_FRAME_SET) {
        if (info.Length() < 3 || !info[2].IsFunction()) {
            throw Napi::TypeError::New(env, "frameAction=FRAME_SET requires a callback function");
        }
        Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(
            env, info[2].As<Napi::Function>(), Napi::String::New(env, "hik-cr-frame"), 0, 1);
        tsfn.Unref(env);  // 不保持事件循环存活
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
    } else if (frameAction == HIK_CR_FRAME_CLEAR) {
        releaseFrame(serial);
        cCb = nullptr;  // C 侧按 CLEAR 语义清除 C++ 回调表
    }
    // KEEP：cCb 保持 nullptr，C 侧不动既有登记（用于只想重绑到已停流设备的场景）

    const HikCrResult r = hik_cr_set_frame_callback(serial.c_str(), frameAction, cCb, nullptr);
    check(env, r);
    return env.Undefined();
}

Napi::Value LastError(const Napi::CallbackInfo& info) {
    return Napi::String::New(info.Env(), lastErrorString());
}

}  // namespace

// ---------------------------------------------------------------------------
// 注册读码器部分到 exports（供 addon.cc 的统一 Init 调用）
// ---------------------------------------------------------------------------

Napi::Object RegisterReader(Napi::Env env, Napi::Object exports) {
    exports.Set("enumDevices", Napi::Function::New(env, EnumDevices));
    exports.Set("startDevice", Napi::Function::New(env, StartDevice));
    exports.Set("stopDevice", Napi::Function::New(env, StopDevice));
    exports.Set("stopGrabbing", Napi::Function::New(env, StopGrabbing));
    exports.Set("triggerDevice", Napi::Function::New(env, TriggerDevice));
    exports.Set("setParam", Napi::Function::New(env, SetParam));
    exports.Set("getParam", Napi::Function::New(env, GetParam));
    exports.Set("runCommand", Napi::Function::New(env, RunCommand));
    exports.Set("getBcrImage", Napi::Function::New(env, GetBcrImage));
    exports.Set("setFrameCallback", Napi::Function::New(env, SetFrameCallback));
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
    exports.Set("HIK_CR_FRAME_KEEP", Napi::Number::New(env, HIK_CR_FRAME_KEEP));
    exports.Set("HIK_CR_FRAME_SET", Napi::Number::New(env, HIK_CR_FRAME_SET));
    exports.Set("HIK_CR_FRAME_CLEAR", Napi::Number::New(env, HIK_CR_FRAME_CLEAR));
    exports.Set("HIK_CR_PARAM_STRING_MAX", Napi::Number::New(env, HIK_CR_PARAM_STRING_MAX));

    env.AddCleanupHook([]() {
        clearAllBcr();
        clearAllFrames();
    });
    return exports;
}
