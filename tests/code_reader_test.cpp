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
    constexpr int kBetweenTriggersMs = 300;
    constexpr int kAfterTriggersSec = 2;

    /** 每步完成后打一行，便于对照读码器指示灯/网络抓包/上位机表现。 */
    void logTestStep(const char *tag) {
        std::cout << "[test][STEP] " << tag << std::endl;
    }

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
    logTestStep("00_begin | 用例开始");

    std::vector<CodeReaderInfo> devs;
    ASSERT_NO_THROW(devs = enumDevice());
    logTestStep("01_enumDevice_done | 枚举结束");
    if (devs.empty()) {
        GTEST_SKIP() << "未枚举到读码器，跳过联机用例（需连接设备与驱动）";
    }

    const std::string sn = devs.front().serialNumber;
    std::cout << "[test] using device sn=" << sn << " ip=" << devs.front().netExportIp << '\n';
    logTestStep("02_device_selected | 已选定首台设备");

    std::atomic<int> bcrEvents{0};
    registerImageCallbackForSerial(sn, [&](std::vector<std::string> codeArr) {
        const int n = ++bcrEvents;
        std::cout << "[test] BCR #" << n << " codes=";
        printStringsJoined(std::cout, codeArr, ", ");
        std::cout << '\n';
    });
    logTestStep("03_image_callback_registered | 图像/读码回调已注册");

    // 取流前先 Open 并配置软触发，再 StartGrabbing
    ASSERT_NO_THROW(openDeviceForParameters(sn));
    logTestStep("04_phase1_openDeviceForParameters_done | 阶段1：Open（参数）完成");
    ASSERT_NO_THROW(configureSoftwareTriggerForTest(sn));
    logTestStep("05_phase1_software_trigger_params_done | 阶段1：TriggerMode/TriggerSource 配置完成");
    ASSERT_NO_THROW(startDevice(sn));
    logTestStep("06_phase1_startDevice_done | 阶段1：开始取流完成");

    // 取流 → 软触发 → 等待 → 停流
    for (int i = 0; i < kTriggersPerPhase; ++i) {
        ASSERT_NO_THROW(triggerDevice(sn)) << "trigger " << i;
        logTestStep(("07_phase1_triggerDevice_done_" + std::to_string(i)).c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(kBetweenTriggersMs));
        logTestStep(("08_phase1_between_trigger_sleep_done_" + std::to_string(i)).c_str());
    }
    std::this_thread::sleep_for(std::chrono::seconds(kAfterTriggersSec));
    logTestStep("09_phase1_after_triggers_sleep_done | 阶段1：末次触发后长等待结束");
    ASSERT_NO_THROW(stopDevice(sn));
    logTestStep("10_phase1_stopDevice_done | 阶段1：停流完成");

    // stop 后为 Connected；再次 Open → 软触发参数 → 取流
    ASSERT_NO_THROW(openDeviceForParameters(sn));
    logTestStep("11_phase2_openDeviceForParameters_done | 阶段2：Open（参数）完成");
    ASSERT_NO_THROW(configureSoftwareTriggerForTest(sn));
    logTestStep("12_phase2_software_trigger_params_done | 阶段2：软触发参数完成");
    ASSERT_NO_THROW(startDevice(sn));
    logTestStep("13_phase2_startDevice_done | 阶段2：开始取流完成");
    for (int i = 0; i < kTriggersPerPhase; ++i) {
        ASSERT_NO_THROW(triggerDevice(sn)) << "trigger phase2 " << i;
        logTestStep(("14_phase2_triggerDevice_done_" + std::to_string(i)).c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(kBetweenTriggersMs));
        logTestStep(("15_phase2_between_trigger_sleep_done_" + std::to_string(i)).c_str());
    }
    std::this_thread::sleep_for(std::chrono::seconds(kAfterTriggersSec));
    logTestStep("16_phase2_after_triggers_sleep_done | 阶段2：末次触发后长等待结束");
    ASSERT_NO_THROW(stopDevice(sn));
    logTestStep("17_phase2_stopDevice_done | 阶段2：停流完成");

    registerImageCallbackForSerial(sn, {});
    logTestStep("18_image_callback_cleared | 回调已注销");
    std::cout << "[test] done, BCR callback count=" << bcrEvents.load() << '\n';
    logTestStep("19_end | 用例结束");
}
