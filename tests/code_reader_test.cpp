/**
 * @file code_reader_test.cpp
 * @brief 读码器联机烟测：枚举 → 取流 → 软触发 → 停流；再测 openDeviceForParameters → 再次取流 → 停流（需真实设备）。
 */
#include "code_reader.h"

#include <atomic>
#include <chrono>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>
#include <thread>

namespace {

constexpr int kTriggersPerPhase = 3;
constexpr auto kBetweenTriggers = std::chrono::milliseconds(300);
constexpr auto kAfterTriggers = std::chrono::seconds(2);

} // namespace

TEST(CodeReaderTest, SmokeEnumGrabTriggerStopAndReopen) {
    std::vector<CodeReaderInfo> devs;
    ASSERT_NO_THROW(devs = enumDevice());
    if (devs.empty()) {
        GTEST_SKIP() << "未枚举到读码器，跳过联机用例（需连接设备与驱动）";
    }

    const std::string sn = devs.front().serialNumber;
    fmt::print("[test] using device sn={} ip={}\n", sn, devs.front().netExportIp);

    std::atomic<int> bcrEvents{0};
    registerImageCallback([&](std::vector<std::string> codeArr) {
        const int n = ++bcrEvents;
        fmt::print("[test] BCR #{} codes=[{}]\n", n, fmt::join(codeArr, ", "));
    });

    // 取流 → 软触发 → 等待 → 停流
    ASSERT_NO_THROW(startDevice(sn));
    for (int i = 0; i < kTriggersPerPhase; ++i) {
        ASSERT_NO_THROW(triggerDevice(sn)) << "trigger " << i;
        std::this_thread::sleep_for(kBetweenTriggers);
    }
    std::this_thread::sleep_for(kAfterTriggers);
    ASSERT_NO_THROW(stopDevice(sn));

    // stop 后为 Connected；openDeviceForParameters → Open，再 startDevice 应能重新 Grabbing
    ASSERT_NO_THROW(openDeviceForParameters(sn));
    ASSERT_NO_THROW(startDevice(sn));
    for (int i = 0; i < kTriggersPerPhase; ++i) {
        ASSERT_NO_THROW(triggerDevice(sn)) << "trigger phase2 " << i;
        std::this_thread::sleep_for(kBetweenTriggers);
    }
    std::this_thread::sleep_for(kAfterTriggers);
    ASSERT_NO_THROW(stopDevice(sn));

    registerImageCallback({});
    fmt::print("[test] done, BCR callback count={}\n", bcrEvents.load());
}
