/** 内存内图像编码：原始帧 → JPEG 字节（MV_CC_SaveImageEx3），不落盘 */

#include "MvCameraControl.h"
#include "camera.h"
#include "camera_detail.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

/** 默认 JPEG 质量：与旧落盘方案 MV_CC_SaveImageToFileEx 使用的 80 保持一致。 */
constexpr int kDefaultQuality = 80;

/** 默认 Bayer 插值方法：1=均衡（MV_SAVE_IMAGE_PARAM_EX3::iMethodValue 的默认档）。 */
constexpr unsigned int kDefaultMethod = 1;

/**
 * 输出缓冲的富余量：JPEG 对高噪声图像理论上可能略超原始 RGB 体积，
 * 且 SaveImageEx3 不提供「只查长度」的调用方式，故在 RGB24 最坏体积之上再留 1MB。
 */
constexpr size_t kBufferHeadroom = 1u << 20;

/** 海康 JPEG 编码的宽高上限（见 MvCameraControl.h 中 MV_CC_SaveImageEx3 的说明）。 */
constexpr unsigned int kMaxJpegDim = 65500;

int normalizeQuality(int quality) {
    // 海康约定质量区间为 (50,99]，越界时回落到默认值而非报错——调用方传 0 表示「用默认」。
    if (quality <= 50 || quality > 99) {
        return kDefaultQuality;
    }
    return quality;
}

unsigned int normalizeMethod(int method) {
    if (method < 0 || method > 3) {
        return kDefaultMethod;
    }
    return static_cast<unsigned int>(method);
}

/** 乘法溢出检查：win32 构建下 size_t 为 32 位，直接相乘可能静默回绕。 */
bool mulOverflow(size_t a, size_t b, size_t* out) {
    if (a != 0 && b > std::numeric_limits<size_t>::max() / a) {
        return true;
    }
    *out = a * b;
    return false;
}

/** 校验帧几何并返回像素数；溢出或越界时抛 invalid_argument。 */
size_t validateGeometry(unsigned int width, unsigned int height) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("帧宽高为 0");
    }
    if (width > kMaxJpegDim || height > kMaxJpegDim) {
        throw std::invalid_argument("帧宽高超出 JPEG 编码上限 65500");
    }
    size_t pixels = 0;
    if (mulOverflow(static_cast<size_t>(width), static_cast<size_t>(height), &pixels)) {
        throw std::invalid_argument("帧尺寸溢出");
    }
    return pixels;
}

} // namespace

size_t cameraJpegBufferBound(unsigned int width, unsigned int height) {
    const size_t pixels = validateGeometry(width, height);
    size_t bound = 0;
    if (mulOverflow(pixels, 3u, &bound) ||
        bound > std::numeric_limits<size_t>::max() - kBufferHeadroom) {
        throw std::invalid_argument("cameraJpegBufferBound: 输出缓冲上界溢出");
    }
    bound += kBufferHeadroom;
    if (bound > std::numeric_limits<unsigned int>::max()) {
        throw std::invalid_argument("cameraJpegBufferBound: 输出缓冲上界超出 unsigned int");
    }
    return bound;
}

std::vector<unsigned char> encodeCameraJpeg(const std::string& sn, const FrameInfo& info,
                                            const unsigned char* data, size_t len, int quality, int method) {
    if (!data || len == 0) {
        throw std::invalid_argument("encodeCameraJpeg: 原始帧数据为空");
    }
    const size_t pixels = validateGeometry(info.width, info.height);

    // 输入下界校验：海康全部像素格式均 ≥1 字节/像素，故 width×height 是任何合法帧的字节数下界。
    // 这条挡住「frameInfo 与实际缓冲不匹配」（例如沿用上一分辨率的 info、或调用方手搓的元数据）——
    // 少了它，SDK 会拿着 10 字节的缓冲按 5000×5000 去读 25MB，是越界读。
    if (len < pixels) {
        throw std::invalid_argument(
            "encodeCameraJpeg: 数据长度小于 width*height，frameInfo 与实际缓冲不匹配");
    }
    if (info.frameLen > len) {
        throw std::invalid_argument("encodeCameraJpeg: frameLen 大于实际缓冲长度");
    }

    CameraDevice* dev = findCamera(sn);
    if (!dev) {
        throw std::logic_error("encodeCameraJpeg: 设备未 startCamera: " + sn);
    }
    if (!dev->handle) {
        throw std::logic_error("encodeCameraJpeg: 设备句柄为空（须先 startCamera）: " + sn);
    }

    const size_t want = cameraJpegBufferBound(info.width, info.height);
    // 刻意用 unique_ptr 而非 std::vector<T>(n)：后者会零初始化，对 2448×2048 的帧每次
    // 白写 16MB 内存；这里只需一块「够大」的裸缓冲，实际写入长度由 nImageLen 给出。
    std::unique_ptr<unsigned char[]> scratch(new unsigned char[want]);

    MV_SAVE_IMAGE_PARAM_EX3 param{};
    param.pData = const_cast<unsigned char*>(data);  // SDK 只读输入，但接口签名非 const
    param.nDataLen = static_cast<unsigned int>(len);
    param.enPixelType = static_cast<MvGvspPixelType>(info.pixelType);
    param.nWidth = info.width;
    param.nHeight = info.height;
    param.pImageBuffer = scratch.get();
    param.nBufferSize = static_cast<unsigned int>(want);
    param.nImageLen = 0;
    param.enImageType = MV_Image_Jpeg;
    param.nJpgQuality = static_cast<unsigned int>(normalizeQuality(quality));
    param.iMethodValue = normalizeMethod(method);

    checkSdk<MV_OK>(MV_CC_SaveImageEx3(dev->handle, &param), "MV_CC_SaveImageEx3");

    if (param.nImageLen == 0) {
        throw std::runtime_error("encodeCameraJpeg: 编码结果长度为 0");
    }
    if (param.nImageLen > want) {
        // SDK 越界写的前置信号：不信任其返回长度，宁可报错也不按它去做 memcpy。
        throw std::runtime_error("encodeCameraJpeg: SDK 返回长度超出所提供缓冲");
    }
    return std::vector<unsigned char>(scratch.get(), scratch.get() + param.nImageLen);
}
