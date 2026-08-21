/**
 * @file mvcamera_test.cpp
 * @brief 相机联机烟测：枚举 → startCamera（软触发参数 + 图像回调）→ 触发 → 停流；第二阶段重复（需真实相机）。
 */
#include "camera.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

    constexpr int kTriggersPerPhase = 3;
    constexpr int kBetweenTriggersMs = 300;
    constexpr int kAfterTriggersSec = 2;

    /** 每步完成后打一行，便于对照相机指示灯/网络抓包/上位机表现。 */
    void logTestStep(const char* tag) {
        std::cout << "[test][STEP] " << tag << std::endl;
    }

} // namespace

TEST(MvCameraTest, SmokeEnumStartTriggerStopAndReopen) {
    logTestStep("00_begin | 用例开始");

    std::vector<CameraInfo> devs;
    ASSERT_NO_THROW(devs = enumCamera());
    logTestStep("01_enumCamera_done | 枚举结束");
    if (devs.empty()) {
        GTEST_SKIP() << "未枚举到相机，跳过联机用例（需连接相机与驱动）";
    }

    const std::string sn = devs.front().serialNumber;
    std::cout << "[test] using camera sn=" << sn << " ip=" << devs.front().netExportIp << '\n';
    logTestStep("02_camera_selected | 已选定首台相机");

    CameraOpenParams params;
    params.triggerMode = "On";
    params.triggerSource = "Software";

    std::atomic<int> frameEvents{0};
    ASSERT_NO_THROW(startCamera(
        sn, params,
        [&](const FrameInfo& fi, const unsigned char*, size_t) {
            const int n = ++frameEvents;
            std::cout << "[test] frame #" << n << " " << fi.width << "x" << fi.height
                      << " len=" << fi.frameLen << " frameNum=" << fi.frameNum << '\n';
        }));
    logTestStep("03_phase1_startCamera_done | 阶段1：起流（软触发参数与图像回调）完成");

    for (int i = 0; i < kTriggersPerPhase; ++i) {
        ASSERT_NO_THROW(triggerCamera(sn)) << "trigger " << i;
        std::this_thread::sleep_for(std::chrono::milliseconds(kBetweenTriggersMs));
    }
    std::this_thread::sleep_for(std::chrono::seconds(kAfterTriggersSec));
    logTestStep("06_phase1_after_triggers_sleep_done | 阶段1：末次触发后长等待结束");
    ASSERT_NO_THROW(stopCamera(sn));
    logTestStep("07_phase1_stopCamera_done | 阶段1：停流完成");

    // stop 后为 Connected；再次起流并应用软触发参数（图像回调表仍保留）
    ASSERT_NO_THROW(startCamera(sn, params, std::nullopt));
    logTestStep("08_phase2_startCamera_done | 阶段2：开始取流完成");
    for (int i = 0; i < kTriggersPerPhase; ++i) {
        ASSERT_NO_THROW(triggerCamera(sn)) << "trigger phase2 " << i;
        std::this_thread::sleep_for(std::chrono::milliseconds(kBetweenTriggersMs));
    }
    std::this_thread::sleep_for(std::chrono::seconds(kAfterTriggersSec));
    logTestStep("11_phase2_after_triggers_sleep_done | 阶段2：末次触发后长等待结束");
    ASSERT_NO_THROW(stopCamera(sn));
    logTestStep("12_phase2_stopCamera_done | 阶段2：停流完成");

    ASSERT_NO_THROW(startCamera(sn, params, std::optional<CameraFrameCallback>(CameraFrameCallback{})));
    ASSERT_NO_THROW(stopCamera(sn));
    logTestStep("13_image_callback_cleared | 回调已注销（空回调 + 停流）");
    std::cout << "[test] done, frame callback count=" << frameEvents.load() << '\n';
    logTestStep("14_end | 用例结束");
}
