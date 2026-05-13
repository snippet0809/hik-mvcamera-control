/**
 * @file device_control.cpp
 * @brief 设备状态迁移（Connected → Open → Grabbing）与 `startDevice` / `stopDevice`。
 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <stdexcept>

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
        codeReaderInternalBindImageCallbackBeforeGrabbing(this);
        int sdkOk = MV_CODEREADER_StartGrabbing(this->handle);
        if (sdkOk != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_StartGrabbing error: " + toHexStr(sdkOk));
        }
        this->status = CodeReaderStatus::Grabbing;
    }
}

void stopDevice(const std::string &sn) {
    CodeReader *cr = getDevice(sn, false);
    if (cr != nullptr) {
        cr->close();
    }
}

void startDevice(const std::string &sn, const CodeReaderOpenParams &params,
                 const std::optional<CodeReaderBcrCallback> &onBcrCodes) {
    CodeReader *cr = getDevice(sn, true);

    if (cr->status == CodeReaderStatus::Grabbing) {
        if (onBcrCodes.has_value()) {
            registerImageCallbackForSerial(sn, *onBcrCodes);
        }
        return;
    }

    if (onBcrCodes.has_value()) {
        registerImageCallbackForSerial(sn, *onBcrCodes);
    }

    if (cr->status == CodeReaderStatus::Connected) {
        cr->open();
    }
    applyCodeReaderOpenParams(cr, params);
    cr->startGrabbing();
}
