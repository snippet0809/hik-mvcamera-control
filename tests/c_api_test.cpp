/**
 * @file c_api_test.cpp
 * @brief C ABI 离线回归：hik_cr_get_bcr_image 取图契约 + hik_cr_stop_grabbing 空操作语义。
 *
 * 不经硬件：直接用内部 setLastBcrImage 造一份“最近读码帧图”，再按头文件契约验四条路径
 * （NULL 出参只回填长度 / out_cap 不足报错 / 精确缓冲拷贝 / 未读到条码报错）。
 */
#include "code_reader_detail.h"
#include "hik_code_reader/c_api.h"

#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

constexpr const char *kFakeSn = "TEST-SN-BCR-IMAGE";

/** 造一份确定性的假帧图并登记为该序列号的“最近一次 BCR 图像”。 */
void seedBcrImage(int width, int height, int pixelType, const std::vector<unsigned char> &bytes) {
    setLastBcrImage(kFakeSn, bytes.data(), bytes.size(), width, height, pixelType);
}

} // namespace

TEST(CodeReaderCApiTest, GetBcrImageRejectsNullArgs) {
    HikCrBcrImageInfo info{};
    EXPECT_EQ(hik_cr_get_bcr_image(nullptr, &info, nullptr, 0), HIK_CR_ERR_INVALID_ARG);
    EXPECT_EQ(hik_cr_get_bcr_image(kFakeSn, nullptr, nullptr, 0), HIK_CR_ERR_INVALID_ARG);
}

TEST(CodeReaderCApiTest, GetBcrImageFailsBeforeAnyBarcode) {
    HikCrBcrImageInfo info{};
    std::vector<unsigned char> buf(16);
    // 未读到过条码的序列号：头文件契约要求报错，且不得回填。
    EXPECT_EQ(hik_cr_get_bcr_image("TEST-SN-NEVER-READ", &info, buf.data(), buf.size()),
              HIK_CR_ERR_RUNTIME);
    // 空图（data 为空）同样视为“无 BCR 图像”。
    setLastBcrImage("TEST-SN-EMPTY-IMG", nullptr, 0, 640, 480, 0);
    EXPECT_EQ(hik_cr_get_bcr_image("TEST-SN-EMPTY-IMG", &info, buf.data(), buf.size()),
              HIK_CR_ERR_RUNTIME);
}

TEST(CodeReaderCApiTest, GetBcrImageNullOutDataOnlyFillsLength) {
    const std::vector<unsigned char> bytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    seedBcrImage(1280, 1024, 0x01080001, bytes);

    HikCrBcrImageInfo info{};
    ASSERT_EQ(hik_cr_get_bcr_image(kFakeSn, &info, nullptr, 0), HIK_CR_OK);
    EXPECT_EQ(info.width, 1280);
    EXPECT_EQ(info.height, 1024);
    EXPECT_EQ(info.pixel_type, 0x01080001);
    EXPECT_EQ(info.frame_len, static_cast<int>(bytes.size()));
}

TEST(CodeReaderCApiTest, GetBcrImageRejectsShortBufferAndCopiesOnExactFit) {
    const std::vector<unsigned char> bytes{0xAA, 0xBB, 0xCC, 0xDD};
    seedBcrImage(2, 2, 0x01080001, bytes);

    HikCrBcrImageInfo info{};
    std::vector<unsigned char> tooSmall(bytes.size() - 1, 0);
    EXPECT_EQ(hik_cr_get_bcr_image(kFakeSn, &info, tooSmall.data(), tooSmall.size()),
              HIK_CR_ERR_RUNTIME);

    // 精确大小：拷贝内容须逐字节一致；out_info 在拷贝路径上也要回填。
    std::vector<unsigned char> exact(bytes.size(), 0);
    ASSERT_EQ(hik_cr_get_bcr_image(kFakeSn, &info, exact.data(), exact.size()), HIK_CR_OK);
    EXPECT_EQ(exact, bytes);
    EXPECT_EQ(info.frame_len, static_cast<int>(bytes.size()));

    // 超额缓冲：只写前 frame_len 字节，其余不动。
    std::vector<unsigned char> roomy(bytes.size() + 8, 0x7F);
    ASSERT_EQ(hik_cr_get_bcr_image(kFakeSn, &info, roomy.data(), roomy.size()), HIK_CR_OK);
    EXPECT_EQ(std::memcmp(roomy.data(), bytes.data(), bytes.size()), 0);
    EXPECT_EQ(roomy[bytes.size()], 0x7F);
}

TEST(CodeReaderCApiTest, StopGrabbingIsNoOpOnUnknownDevice) {
    // 头文件契约：未开流时为空操作 —— 未知序列号须返回成功而非报错。
    EXPECT_EQ(hik_cr_stop_grabbing(kFakeSn), HIK_CR_OK);
    EXPECT_EQ(hik_cr_stop_grabbing(nullptr), HIK_CR_ERR_INVALID_ARG);
}
