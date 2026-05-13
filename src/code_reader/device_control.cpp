/**
 * @file device_control.cpp
 * @brief 设备打开/关闭/取流状态迁移，以及 startDevice、stopDevice。
 * @note 状态：Connected（仅句柄）→ Open（已 OpenDevice）→ Grabbing（正在取流）。
 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <stdexcept>

/**
 * 从 Connected 执行 OpenDevice → Open。已为 Open 则直接返回。
 * 取流（Grabbing）下禁止调用：须先 stopDevice。
 *
 * @throws std::logic_error 当前为 Grabbing
 * @throws std::runtime_error OpenDevice 失败
 */
void CodeReader::open() {
    if (this->status == CodeReaderStatus::Open) {
        return;
    }
    if (this->status == CodeReaderStatus::Grabbing) {
        throw std::logic_error(
            "CodeReader::open：当前为取流状态，禁止隐式停流；请先 stopDevice 后再打开设备以配置参数");
    }
    if (this->status == CodeReaderStatus::Connected) {
        int sdkOk = MV_CODEREADER_OpenDevice(this->handle);
        if (sdkOk != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_OpenDevice error: " + toHexStr(sdkOk));
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
        // SDK 要求：RegisterImageCallBack 须在 StartGrabbing 之前；此处刷新按序列号绑定的 BCR 回调
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
 * 启动指定序列号设备：确保句柄存在后进入取流状态；若 @p params 含待写项，则在 Open 下写入后再 StartGrabbing。
 *
 * @param sn 设备序列号。
 */
void startDevice(const std::string &sn, const CodeReaderOpenParams &params,
                 const std::optional<CodeReaderBcrCallback> &onBcrCodes) {
    CodeReader *cr = getDevice(sn, true);
    const bool hasOpenPhaseWrites =
        params.triggerMode.has_value() || params.triggerSource.has_value();

    if (cr->status == CodeReaderStatus::Grabbing) {
        if (hasOpenPhaseWrites) {
            throw std::logic_error(
                "startDevice：当前已在取流，无法在携带 CodeReaderOpenParams 时改参；请先 stopDevice");
        }
        if (onBcrCodes.has_value()) {
            registerImageCallbackForSerial(sn, *onBcrCodes);
        }
        return;
    }

    if (onBcrCodes.has_value()) {
        registerImageCallbackForSerial(sn, *onBcrCodes);
    }

    if (hasOpenPhaseWrites) {
        if (cr->status == CodeReaderStatus::Connected) {
            cr->open();
        }
        if (params.triggerMode.has_value()) {
            setEnumValueByString(sn, "TriggerMode", *params.triggerMode);
        }
        if (params.triggerSource.has_value()) {
            setEnumValueByString(sn, "TriggerSource", *params.triggerSource);
        }
    }

    cr->startGrabbing();
}
