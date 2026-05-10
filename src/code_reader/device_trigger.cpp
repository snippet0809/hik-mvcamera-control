/**
 * @file device_trigger.cpp
 * @brief 图像回调注册与触发等扩展接口（占位实现，待对接 SDK 取流/事件 API）。
 */

#include "code_reader.h"

/** 占位：后续对接取流/解码回调。 */
void registerImageCallback(const std::function<void(std::vector<std::string> codeArr)> &callback) {
    (void)callback;
}

/** 占位：后续对接软触发等 SDK 接口。 */
void triggerDevice(const std::string &sn) {
    (void)sn;
}
