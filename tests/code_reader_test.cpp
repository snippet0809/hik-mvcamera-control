#include "code_reader.h"
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>

TEST(CodeReaderTest, EnumCodeReader) {
    try {
        std::vector<std::string> sns = enumCodeReader();
        fmt::print("{}\n", sns);
    } catch (const std::exception &e) {
        FAIL() << e.what();
    }
}