#include "code_reader.h"
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>

TEST(CodeReaderTest, CodeReaderControl) {
    try {
        std::vector<std::string> sns = enumCodeReader();
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