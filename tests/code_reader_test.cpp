/**
 * @file code_reader_test.cpp
 * @brief 读码器联机烟测：枚举设备后对每台 startDevice，持流约 30s 再 stopDevice（需真实设备与驱动）。
 */
#include "code_reader.h"
#include <chrono>
#include <fmt/core.h>
#include <thread>
#include <fmt/ranges.h>
#include <gtest/gtest.h>

TEST(CodeReaderTest, CodeReaderControl) {
    try {
        std::vector<CodeReaderInfo> devs = enumDevice();
        std::vector<std::string> sns;
        sns.reserve(devs.size());
        for (const auto &d : devs) {
            sns.push_back(d.serialNumber);
        }
        fmt::print("{}\n", sns);
        for (auto &sn : sns) {
            startDevice(sn);
        }

        std::this_thread::sleep_for(std::chrono::seconds(30));

        for (auto &sn : sns) {
            stopDevice(sn);
        }
    } catch (const std::exception &e) {
        FAIL() << e.what();
    }
}
