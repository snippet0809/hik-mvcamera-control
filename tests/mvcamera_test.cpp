/**
 * @file mvcamera_test.cpp
 * @brief 相机联机烟测：枚举 → startCamera（软触发参数 + 图像回调）→ 触发 → 停流；第二阶段重复（需真实相机）。
 */
#include "camera.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <gtest/gtest.h>
#include <iostream>
#include <mutex>
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

    // 回调里只累积、不断言（gtest 断言不适合放在 SDK 抓图线程里），事后统一校验。
    struct SeenFrame {
        unsigned int width, height, pixelType, frameLen, frameNum;
        size_t argLen;
    };
    std::atomic<int> frameEvents{0};
    std::mutex seenMu;
    std::vector<SeenFrame> seen;

    ASSERT_NO_THROW(startCamera(
        sn, params,
        [&](const FrameInfo& fi, const unsigned char*, size_t len) {
            const int n = ++frameEvents;
            std::cout << "[test] frame #" << n << " " << fi.width << "x" << fi.height
                      << " pixelType=0x" << std::hex << fi.pixelType << std::dec
                      << " len=" << fi.frameLen << " frameNum=" << fi.frameNum << '\n';
            std::lock_guard<std::mutex> lk(seenMu);
            seen.push_back({fi.width, fi.height, fi.pixelType, fi.frameLen, fi.frameNum, len});
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

    // 逐帧校验元数据。这是整套测试里**唯一**能抓住「编译器的结构体布局与厂商二进制
    // 不一致」的地方，别删。
    //
    // 踩过：MinGW 默认不定义 WIN32，海康 PixelType.h 走了 #else 分支，
    // MvGvspPixelType 从 4 字节变 8 字节，MV_FRAME_OUT_INFO_EX 之后的字段整体错位。
    // width/height 恰好还对（在结构体开头，偏移未受影响），但 frameLen 读成 0、
    // frameNum 是垃圾值——而当时用例只打印不断言，照样 [ OK ]。
    // MSVC 构建不会触发，但海康 SDK 更新同样可能改变布局，所以两条路径都得防。
    {
        std::lock_guard<std::mutex> lk(seenMu);
        ASSERT_FALSE(seen.empty()) << "一帧都没收到，无法校验元数据";
        for (std::size_t i = 0; i < seen.size(); ++i) {
            const SeenFrame &f = seen[i];
            EXPECT_GT(f.width, 0u) << "frame " << i;
            EXPECT_GT(f.height, 0u) << "frame " << i;
            EXPECT_GT(f.frameLen, 0u)
                << "frame " << i << "：frameLen 为 0，疑似结构体布局错位（编译器与厂商二进制不一致）";
            // 回调第三参与 fi.frameLen 同源，不一致说明传递环节出了问题。
            EXPECT_EQ(f.argLen, static_cast<std::size_t>(f.frameLen)) << "frame " << i;
            // 8 位像素（Mono8 / BayerRG8 等）时每帧字节数应恰为 宽x高。
            // 位深取自像素值本身，沿用厂商的 MV_GVSP_PIX_EFFECTIVE_PIXEL_SIZE_MASK/SHIFT
            // 约定：(pixelType & 0x00FF0000) >> 16。这样不绑定具体相机型号。
            const unsigned bitsPerPixel = (f.pixelType & 0x00FF0000u) >> 16;
            if (bitsPerPixel == 8) {
                EXPECT_EQ(f.frameLen, f.width * f.height)
                    << "frame " << i << "：字节数与 宽x高 不符，疑似结构体布局错位";
            }
        }
    }

    std::cout << "[test] done, frame callback count=" << frameEvents.load() << '\n';
    logTestStep("14_end | 用例结束");
}
