/** 状态迁移、startDevice/stopDevice、起流前四项 GenICam 写参 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <stdexcept>

namespace {

void checkSdk(int ok, const char *api) {
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error(std::string(api) + " error: " + toHexStr(ok));
    }
}

void openIfConnected(CodeReader *d) {
    if (d->status != CodeReaderStatus::Connected) {
        return;
    }
    checkSdk(MV_CODEREADER_OpenDevice(d->handle), "MV_CODEREADER_OpenDevice");
    d->status = CodeReaderStatus::Open;
}

void applyOpenParams(CodeReader *d, const CodeReaderOpenParams &p) {
    if (d->status != CodeReaderStatus::Open) {
        throw std::logic_error("applyOpenParams: expect Open");
    }
    void *h = d->handle;
    checkSdk(MV_CODEREADER_SetEnumValueByString(h, "TriggerMode", p.triggerMode.c_str()), "SetEnum(TriggerMode)");
    checkSdk(MV_CODEREADER_SetEnumValueByString(h, "TriggerSource", p.triggerSource.c_str()), "SetEnum(TriggerSource)");
    checkSdk(MV_CODEREADER_SetBoolValue(h, "CODE128", p.code128), "SetBool(CODE128)");
    checkSdk(MV_CODEREADER_SetBoolValue(h, "QRCode", p.qrcode), "SetBool(QRCode)");
}

} // namespace

void CodeReader::open() {
    if (status == CodeReaderStatus::Open) {
        return;
    }
    if (status == CodeReaderStatus::Grabbing) {
        throw std::logic_error("CodeReader::open: Grabbing 下请先 stopDevice");
    }
    openIfConnected(this);
}

void CodeReader::grabbing() {
    if (status == CodeReaderStatus::Grabbing) {
        return;
    }
    openIfConnected(this);
    if (status == CodeReaderStatus::Open) {
        codeReaderInternalBindImageCallbackBeforeGrabbing(this);
        checkSdk(MV_CODEREADER_StartGrabbing(handle), "MV_CODEREADER_StartGrabbing");
        status = CodeReaderStatus::Grabbing;
    }
}

void CodeReader::close() {
    // 调用方须已持有 g_device_mutex。
    if (status == CodeReaderStatus::Connected) {
        return;
    }
    if (status == CodeReaderStatus::Grabbing) {
        checkSdk(MV_CODEREADER_StopGrabbing(handle), "MV_CODEREADER_StopGrabbing");
        status = CodeReaderStatus::Open;
    }
    if (status == CodeReaderStatus::Open) {
        checkSdk(MV_CODEREADER_CloseDevice(handle), "MV_CODEREADER_CloseDevice");
        status = CodeReaderStatus::Connected;
        // CloseDevice 之后 SDK 不允许拿该句柄再次 OpenDevice（返回 0x80020000），须先重建。
        handleStale = true;
    }
}

void stopDevice(const std::string &sn) {
    std::lock_guard<std::mutex> lock(g_device_mutex);
    CodeReader *cr = findDevice(sn);
    if (cr) {
        cr->close();
    }
}

void startDevice(const std::string &sn, const CodeReaderOpenParams &params,
                 const std::optional<CodeReaderBcrCallback> &onBcrCodes) {
    std::lock_guard<std::mutex> lock(g_device_mutex);
    CodeReader *cr = getOrCreateDevice(sn);
    const auto reg = [&] {
        if (onBcrCodes) {
            registerImageCallbackForSerial(sn, *onBcrCodes);
        }
    };
    if (cr->status == CodeReaderStatus::Grabbing) {
        reg();
        return;
    }
    reg();
    if (cr->status == CodeReaderStatus::Connected) {
        if (cr->handleStale) {
            // 修复：stop 后再 start 时旧句柄已 CloseDevice，重建后 OpenDevice 才能成功。
            cr->recreateHandle();
        }
        cr->open();
    }
    applyOpenParams(cr, params);
    cr->grabbing();
}
