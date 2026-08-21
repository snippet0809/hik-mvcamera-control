/**
 * @file addon.cc
 * @brief 统一 N-API 插件入口（hik-mvcamera-control）。
 *
 * 同时导出读码器（hik_cr_*，HikCodeReader）与相机（hik_cv_*，HikCamera）：
 *   - `native.reader`  子对象 ← reader_addon.cc 的 `RegisterReader`（BCR 回调桥 + HIK_CR_* 常量）；
 *   - `native.camera`  子对象 ← camera_addon.cc 的 `RegisterCamera`（图像回调桥 + HIK_CV_* 常量）；
 *   - 顶层 `HIK_CR_*` / `HIK_CV_*` 常量（命名唯一，不冲突）。
 *
 * 两个子对象内的函数名（enumDevices/startDevice/...）相同但分属不同命名空间，
 * 避免在同一个 exports 对象上相互覆盖。
 *
 * 本文件是唯一含 `NODE_API_MODULE` 的编译单元。
 */

#include <napi.h>

Napi::Object RegisterReader(Napi::Env env, Napi::Object exports);
Napi::Object RegisterCamera(Napi::Env env, Napi::Object exports);

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    Napi::Object reader = Napi::Object::New(env);
    RegisterReader(env, reader);
    exports.Set("reader", reader);

    Napi::Object camera = Napi::Object::New(env);
    RegisterCamera(env, camera);
    exports.Set("camera", camera);

    return exports;
}

NODE_API_MODULE(hik_mvcamera_control, Init)
