#pragma once

/** 实现侧类型；不随 code_reader.h 对外暴露。 */

#include "code_reader.h"

#include "../common/sdk_util.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

enum class CodeReaderStatus { Connected, Open, Grabbing };

/**
 * 全局互斥锁：保护 deviceMap / g_bcr / g_frames 及句柄状态迁移。
 * startDevice / stopDevice / triggerDevice / imageBridge 在各自入口加锁；
 * findDevice / getOrCreateDevice / CodeReader::open/grabbing/close / recreateHandle /
 * registerImageCallbackForSerial / registerFrameCallbackForSerial 等内部函数约定「调用方已持有本锁」。
 */
extern std::mutex g_device_mutex;

/** 最近一次 BCR 成功的读码帧图（按序列号常驻，供 hik_cr_get_bcr_image 拉取）。 */
struct KeptBcrImage {
    std::vector<unsigned char> data;
    int width = 0;
    int height = 0;
    int pixelType = 0;  // MvCodeReaderGvspPixelType
};

std::shared_ptr<KeptBcrImage> getLastBcrImage(const std::string &sn);
void setLastBcrImage(const std::string &sn, const unsigned char *data, size_t len,
                     int width, int height, int pixelType);

class CodeReader {
public:
    std::string serialNumber;
    void *handle;
    CodeReaderStatus status;
    /** true 表示句柄已被 CloseDevice，SDK 不允许拿它再次 OpenDevice，须先重建。 */
    bool handleStale;

    explicit CodeReader(const std::string &serialNumber);
    ~CodeReader();
    void open();
    void grabbing();
    void close();
    /** 销毁旧句柄并重新 CreateHandleBySerialNumber，得到可再次 Open 的新句柄。 */
    void recreateHandle();
};

CodeReader *findDevice(const std::string &sn);
CodeReader *getOrCreateDevice(const std::string &sn);

void codeReaderInternalBindImageCallbackBeforeGrabbing(CodeReader *device);
void registerImageCallbackForSerial(const std::string &sn, const CodeReaderBcrCallback &callback);
void registerFrameCallbackForSerial(const std::string &sn, const CodeReaderFrameCallback &callback);
