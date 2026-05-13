/**
 * @file device_control.cpp
 * @brief 读码器会话：状态迁移（Connected → Open → Grabbing）、`startDevice` / `stopDevice`，
 *      以及在已 Open 句柄上写入 `CodeReaderOpenParams`（四项 GenICam，与设备 XML 一致）。
 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <stdexcept>

namespace {

    /** SDK 返回非 MV_CODEREADER_OK 时抛出 runtime_error（带十六进制错误码）。 */
    void checkSdkOk(int ok, const char *apiName) {
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error(std::string(apiName) + " error: " + toHexStr(ok));
        }
    }

    /**
     * Connected → Open：调用 MV_CODEREADER_OpenDevice。
     * 若当前不是 Connected，则什么也不做（幂等，供 `open` / `grabbing` 共用）。
     */
    void openDeviceFromConnected(CodeReader *d) {
        if (d->status != CodeReaderStatus::Connected) {
            return;
        }
        checkSdkOk(MV_CODEREADER_OpenDevice(d->handle), "MV_CODEREADER_OpenDevice");
        d->status = CodeReaderStatus::Open;
    }

    /**
     * 在 Open 状态下写入起流前参数（须在 `grabbing()` 内调用 MV_CODEREADER_StartGrabbing 之前完成）。
     * @throws std::logic_error 句柄为空或状态不是 Open
     */
    void applyOpenParamsOnOpenDevice(CodeReader *d, const CodeReaderOpenParams &params) {
        if (d == nullptr) {
            throw std::logic_error("applyOpenParamsOnOpenDevice: null device");
        }
        if (d->status != CodeReaderStatus::Open) {
            throw std::logic_error("applyOpenParamsOnOpenDevice: device must be Open");
        }
        void *h = d->handle;
        checkSdkOk(MV_CODEREADER_SetEnumValueByString(h, "TriggerMode", params.triggerMode.c_str()),
                   "MV_CODEREADER_SetEnumValueByString(TriggerMode)");
        checkSdkOk(MV_CODEREADER_SetEnumValueByString(h, "TriggerSource", params.triggerSource.c_str()),
                   "MV_CODEREADER_SetEnumValueByString(TriggerSource)");
        checkSdkOk(MV_CODEREADER_SetBoolValue(h, "CODE128", params.code128), "MV_CODEREADER_SetBoolValue(CODE128)");
        checkSdkOk(MV_CODEREADER_SetBoolValue(h, "QRCode", params.qrcode), "MV_CODEREADER_SetBoolValue(QRCode)");
    }

} // namespace

// ---------------------------------------------------------------------------
// CodeReader 状态机：open → grabbing → close（与 SDK 生命周期一致）
// ---------------------------------------------------------------------------

void CodeReader::open() {
    if (this->status == CodeReaderStatus::Open) {
        return;
    }
    if (this->status == CodeReaderStatus::Grabbing) {
        throw std::logic_error(
            "CodeReader::open：当前为取流状态，禁止隐式停流；请先 stopDevice 后再打开设备以配置参数");
    }
    openDeviceFromConnected(this);
}

void CodeReader::grabbing() {
    if (this->status == CodeReaderStatus::Grabbing) {
        return;
    }
    // 若尚未 OpenDevice，与 `open()` 相同路径先进入 Open
    openDeviceFromConnected(this);
    if (this->status == CodeReaderStatus::Open) {
        // SDK 要求：RegisterImageCallBack 须在 MV_CODEREADER_StartGrabbing 之前
        codeReaderInternalBindImageCallbackBeforeGrabbing(this);
        checkSdkOk(MV_CODEREADER_StartGrabbing(this->handle), "MV_CODEREADER_StartGrabbing");
        this->status = CodeReaderStatus::Grabbing;
    }
}

void CodeReader::close() {
    if (this->status == CodeReaderStatus::Connected) {
        return;
    }
    // Grabbing → Open：先停流，再 CloseDevice 回到 Connected
    if (this->status == CodeReaderStatus::Grabbing) {
        checkSdkOk(MV_CODEREADER_StopGrabbing(this->handle), "MV_CODEREADER_StopGrabbing");
        this->status = CodeReaderStatus::Open;
    }
    if (this->status == CodeReaderStatus::Open) {
        checkSdkOk(MV_CODEREADER_CloseDevice(this->handle), "MV_CODEREADER_CloseDevice");
        this->status = CodeReaderStatus::Connected;
    }
}

// ---------------------------------------------------------------------------
// 对外 API：按序列号操作缓存中的 CodeReader
// ---------------------------------------------------------------------------

void stopDevice(const std::string &sn) {
    CodeReader *cr = getDevice(sn, false);
    if (cr != nullptr) {
        cr->close();
    }
}

void startDevice(const std::string &sn, const CodeReaderOpenParams &params,
                 const std::optional<CodeReaderBcrCallback> &onBcrCodes) {
    CodeReader *cr = getDevice(sn, true);

    // 已在取流：不再改 Open 阶段参数，仅按第三参更新或保留 BCR 回调绑定
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
    applyOpenParamsOnOpenDevice(cr, params);
    cr->grabbing();
}
