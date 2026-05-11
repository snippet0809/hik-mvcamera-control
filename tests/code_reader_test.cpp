/**
 * @file code_reader_test.cpp
 * @brief 读码器联机烟测：枚举 → Open → 配置软触发 → 取流 → 软触发 → 停流；第二阶段重复（需真实设备）。
 */
#include "code_reader.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

    constexpr int kTriggersPerPhase = 3;
    constexpr auto kBetweenTriggers = std::chrono::milliseconds(300);
    constexpr auto kAfterTriggers = std::chrono::seconds(2);

    void printStringsJoined(std::ostream &os, const std::vector<std::string> &v, const char *sep) {
        os << '[';
        for (size_t i = 0; i < v.size(); ++i) {
            if (i != 0) {
                os << sep;
            }
            os << v[i];
        }
        os << ']';
    }

    /**
     * 在 Open（未取流）下配置软触发，便于后续 TriggerSoftware 命令成功（否则易出现 MV_CODEREADER_E_GC_ACCESS）。
     */
    void configureSoftwareTriggerForTest(const std::string &sn) {
        setEnumValueByString(sn, "TriggerMode", "On");
        setEnumValueByString(sn, "TriggerSource", "Software");
    }

} // namespace

TEST(CodeReaderTest, SmokeEnumGrabTriggerStopAndReopen) {
    std::vector<CodeReaderInfo> devs;
    ASSERT_NO_THROW(devs = enumDevice());
    if (devs.empty()) {
        GTEST_SKIP() << "未枚举到读码器，跳过联机用例（需连接设备与驱动）";
    }

    const std::string sn = devs.front().serialNumber;
    std::cout << "[test] using device sn=" << sn << " ip=" << devs.front().netExportIp << '\n';

    std::atomic<int> bcrEvents{0};
    registerImageCallback([&](std::vector<std::string> codeArr) {
        const int n = ++bcrEvents;
        std::cout << "[test] BCR #" << n << " codes=";
        printStringsJoined(std::cout, codeArr, ", ");
        std::cout << '\n';
    });

    // 取流前先 Open 并配置软触发，再 StartGrabbing
    ASSERT_NO_THROW(openDeviceForParameters(sn));
    ASSERT_NO_THROW(configureSoftwareTriggerForTest(sn));
    ASSERT_NO_THROW(startDevice(sn));

    // 取流 → 软触发 → 等待 → 停流
    for (int i = 0; i < kTriggersPerPhase; ++i) {
        ASSERT_NO_THROW(triggerDevice(sn)) << "trigger " << i;
        std::this_thread::sleep_for(kBetweenTriggers);
    }
    std::this_thread::sleep_for(kAfterTriggers);
    ASSERT_NO_THROW(stopDevice(sn));

    // stop 后为 Connected；再次 Open → 软触发参数 → 取流
    ASSERT_NO_THROW(openDeviceForParameters(sn));
    ASSERT_NO_THROW(configureSoftwareTriggerForTest(sn));
    ASSERT_NO_THROW(startDevice(sn));
    for (int i = 0; i < kTriggersPerPhase; ++i) {
        ASSERT_NO_THROW(triggerDevice(sn)) << "trigger phase2 " << i;
        std::this_thread::sleep_for(kBetweenTriggers);
    }
    std::this_thread::sleep_for(kAfterTriggers);
    ASSERT_NO_THROW(stopDevice(sn));

    registerImageCallback({});
    std::cout << "[test] done, BCR callback count=" << bcrEvents.load() << '\n';
}
