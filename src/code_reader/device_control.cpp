/**
 * @file device_control.cpp
 * @brief 设备打开/关闭/取流状态迁移，以及 startDevice / stopDevice。
 * @note 状态：Connected（仅句柄）→ Open（已 OpenDevice）→ Grabbing（正在取流）。
 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include <stdexcept>
#include <string>

/**
 * 打开设备（OpenDevice），使状态进入「已打开、未取流」（Open）。
 *
 * - 已为 Open：直接返回。
 * - Connected：调用 SDK 打开设备。
 * - Grabbing：先 StopGrabbing，再保持为 Open，便于后续设置参数等流程。
 *
 * @throws std::runtime_error SDK 返回非 MV_CODEREADER_OK 时抛出，错误信息含十六进制错误码。
 */
void CodeReader::open() {
    if (this->status == CodeReaderStatus::Open) {
        return;
    }
    if (this->status == CodeReaderStatus::Connected) {
        int sdkOk = MV_CODEREADER_OpenDevice(this->handle);
        if (sdkOk != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_OpenDevice error: " + toHexStr(sdkOk));
        }
        this->status = CodeReaderStatus::Open;
        return;
    }
    if (this->status == CodeReaderStatus::Grabbing) {
        int sdkOk = MV_CODEREADER_StopGrabbing(this->handle);
        if (sdkOk != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_StopGrabbing error: " + toHexStr(sdkOk));
        }
        this->status = CodeReaderStatus::Open;
    }
}

/**
 * 关闭设备：若正在取流则先停流，再 CloseDevice，最终回到 Connected（仅句柄有效）。
 *
 * - 已为 Connected：无需操作。
 *
 * @throws std::runtime_error StopGrabbing 或 CloseDevice 失败时抛出。
 */
void CodeReader::close() {
    if (this->status == CodeReaderStatus::Connected) {
        return;
    }
    if (this->status == CodeReaderStatus::Grabbing) {
        int sdkOk = MV_CODEREADER_StopGrabbing(this->handle);
        if (sdkOk != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_StopGrabbing error: " + toHexStr(sdkOk));
        }
        this->status = CodeReaderStatus::Open;
    }
    if (this->status == CodeReaderStatus::Open) {
        int sdkOk = MV_CODEREADER_CloseDevice(this->handle);
        if (sdkOk != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_CloseDevice error: " + toHexStr(sdkOk));
        }
        this->status = CodeReaderStatus::Connected;
    }
}

/**
 * 开始取流：必要时先 OpenDevice，再 StartGrabbing，状态变为 Grabbing。
 *
 * - 已为 Grabbing：直接返回。
 *
 * @throws std::runtime_error OpenDevice 或 StartGrabbing 失败时抛出。
 */
void CodeReader::startGrabbing() {
    if (this->status == CodeReaderStatus::Grabbing) {
        return;
    }
    if (this->status == CodeReaderStatus::Connected) {
        int sdkOk = MV_CODEREADER_OpenDevice(this->handle);
        if (sdkOk != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_OpenDevice error: " + toHexStr(sdkOk));
        }
        this->status = CodeReaderStatus::Open;
    }
    if (this->status == CodeReaderStatus::Open) {
        // SDK 要求：RegisterImageCallBack 须在 StartGrabbing 之前；此处绑定集成方 registerImageCallback 中的逻辑
        codeReaderInternalBindImageCallbackBeforeGrabbing(this);
        int sdkOk = MV_CODEREADER_StartGrabbing(this->handle);
        if (sdkOk != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_StartGrabbing error: " + toHexStr(sdkOk));
        }
        this->status = CodeReaderStatus::Grabbing;
    }
}

/**
 * 停止指定序列号设备：对缓存中的实例执行 close()（停流并关闭设备）。
 *
 * @param sn 设备序列号。
 * @note 不会创建新实例；若尚未缓存则本函数无操作。
 */
void stopDevice(const std::string &sn) {
    CodeReader *cr = getDevice(sn, false);
    if (cr != nullptr) {
        cr->close();
    }
}

/**
 * 启动指定序列号设备：确保句柄存在后进入取流状态。
 *
 * @param sn 设备序列号。
 */
void startDevice(const std::string &sn) {
    CodeReader *cr = getDevice(sn, true);
    cr->startGrabbing();
}
